// moe service
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "efi.h"
#include "setjmp.h"


#define DEFAULT_QUANTUM     5
#define CONSUME_QUANTUM_THRESHOLD 2500
#define THREAD_NAME_SIZE    32
#define CLEANUP_LOAD_TIME   1000000
#define DEFAULT_SCHEDULE_SIZE   1024
#define DEFAULT_SCHEDULE_QUEUES 2
#define BLOCK_AFTER_RETIRE  1000
#define PREEMPT_PENALTY     1000


typedef uint32_t moe_affinity_t;
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
            uint8_t reserved2: 6;
            uint8_t zombie: 1;
            uint8_t running: 1;
        };
    };
    thid_t thid;
    _Atomic uint64_t measure0;
    _Atomic uint64_t cputime;
    _Atomic uint32_t load00, load0, load;
    moe_affinity_t affinity;
    _Atomic uint8_t quantum_left;
    uint8_t quantum, quantum_min, quantum_max;
    moe_timer_t block;
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


typedef struct {
    int cpuid;
    moe_thread_t* idle;
    _Atomic (moe_thread_t*) current;
} core_specific_data_t;

moe_thread_t *root_thread;
_Atomic thid_t next_thid;
int n_active_cpu;
_Atomic moe_affinity_t system_affinity;

struct {
    moe_fifo_t* ready[DEFAULT_SCHEDULE_QUEUES];
    moe_fifo_t* retired;
    _Atomic moe_affinity_t core_lock;
    atomic_flag lock;
} thread_queue;

core_specific_data_t* core_data;


moe_thread_t* get_current_thread() {
    uintptr_t cpuid = moe_get_current_cpuid();
    return atomic_load(&core_data[cpuid].current);
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
        return moe_fifo_write(thread_queue.ready[pri], (uintptr_t)thread);
    } else {
        return -1;
    }
}

int sch_retire(moe_thread_t* thread) {
    if (thread->zombie) {
        return 0;
    }
    if (thread->priority) {
        while (atomic_bit_test_and_set(&thread_queue.lock, 0)) {
            io_pause();
        }
        if (!thread->block) {
            thread->block = moe_create_interval_timer(BLOCK_AFTER_RETIRE);
        }
        int retval = moe_fifo_write(thread_queue.retired, (uintptr_t)thread);
        atomic_bit_test_and_clear(&thread_queue.lock, 0);
        return retval;
    } else {
        return -1;
    }
}

moe_thread_t *sch_next() {
    moe_thread_t *result;
    for(int i = 0; i < DEFAULT_SCHEDULE_QUEUES; i++) {
        result = (moe_thread_t*)moe_fifo_read(thread_queue.ready[i], 0);
        if (result) {
            if (moe_check_timer(&result->block)) {
                sch_retire(result);
            } else {
                return result;
            }
        }
    }

    //  Wait queue is empty
    if (!atomic_bit_test_and_set(&thread_queue.lock, 0)) {
        moe_thread_t *p;
        while ((p = (moe_thread_t*)moe_fifo_read(thread_queue.retired, 0))) {
            sch_add(p);
        }
        atomic_bit_test_and_clear(&thread_queue.lock, 0);
    }

    return core_data[moe_get_current_cpuid()].idle;
}

void moe_unlock_core() {
    uint32_t eflags = io_lock_irq();
    int cpuid = moe_get_current_cpuid();
    atomic_bit_test_and_clear(&thread_queue.core_lock, cpuid);
    io_unlock_irq(eflags);
}

// moe_affinity_t lock_all_cores() {
//     moe_affinity_t old_lock = 0;
//     while (!atomic_compare_exchange_strong(&thread_queue.core_lock, &old_lock, system_affinity)) {
//         io_pause();
//     }
//     return old_lock;
// }

// int unlock_all_cores(moe_affinity_t lock) {
//     moe_affinity_t expected = system_affinity;
//     return atomic_compare_exchange_strong(&thread_queue.core_lock, &expected, lock);
// }

