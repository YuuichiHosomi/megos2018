// moe service
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "efi.h"
#include "setjmp.h"

#define DEFAULT_QUANTUM     3
#define CLEANUP_LOAD_TIME   1000000
#define CONSUME_QUANTUM_THRESHOLD 1000
#define THREAD_NAME_SIZE    32
#define DEFAULT_SCHEDULE_SIZE   1024
#define DEFAULT_SCHEDULE_QUEUES 2


typedef int thid_t;
typedef struct moe_thread_t moe_thread_t;

typedef struct moe_thread_t {
    moe_thread_t* next;
    union {
        _Atomic uintptr_t flags;
        struct {
            moe_priority_level_t priority;
            uint8_t last_cpuid;
            uint8_t reserved1: 7;
            uint8_t fpu_used: 1;
            uint8_t reserved2: 7;
            uint8_t running: 1;
        };
    };
    thid_t thid;
    _Atomic uint64_t measure0;
    _Atomic uint64_t cputime;
    _Atomic uint32_t load00, load0, load;
    uint32_t affinity;
    uint8_t quantum_base;
    _Atomic uint8_t quantum_left;
    atomic_flag lock;
    void *fpu_context;
    jmp_buf jmpbuf;
    char name[THREAD_NAME_SIZE];
} moe_thread_t;

enum {
    thread_flags_running = 31,
};

extern void io_set_lazy_fpu_restore();
extern void io_finit();
extern void io_fsave(void*);
extern void io_fload(void*);
extern uint32_t io_lock_irq();
extern void io_unlock_irq(uint32_t);
extern void setjmp_new_thread(jmp_buf env, uintptr_t* new_sp);
extern uintptr_t moe_get_current_cpuid();
char *strncpy(char *s1, const char *s2, size_t n);
int snprintf(char* buffer, size_t n, const char* format, ...);


_Atomic thid_t next_thid = 1;
_Atomic uint32_t system_affinity = 1;

moe_thread_t **idle_threads;
_Atomic (moe_thread_t *)*current_threads;
moe_thread_t root_thread;
int n_active_cpu = 1;

struct {
    moe_fifo_t* waiting[DEFAULT_SCHEDULE_QUEUES];
    moe_fifo_t* retired;
    atomic_flag lock;
} thread_queue;


moe_thread_t* get_current_thread() {
    uintptr_t cpuid = moe_get_current_cpuid();
    return atomic_load(&current_threads[cpuid]);
}

int moe_get_current_thread() {
    return get_current_thread()->thid;
}

uint64_t moe_get_thread_load(moe_thread_t* thread) {
    return moe_get_measure() - atomic_load(&thread->measure0);
}

int sch_add(moe_thread_t *thread) {
    if (thread->priority) {
        int pri = thread->priority >= priority_highest ? 0 : 1;
        return moe_fifo_write(thread_queue.waiting[pri], (uintptr_t)thread);
    } else {
        return -1;
    }
}

int sch_retire(moe_thread_t* thread) {
    if (thread->priority) {
        return moe_fifo_write(thread_queue.retired, (uintptr_t)thread);
    } else {
        return -1;
    }
}

moe_thread_t *sch_next() {
    moe_thread_t *result;
    for(int i = 0; i < DEFAULT_SCHEDULE_QUEUES; i++) {
        result = (moe_thread_t*)moe_fifo_read(thread_queue.waiting[i], 0);
        if (result) return result;
    }

    //  Wait queue is empty
    if (!atomic_flag_test_and_set(&thread_queue.lock)) {
        moe_thread_t *p;
        while ((p = (moe_thread_t*)moe_fifo_read(thread_queue.retired, 0))) {
            sch_add(p);
        }
        atomic_flag_clear(&thread_queue.lock);
    }

    return idle_threads[moe_get_current_cpuid()];
}

//  Main Context Swicth
static void switch_context(uint32_t cpuid, moe_thread_t* current, moe_thread_t* next) {
    uint64_t load = moe_get_thread_load(current);
    atomic_fetch_add(&current->cputime, load);
    atomic_fetch_add(&current->load0, load);
    if (current->fpu_used) {
        io_fsave(current->fpu_context);
        current->fpu_used = 0;
    }
    if (!setjmp(current->jmpbuf)) {
        sch_retire(current);
        if (atomic_compare_exchange_strong(&current_threads[cpuid], &current, next)) {
            io_set_lazy_fpu_restore();
            current->running = 0;
            next->measure0 = moe_get_measure();
            next->affinity |= (1 << cpuid);
            next->last_cpuid = cpuid;
            next->running = 1;
            longjmp(next->jmpbuf, 0);
        } else {
            MOE_ASSERT(false, "WTF?");
        }
    }
}

