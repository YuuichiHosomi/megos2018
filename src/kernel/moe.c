// moe service
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "efi.h"
#include "setjmp.h"

#define DEFAULT_QUANTUM     5
#define CLEANUP_LOAD_TIME   1000000
#define CONSUME_QUANTUM_THRESHOLD 1000

char *strncpy(char *s1, const char *s2, size_t n);

extern void setjmp_new_thread(jmp_buf env, uintptr_t* new_sp);

typedef int thid_t;

typedef struct _moe_fiber_t moe_fiber_t;

#define THREAD_NAME_SIZE    32
typedef struct _moe_fiber_t {
    moe_fiber_t* next;
    thid_t      thid;
    uint8_t quantum_base;
    _Atomic uint8_t quantum_left;
    atomic_flag lock;
    union {
        uintptr_t flags;
        struct {
            uintptr_t hoge:1;
        };
    };
    _Atomic uint64_t measure0;
    _Atomic uint64_t cputime;
    _Atomic uint32_t load00, load0, load;
    void *fpu_context;
    jmp_buf jmpbuf;
    char name[THREAD_NAME_SIZE];
} moe_fiber_t;

_Atomic thid_t next_thid = 1;
moe_fiber_t *current_thread;
moe_fiber_t *fpu_owner = 0;
moe_fiber_t root_thread;

int moe_get_current_thread() {
    return current_thread->thid;
}

void io_set_lazy_fpu_switch();
void io_finit();
void io_fsave(void*);
void io_fload(void*);

uint64_t moe_get_current_load() {
    return moe_get_measure() - current_thread->measure0;
}

//  Main Context Swicth
void moe_switch_context(moe_fiber_t* next) {
    MOE_ASSERT(current_thread != next, "CONTEXT SWITCH SKIPPING (%zx %zx)\n", current_thread, current_thread->thid);
    if (current_thread == next) return;

    if (atomic_flag_test_and_set(&current_thread->lock)) return;

    uint64_t load = moe_get_current_load();
    atomic_fetch_add(&current_thread->cputime, load);
    atomic_fetch_add(&current_thread->load0, load);
    if (!setjmp(current_thread->jmpbuf)) {
        if (fpu_owner != next) {
            io_set_lazy_fpu_switch();
        }
        current_thread = next;
        current_thread->measure0 = moe_get_measure();
        longjmp(next->jmpbuf, 0);
    }

    atomic_flag_clear(&current_thread->lock);
}

//  Lazy FPU Context Switch
void moe_switch_fpu_context(uintptr_t delta) {
    if (fpu_owner == current_thread) {
        ;
    } else {
        if (fpu_owner) {
            io_fsave(fpu_owner->fpu_context);
        }
        fpu_owner = current_thread;
        if (current_thread->fpu_context) {
            io_fload(current_thread->fpu_context);
        } else {
            io_finit();
            current_thread->fpu_context = mm_alloc_static(delta);
        }
    }
}

void moe_next_thread() {
    moe_fiber_t* next = current_thread->next;
    if (!next) next = &root_thread;
    moe_switch_context(next);
}

void moe_consume_quantum() {
    uint32_t cload = moe_get_current_load();
    uint32_t load = atomic_fetch_add(&current_thread->load00, cload);
    if (load + cload > CONSUME_QUANTUM_THRESHOLD) {
        atomic_fetch_sub(&current_thread->load00, CONSUME_QUANTUM_THRESHOLD);
        if (atomic_fetch_add(&current_thread->quantum_left, -1) <= 0) {
            atomic_fetch_add(&current_thread->quantum_left, current_thread->quantum_base);
            moe_next_thread();
        }
    }
}