//  Context Swicth
static void next_thread(uint32_t cpuid, moe_thread_t* current, moe_timer_t timer) {
    if (!atomic_bit_test_and_set(&thread_queue.core_lock, cpuid)) {
        moe_thread_t* next = sch_next();
        uint64_t load = moe_get_thread_load(current);
        atomic_fetch_add(&current->cputime, load);
        atomic_fetch_add(&current->load0, load);
        if (next != current) {
            MOE_ASSERT(!next->running, "THREAD RUNNING %d <= %d #%d\n", next->thid, current->thid, cpuid);
            if (!setjmp(current->jmpbuf)) {
                if (current->fpu_used) {
                    io_fsave(current->fpu_context);
                    current->fpu_used = 0;
                }
                io_set_lazy_fpu_restore();
                moe_thread_t* expected_current = current;
                if (atomic_compare_exchange_strong(&core_data[cpuid].current, &expected_current, next)) {
                    current->block = timer;
                    current->running = 0;
                    next->measure0 = moe_get_measure();
                    next->affinity |= (1 << cpuid);
                    next->last_cpuid = cpuid;
                    next->running = 1;
                    // core_data[cpuid].current = next;
                    sch_retire(current);
                    longjmp(next->jmpbuf, 0);
                } else {
                    sch_add(next);
                    MOE_ASSERT(false, "WTF? #%d %08zx(%d) != %08zx(%d)\n", cpuid, (uintptr_t)expected_current, expected_current->thid, (uintptr_t)current, current->thid);
                }
            }
        } else {
            current->measure0 = moe_get_measure();
        }
        moe_unlock_core();
    }
}

void on_thread_start() {
    // uint32_t eflags = io_lock_irq();
    uint32_t cpuid = moe_get_current_cpuid();
    moe_thread_t* current = core_data[cpuid].current;
    current->measure0 = moe_get_measure();
    current->affinity |= (1 << cpuid);
    current->last_cpuid = cpuid;
    current->running = 1;
    // io_unlock_irq(eflags);
    moe_unlock_core();
}

//  Lazy FPU restore
void moe_fpu_restore(uintptr_t delta) {
    moe_thread_t* current = get_current_thread();
    if (current->fpu_context) {
        io_fload(current->fpu_context);
    } else {
        io_finit();
        current->fpu_context = mm_alloc_static(delta);
    }
    current->fpu_used = 1;
}


void moe_consume_quantum() {
    if (!core_data) return;
    uint32_t cpuid = moe_get_current_cpuid();
    moe_thread_t* current = core_data[cpuid].current;
    if (current->quantum > 0) {
        if (current->priority < priority_highest && moe_fifo_get_estimated_count(thread_queue.ready[0])) {
            next_thread(cpuid, current, 0);
            return;
        }
        uint32_t cload = moe_get_thread_load(current);
        uint32_t load = atomic_fetch_add(&current->load00, cload);
        if (load + cload > CONSUME_QUANTUM_THRESHOLD) {
            atomic_fetch_sub(&current->load00, CONSUME_QUANTUM_THRESHOLD);
            if (atomic_fetch_add(&current->quantum_left, -1) <= 0) {
                if (current->quantum > current->quantum_min) {
                    current->quantum--;
                }
                atomic_fetch_add(&current->quantum_left, current->quantum);
                next_thread(cpuid, current, moe_create_interval_timer(PREEMPT_PENALTY));
            }
        }
    } else {
        next_thread(cpuid, current, 0);
    }
}

void moe_yield() {
    if (!core_data) {
        io_hlt();
        return;
    }
    moe_usleep(0);
}

int moe_wait_for_timer(moe_timer_t* timer) {
    if (!core_data) {
        while (moe_check_timer(timer)) {
            io_hlt();
        }
    } else {
        uint32_t eflags = io_lock_irq();
        uint32_t cpuid = moe_get_current_cpuid();
        moe_thread_t* current = core_data[cpuid].current;

        if (current->quantum < current->quantum_max) {
            current->quantum++;
        }

        next_thread(cpuid, current, *timer);
        io_unlock_irq(eflags);
    }
    return 0;
}

int moe_usleep(uint64_t us) {
    moe_timer_t timer = moe_create_interval_timer(us);
    return moe_wait_for_timer(&timer);
}

_Noreturn void moe_exit_thread(uint32_t exit_code) {
    moe_thread_t* current = get_current_thread();
    current->zombie = 1;
    moe_yield();
    for (;;) io_hlt();
}

moe_thread_t* create_thread(moe_thread_start start, moe_priority_level_t priority, void* args, const char* name) {

    moe_thread_t* new_thread = mm_alloc_static(sizeof(moe_thread_t));
    memset(new_thread, 0, sizeof(moe_thread_t));
    new_thread->thid = atomic_fetch_add(&next_thid, 1);
    new_thread->priority = priority;
    if (priority) {
        new_thread->quantum = DEFAULT_QUANTUM;
        new_thread->quantum_min = DEFAULT_QUANTUM / 2;
        new_thread->quantum_max = DEFAULT_QUANTUM * 3;
        new_thread->quantum_left = new_thread->quantum_max;
    }
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

    if (root_thread) {
        moe_thread_t* p = root_thread;
        for (; p->next; p = p->next) {}
        p->next = new_thread;
    } else {
        root_thread = new_thread;
    }

    sch_add(new_thread);

    return new_thread;
}