static void next_thread() {
    uint32_t cpuid = moe_get_current_cpuid();
    moe_thread_t* current = current_threads[cpuid];
    if (!atomic_flag_test_and_set(&current->lock)) {
        moe_thread_t* next = sch_next();
        if (next != current) {
            switch_context(cpuid, current, next);
        }
        atomic_flag_clear(&current->lock);
    }
}

//  Lazy FPU restore
void moe_fpu_restore(uintptr_t delta) {
    int cpuid = moe_get_current_cpuid();
    moe_thread_t* current = current_threads[cpuid];
    if (current->fpu_context) {
        io_fload(current->fpu_context);
    } else {
        io_finit();
        current->fpu_context = mm_alloc_static(delta);
    }
    current->fpu_used = 1;
}


void moe_consume_quantum() {
    if (!current_threads) return;
    moe_thread_t* current = get_current_thread();
    if (current->quantum_base > 0) {
        if (current->priority < priority_highest && moe_fifo_get_estimated_count(thread_queue.waiting[0])) {
            next_thread();
        }
        uint32_t cload = moe_get_thread_load(current);
        uint32_t load = atomic_fetch_add(&current->load00, cload);
        if (load + cload > CONSUME_QUANTUM_THRESHOLD) {
            atomic_fetch_sub(&current->load00, CONSUME_QUANTUM_THRESHOLD);
            if (atomic_fetch_add(&current->quantum_left, -1) <= 0) {
                atomic_fetch_add(&current->quantum_left, current->quantum_base);
                next_thread();
            }
        }
    } else {
        next_thread();
    }
}

void moe_yield() {
    if (!current_threads) {
        io_hlt();
        return;
    }
    uint32_t eflags = io_lock_irq();
    moe_thread_t* current = get_current_thread();
    if (atomic_load(&current->quantum_left) > current->quantum_base) {
        atomic_store(&current->quantum_left, current->quantum_base);
    }
    next_thread();
    io_unlock_irq(eflags);
}

int moe_wait_for_timer(moe_timer_t* timer) {
    while (moe_check_timer(timer)) {
        moe_yield();
    }
    return 0;
}

int moe_usleep(uint64_t us) {
    moe_timer_t timer = moe_create_interval_timer(us);
    return moe_wait_for_timer(&timer);
}

moe_thread_t* create_thread(moe_start_thread start, moe_priority_level_t priority, void* args, const char* name) {

    moe_thread_t* new_thread = mm_alloc_static(sizeof(moe_thread_t));
    memset(new_thread, 0, sizeof(moe_thread_t));
    new_thread->thid = atomic_fetch_add(&next_thid, 1);
    new_thread->priority = priority;
    new_thread->quantum_base = new_thread->priority * DEFAULT_QUANTUM;
    new_thread->quantum_left = 3 * new_thread->quantum_base; // quantum boost
    // new_thread->affinity = system_affinity;
    if (name) {
        strncpy(&new_thread->name[0], name, THREAD_NAME_SIZE - 1);
    }
    if (start) {
        const uintptr_t stack_count = 0x1000;
        const uintptr_t stack_size = stack_count * sizeof(uintptr_t);
        uintptr_t* stack = mm_alloc_static(stack_size);
        memset(stack, 0, stack_size);
        uintptr_t* sp = stack + stack_count;
        *--sp = 0x00007fffdeadbeef;
        *--sp = (uintptr_t)args;
        *--sp = (uintptr_t)start;
        setjmp_new_thread(new_thread->jmpbuf, sp);
    }

    moe_thread_t* p = &root_thread;
    for (; p->next; p = p->next) {}
    p->next = new_thread;

    sch_add(new_thread);

    return new_thread;
}

int moe_create_thread(moe_start_thread start, moe_priority_level_t priority, void* args, const char* name) {
    return create_thread(start, priority ? priority : priority_normal, args, name)->thid;
}


_Noreturn void moe_startup_ap(int cpuid) {
    atomic_bit_test_and_set(&system_affinity, cpuid);
    // printf("Hello AP! (%d)\n", cpuid);
    for (;;) io_hlt();
}