void moe_yield() {
    if (current_thread->quantum_left > current_thread->quantum_base) {
        atomic_store(&current_thread->quantum_left, current_thread->quantum_base);
    }
    moe_next_thread();
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

int moe_create_thread(moe_start_thread start, void* args, const char* name) {

    moe_fiber_t* new_thread = mm_alloc_static(sizeof(moe_fiber_t));
    memset(new_thread, 0, sizeof(moe_fiber_t));
    // new_thread->lock = ATOMIC_FLAG_INIT;
    new_thread->thid = atomic_fetch_add(&next_thid, 1);
    new_thread->quantum_base = DEFAULT_QUANTUM;
    new_thread->quantum_left = 3 * DEFAULT_QUANTUM;
    if (name) {
        strncpy(&new_thread->name[0], name, THREAD_NAME_SIZE - 1);
    }
    const uintptr_t stack_count = 0x1000;
    const uintptr_t stack_size = stack_count * sizeof(uintptr_t);
    uintptr_t* stack = mm_alloc_static(stack_size);
    memset(stack, 0, stack_size);
    uintptr_t* sp = stack + stack_count;
    *--sp = 0x00007fffdeadbeef;
    *--sp = (uintptr_t)args;
    *--sp = (uintptr_t)start;
    setjmp_new_thread(new_thread->jmpbuf, sp);

    moe_fiber_t* p = &root_thread;
    for (; p->next; p = p->next) {}
    p->next = new_thread;

    // moe_switch_context(new_thread);

    return new_thread->thid;
}


_Noreturn void scheduler() {
    int pid = moe_get_current_thread();
    int64_t last_cleanup_load_measure = 0;
    for (;;) {
        int64_t measure = moe_get_measure();
        if ((measure - last_cleanup_load_measure) >= CLEANUP_LOAD_TIME) {

            //  Update load
            moe_fiber_t* thread = &root_thread;
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

            last_cleanup_load_measure = moe_get_measure();
        }
        moe_yield();
    }
}

void thread_init() {

    current_thread = &root_thread;
    root_thread.quantum_base = 1;
    strncpy(&root_thread.name[0], "(System Idle Thread)", THREAD_NAME_SIZE - 1);

    moe_create_thread(&scheduler, 0, "Scheduler");
}

/*********************************************************************/

typedef struct _moe_fifo_t {
    volatile intptr_t* data;
    atomic_uintptr_t read, write, free, count;
    uintptr_t mask, flags;
} moe_fifo_t;


void moe_fifo_init(moe_fifo_t** result, uintptr_t capacity) {
    moe_fifo_t* self = mm_alloc_static(sizeof(moe_fifo_t));
    self->data = mm_alloc_static(capacity * sizeof(uintptr_t));
    self->read = self->write = self->count = self->flags = 0;
    self->free = self->mask = capacity - 1;
    *result = self;
}

intptr_t moe_fifo_read(moe_fifo_t* self, intptr_t default_val) {
    uintptr_t count = self->count;
    while (count > 0) {
        if (atomic_compare_exchange_weak(&self->count, &count, count - 1)) {
            uintptr_t read_ptr = atomic_fetch_add(&self->read, 1);
            intptr_t retval = self->data[read_ptr & self->mask];
            atomic_fetch_add(&self->free, 1);
            return retval;
        } else {
            io_pause();
            count = self->count;
        }
    }
    return default_val;
}

int moe_fifo_write(moe_fifo_t* self, intptr_t data) {
    uintptr_t free = self->free;
    while (free > 0) {
        if (atomic_compare_exchange_strong(&self->free, &free, free - 1)) {
            uintptr_t write_ptr = atomic_fetch_add(&self->write, 1);
            self->data[write_ptr & self->mask] = data;
            atomic_fetch_add(&self->count, 1);
            return 0;
        } else {
            io_pause();
            free = self->free;
        }
    }
    return -1;
}

/*********************************************************************/

void display_threads() {
    moe_fiber_t* p = &root_thread;
    printf("ID context  sse_ctx  flags         usage cpu time   name\n");
    for (; p; p = p->next) {
        uint64_t time = p->cputime / 1000000;
        uint32_t time0 = time % 60;
        uint32_t time1 = (time / 60) % 60;
        uint32_t time2 = (time / 3600);
        printf("%2d %08zx %08zx %08zx %2d/%2d %4u %4u:%02u:%02u %s\n",
            (int)p->thid, (uintptr_t)p, (uintptr_t)p->fpu_context, p->flags,
            p->quantum_left, p->quantum_base, p->load / 1000, time2, time1, time0,
            p->name);
    }
}