int moe_create_thread(moe_thread_start start, moe_priority_level_t priority, void* args, const char* name) {
    return create_thread(start, priority ? priority : priority_normal, args, name)->thid;
}


_Noreturn void moe_startup_ap(int cpuid) {
    atomic_bit_test_and_set(&system_affinity, cpuid);
    for (;;) io_hlt();
}


_Noreturn void scheduler_thread() {
    int64_t last_cleanup_load_measure = 0;
    for (;;) {
        int64_t measure = moe_get_measure();
        if ((measure - last_cleanup_load_measure) >= CLEANUP_LOAD_TIME) {

            //  Update load
            moe_thread_t* thread = root_thread;
            for (; thread; thread = thread->next) {
                int load = atomic_load(&thread->load0);
                atomic_store(&thread->load, load);
                atomic_fetch_add(&thread->load0, -load);
            }

            last_cleanup_load_measure = measure;
        }
        moe_yield();
    }
}

int moe_get_usage() {
    int total = 0;
    for (int i = 0; i < n_active_cpu; i++) {
        total += core_data[i].idle->load;
    }
    int usage = 1000 - (total / (n_active_cpu * 1000));
    return usage > 0 ? usage: 0;
}

void thread_init(int _n_active_cpu) {

    char name[THREAD_NAME_SIZE];
    n_active_cpu = _n_active_cpu;
    uintptr_t size = n_active_cpu * sizeof(core_specific_data_t);

    core_specific_data_t* _core_data = mm_alloc_static(size);
    memset(_core_data, 0, size);

    for (int i = 0; i < DEFAULT_SCHEDULE_QUEUES; i++) {
        thread_queue.ready[i] = moe_fifo_init(DEFAULT_SCHEDULE_SIZE);
    }
    thread_queue.retired = moe_fifo_init(DEFAULT_SCHEDULE_SIZE);

    for (int i = 0; i < n_active_cpu; i++) {
        _core_data[i].cpuid = i;
        snprintf(name, THREAD_NAME_SIZE, "(Idle Core #%d)", i);
        moe_thread_t* th = create_thread(NULL, priority_idle, NULL, name);
        th->affinity = 1 << i;
        th->last_cpuid = i;
        th->running = 1;
        _core_data[i].idle = th;
        _core_data[i].current = th;
    }
    core_data = _core_data;

    moe_create_thread(&scheduler_thread, priority_highest, 0, "Scheduler");
}


/*********************************************************************/
//  First In, First Out method

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
            uintptr_t read_ptr = atomic_fetch_add_explicit(&self->read, 1, memory_order_seq_cst);
            intptr_t retval = atomic_load_explicit(&self->data[read_ptr & self->mask], memory_order_seq_cst);
            atomic_fetch_add_explicit(&self->free, 1, memory_order_seq_cst);
            return retval;
        } else {
            io_pause();
        }
    }
    return default_val;
}

int moe_fifo_write(moe_fifo_t* self, intptr_t data) {
    uint32_t free = atomic_load(&self->free);
    while (free > 0) {
        if (atomic_compare_exchange_weak(&self->free, &free, free - 1)) {
            uintptr_t write_ptr = atomic_fetch_add_explicit(&self->write, 1, memory_order_seq_cst);
            atomic_store_explicit(&self->data[write_ptr & self->mask], data, memory_order_seq_cst);
            atomic_fetch_add_explicit(&self->count, 1, memory_order_seq_cst);
            return 0;
        } else {
            io_pause();
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
    moe_thread_t* p = root_thread;
    printf("ID context  attr     affinity      usage cpu time   name\n");
    for (; p; p = p->next) {
        uint64_t time = p->cputime / 1000000;
        uint32_t time0 = time % 60;
        uint32_t time1 = (time / 60) % 60;
        uint32_t time2 = (time / 3600);
        printf("%2d %08zx %08zx %08x %2d/%2d %4u %4u:%02u:%02u %s\n",
            (int)p->thid, (uintptr_t)p, p->flags, p->affinity,
            p->quantum_left, p->quantum, p->load / 1000, time2, time1, time0,
            p->name);
    }
}