_Noreturn void scheduler() {
    int64_t last_cleanup_load_measure = 0;
    for (;;) {
        int64_t measure = moe_get_measure();
        if ((measure - last_cleanup_load_measure) >= CLEANUP_LOAD_TIME) {

            //  Update load
            moe_thread_t* thread = &root_thread;
            for (; thread; thread = thread->next) {
                int load = atomic_load(&thread->load0);
                atomic_store(&thread->load, load);
                atomic_fetch_add(&thread->load0, -load);
            }

            //  usage icon
            // {
            //     int left = pid * 10 + 1, width = 6;
            //     int load = (1000000 - root_thread.load) / 200000;
            //     if (load < 0) load = 0;
            //     if (load > 3) {
            //         mgs_fill_rect(left, 2, width, 8, 0xFF0000);
            //     } else {
            //         int ux = load * 2;
            //         int lx = (4 - load) * 2;
            //         mgs_fill_rect(left, 2, width, lx, 0x555555);
            //         mgs_fill_rect(left, 2 + lx, width, ux, 0x00FF00);
            //     }
            // }

            last_cleanup_load_measure = measure;
        }
        moe_yield();
    }
}

void smp_start(int _n_active_cpu) {

    char name[THREAD_NAME_SIZE];
    n_active_cpu = _n_active_cpu;
    uintptr_t size = n_active_cpu * sizeof(void*);
    moe_thread_t** p;

    for (int i = 0; i < DEFAULT_SCHEDULE_QUEUES; i++) {
        thread_queue.waiting[i] = moe_fifo_init(DEFAULT_SCHEDULE_SIZE);
    }
    thread_queue.retired = moe_fifo_init(DEFAULT_SCHEDULE_SIZE);

    p = mm_alloc_static(size);
    p[0] = &root_thread;
    for (int i = 1; i < n_active_cpu; i++) {
        snprintf(name, THREAD_NAME_SIZE, "(IDLE CPU=%d)", i);
        moe_thread_t* th = create_thread(NULL, priority_idle, NULL, name);
        th->affinity = 1 << i;
        p[i] = th;
    }
    idle_threads = p;

    p = mm_alloc_static(size);
    memcpy(p, idle_threads, size);
    current_threads = (_Atomic (moe_thread_t*)*) p;

    moe_create_thread(&scheduler, priority_highest, 0, "Scheduler");
}

void thread_init() {

    root_thread.affinity = 1;
    strncpy(&root_thread.name[0], "(System Idle Thread)", THREAD_NAME_SIZE - 1);

}

/*********************************************************************/

typedef struct moe_fifo_t {
    _Atomic intptr_t* data;
    _Atomic uint32_t read, write, free, count;
    uint32_t mask;
} moe_fifo_t;


moe_fifo_t* moe_fifo_init(uintptr_t capacity) {
    moe_fifo_t* self = mm_alloc_static(sizeof(moe_fifo_t));
    self->data = mm_alloc_static(capacity * sizeof(uintptr_t));
    self->read = 0;
    self->write = 0;
    self->free = capacity - 1;
    self->count = 0;
    self->mask = capacity - 1;
    return self;
}

intptr_t moe_fifo_read(moe_fifo_t* self, intptr_t default_val) {
    uint32_t count = atomic_load(&self->count);
    while (count > 0) {
        if (atomic_compare_exchange_weak(&self->count, &count, count - 1)) {
            uintptr_t read_ptr = atomic_fetch_add(&self->read, 1);
            intptr_t retval = atomic_load(&self->data[read_ptr & self->mask]);
            atomic_fetch_add(&self->free, 1);
            return retval;
        } else {
            io_pause();
            count = atomic_load(&self->count);
        }
    }
    return default_val;
}

int moe_fifo_write(moe_fifo_t* self, intptr_t data) {
    uint32_t free = atomic_load(&self->free);
    while (free > 0) {
        if (atomic_compare_exchange_strong(&self->free, &free, free - 1)) {
            uintptr_t write_ptr = atomic_fetch_add(&self->write, 1);
            atomic_store(&self->data[write_ptr & self->mask], data);
            atomic_fetch_add(&self->count, 1);
            return 0;
        } else {
            io_pause();
            free = atomic_load(&self->free);
        }
    }
    return -1;
}

uintptr_t moe_fifo_get_estimated_count(moe_fifo_t* self) {
    return atomic_load(&self->count);
}

uintptr_t moe_fifo_get_estimated_free(moe_fifo_t* self) {
    return atomic_load(&self->free);
}

/*********************************************************************/

void display_threads() {
    moe_thread_t* p = &root_thread;
    printf("ID context  sse_ctx  flags    affinity      usage cpu time   name\n");
    for (; p; p = p->next) {
        uint64_t time = p->cputime / 1000000;
        uint32_t time0 = time % 60;
        uint32_t time1 = (time / 60) % 60;
        uint32_t time2 = (time / 3600);
        printf("%2d %08zx %08zx %08zx %08x %2d/%2d %4u %4u:%02u:%02u %s\n",
            (int)p->thid, (uintptr_t)p, (uintptr_t)p->fpu_context, p->flags, p->affinity,
            p->quantum_left, p->quantum_base, p->load / 1000, time2, time1, time0,
            p->name);
    }
}
