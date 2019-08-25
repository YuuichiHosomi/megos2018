// MOE
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"


#define DEFAULT_QUANTUM     3
#define CONSUME_QUANTUM_THRESHOLD 2500
#define THREAD_NAME_SIZE    32
#define CLEANUP_LOAD_TIME   1000
#define DEFAULT_SCHEDULE_SIZE   256
#define MAX_THREADS         256
#define CONTEXT_SAVE_AREA_SIZE  1024

typedef uint32_t moe_affinity_t;
typedef int context_id;


typedef union {
    uint8_t context_save_area[CONTEXT_SAVE_AREA_SIZE];
} cpu_context_t;

typedef struct moe_thread_t {
    cpu_context_t context;
    char name[THREAD_NAME_SIZE];

    moe_fiber_t *current_fiber;
    moe_fiber_t *primary_fiber, *last_fiber;
    union {
        uintptr_t flags;
        struct {
            unsigned zombie:1;
            unsigned running:1;
        };
    };
    context_id thid;
    int exit_code;

    moe_priority_level_t priority;
    moe_affinity_t soft_affinity, hard_affinity;
    _Atomic uint8_t quantum_left;
    uint8_t quantum, quantum_min, quantum_max;

    _Atomic (moe_thread_t *) *signal_object;
    moe_measure_t deadline;

} moe_thread_t;


typedef struct moe_fiber_t {
    cpu_context_t context;
    char name[THREAD_NAME_SIZE];

    moe_thread_t *parent_thread;
    moe_fiber_t *next_sibling, *prev_sibling;
    union {
        uintptr_t flags;
        struct {
            unsigned zombie:1;
            unsigned running:1;
        };
    };
    context_id fiber_id;
    int exit_code;

} moe_fiber_t;


typedef enum {
    moe_irql_passive,
    moe_irql_dispatch,
    moe_irql_high,
} moe_irql_t;

typedef struct {
    int cpuid;
    uintptr_t arch_cpuid;
    moe_thread_t* idle;
    _Atomic (moe_thread_t*) current;
    _Atomic (moe_thread_t*) retired;
    _Atomic moe_irql_t irql;
} core_specific_data_t;

static struct {
    _Atomic (moe_thread_t *) *thread_list;
    core_specific_data_t *csd;
    moe_queue_t *ready;
    moe_queue_t *retired;
    _Atomic context_id next_thid;
    _Atomic context_id next_fibid;
    moe_affinity_t system_affinity;
    int ncpu;
    _Atomic int n_active_cpu;
    atomic_flag lock;
} moe;

extern moe_thread_t *_do_switch_context(cpu_context_t *from, cpu_context_t *to);
extern void io_setup_new_thread(moe_thread_t *thread, uintptr_t* new_sp);
extern void io_setup_new_fiber(moe_fiber_t *fiber, uintptr_t* new_sp);
extern int smp_get_current_cpuid();


/*********************************************************************/

static moe_affinity_t AFFINITY(int n) {
    if (sizeof(moe_affinity_t) * 8 > n) {
        return (1ULL << n);
    } else {
        return 0;
    }
}

static core_specific_data_t *_get_current_csd() {
    return &moe.csd[smp_get_current_cpuid()];
}

static moe_thread_t *_get_current_thread() {
    uintptr_t flags = io_lock_irq();
    core_specific_data_t *csd = _get_current_csd();
    moe_thread_t *current = csd->current;
    io_restore_irq(flags);
    return current;
}

static int sch_add(moe_thread_t *thread) {
    if (thread->priority) {
        return moe_queue_write(moe.ready, (uintptr_t)thread);
    } else {
        return -1;
    }
}

static int sch_retire(moe_thread_t *thread) {
    if (!thread) return 0;
    if (thread->zombie) {
        // TODO: remove thread
        return 0;
    }
    if (thread->priority) {
        while (atomic_flag_test_and_set(&moe.lock)) {
            io_pause();
        }
        int retval = moe_queue_write(moe.retired, (intptr_t)thread);
        atomic_flag_clear(&moe.lock);
        return retval;
    } else {
        return -1;
    }
}

static moe_thread_t *sch_next() {
    for (int i = 0; i < 2; i++) {

    moe_thread_t *thread;
    do {
        thread = (moe_thread_t*)moe_queue_read(moe.ready, 0);
        if (thread) {
            if (moe_measure_until(thread->deadline)) {
                sch_retire(thread);
            } else {
                return thread;
            }
        }
    } while (thread);

    //  Wait queue is empty
    if (!atomic_flag_test_and_set(&moe.lock)) {
        moe_thread_t *p;
        while ((p = (moe_thread_t*)moe_queue_read(moe.retired, 0))) {
            sch_add(p);
        }
        atomic_flag_clear(&moe.lock);
    }

    }

    return _get_current_csd()->idle;
}


// Do switch context
static void _next_thread(core_specific_data_t *csd, moe_thread_t *current, _Atomic (moe_thread_t *) *signal_object, moe_measure_t deadline) {
    moe_thread_t *next = sch_next();
    if (current != next) {
        current->signal_object = signal_object;
        current->deadline = deadline;
        current->running = 0;
        csd->current = next;
        next->running = 1;
        csd->retired = current;
        _do_switch_context(&current->context, &next->context);
        csd = _get_current_csd();
        sch_retire(atomic_exchange(&csd->retired, NULL));
    }
}

void thread_on_start() {
    core_specific_data_t *csd = _get_current_csd();
    // moe_thread_t *current = csd->current;
    // current->running = 1;
    sch_retire(atomic_exchange(&csd->retired, NULL));
}


int moe_wait_for_object(_Atomic (moe_thread_t *) *obj, int64_t us) {
    uintptr_t flags = io_lock_irq();
    core_specific_data_t *csd = _get_current_csd();
    moe_thread_t *current = csd->current;
    if (obj) {
        *obj = current;
    }
    moe_measure_t deadline = moe_create_measure(us);
    _next_thread(csd, current, obj, deadline);
    io_restore_irq(flags);
    return 0;
}


int moe_signal_object(_Atomic (moe_thread_t *) *obj) {
    moe_thread_t *thread = *obj;
    _Atomic (moe_thread_t *) *signal_object = thread->signal_object;
    if (signal_object && atomic_compare_exchange_strong(thread->signal_object, &thread, NULL)) {
        thread->deadline = 0;
        return 0;
    } else {
        return -1;
    }
}


static moe_thread_t *_create_thread(moe_thread_start start, moe_priority_level_t priority, void *args, const char *name) {
    moe_thread_t *new_thread = moe_alloc_object(sizeof(moe_thread_t), 1);
    new_thread->thid = atomic_fetch_add(&moe.next_thid, 1);
    new_thread->priority = priority;
    if (priority) {
        new_thread->quantum_min = priority;
        new_thread->quantum_max = DEFAULT_QUANTUM * priority * priority;
        new_thread->quantum = new_thread->quantum_max;
        new_thread->quantum_left = new_thread->quantum_max;
    }
    new_thread->hard_affinity = moe.system_affinity;
    if (name) {
        strncpy(&new_thread->name[0], name, THREAD_NAME_SIZE - 1);
    }
    if (start) {
        const uintptr_t stack_count = 0x2000;
        uintptr_t* stack = moe_alloc_object(stack_count * sizeof(uintptr_t), 1);
        uintptr_t* sp = stack + stack_count;
        *--sp = 0;
        *--sp = 0x00007fffdeadbeef;
        *--sp = (uintptr_t)args;
        *--sp = (uintptr_t)start;
        io_setup_new_thread(new_thread, sp);
    }

    for (int i = 0; i< MAX_THREADS; i++) {
        moe_thread_t *expected = NULL;
        if (atomic_compare_exchange_weak(&moe.thread_list[i], &expected, new_thread))
            break;
    }

    sch_add(new_thread);

    return new_thread;
}

_Noreturn void moe_exit_thread(uint32_t exit_code) {
    moe_thread_t *current = _get_current_thread();
    current->exit_code = exit_code;
    current->zombie = 1;
    moe_usleep(MOE_FOREVER);
    for (;;) io_hlt();
}

void thread_reschedule() {
    if (!moe.csd) return;
    core_specific_data_t *csd = _get_current_csd();
    moe_thread_t *current = csd->current;
    moe_priority_level_t priority = current->priority;
    if (priority >= priority_realtime) {
        // do nothing
    } else if (priority == priority_idle) {
        _next_thread(csd, current, NULL, 0);
    } else {
        int quantum = atomic_fetch_add(&current->quantum_left, -1);
        if (quantum <= 1) {
            atomic_fetch_add(&current->quantum_left, current->quantum);
            _next_thread(csd, current, NULL, 0);
        }
    }
}


int moe_create_thread(moe_thread_start start, moe_priority_level_t priority, void *args, const char *name) {
    moe_thread_t *self = _create_thread(start, priority ? priority : priority_normal, args, name);
    if (self) {
        return self->thid;
    } else {
        return 0;
    }
}

int moe_get_current_thread_id() {
    return _get_current_thread()->thid;
}

const char *moe_get_current_thread_name() {
    return _get_current_thread()->name;
}

int moe_usleep(int64_t us) {
    if (moe.csd) {
        return moe_wait_for_object(NULL, us);
    } else {
        moe_measure_t deadline = moe_create_measure(us);
        while (moe_measure_until(deadline)) {
            io_hlt();
        }
        return 0;
    }
}


moe_fiber_t *_create_fiber(moe_thread_start start, void *args, size_t stack_size, const char *name) {
    moe_thread_t *current_thread = _get_current_thread();
    moe_fiber_t *new_fiber = moe_alloc_object(sizeof(moe_fiber_t), 1);
    new_fiber->parent_thread = current_thread;
    new_fiber->fiber_id = atomic_fetch_add(&moe.next_fibid, 1);
    if (name) {
        strncpy(&new_fiber->name[0], name, THREAD_NAME_SIZE - 1);
    }
    if (start) {
        stack_size = stack_size ? stack_size : 0x10000;
        uintptr_t stack_count = stack_size / sizeof(uintptr_t);
        uintptr_t* stack = moe_alloc_object(stack_size, 1);
        uintptr_t* sp = stack + stack_count;
        *--sp = 0;
        *--sp = 0x00007fffdeadbeef;
        *--sp = (uintptr_t)args;
        *--sp = (uintptr_t)start;
        io_setup_new_fiber(new_fiber, sp);
    }
    return new_fiber;
}


moe_fiber_t *moe_get_primary_fiber() {
    moe_thread_t *current_thread = _get_current_thread();
    if (current_thread->primary_fiber == 0) {
        moe_fiber_t *new_fiber = _create_fiber(NULL, NULL, 0, current_thread->name);
        current_thread->primary_fiber = new_fiber;
        current_thread->last_fiber = new_fiber;
        current_thread->current_fiber = new_fiber;
    }
    return current_thread->primary_fiber;
}

moe_fiber_t *moe_create_fiber(moe_thread_start start, void *args, size_t stack_size, const char *name) {
    moe_thread_t *current_thread = _get_current_thread();
    // moe_fiber_t *primary_fiber = 
    moe_get_primary_fiber();
    moe_fiber_t *new_fiber =_create_fiber(start, args, stack_size, name);

    moe_fiber_t *last_fiber = current_thread->last_fiber;
    last_fiber->next_sibling = new_fiber;
    new_fiber->prev_sibling = last_fiber;
    current_thread->last_fiber = new_fiber;

    return new_fiber;
}

void fiber_on_start() {
    // __asm__ volatile ("int3");
    // moe_fiber_t *current = moe_get_current_fiber();
    // current->running = 1;
}


moe_fiber_t *moe_get_current_fiber() {
    return _get_current_thread()->current_fiber;
}

int moe_get_current_fiber_id() {
    moe_fiber_t *current = moe_get_current_fiber();
    return current ? current->fiber_id : 0;
}

const char *moe_get_current_fiber_name() {
    moe_fiber_t *current = moe_get_current_fiber();
    return current ? current->name : NULL;
}

void moe_yield() {
    if (!moe.csd) return;
    moe_thread_t *current_thread = _get_current_thread();
    moe_fiber_t *current = current_thread->current_fiber;
    if (!current) return;
    moe_fiber_t *next = current->next_sibling;
    if (!next) {
        next = current->parent_thread->primary_fiber;
    }
    if (current != next) {
        current->running = 0;
        next->running = 1;
        current_thread->current_fiber = next;
        _do_switch_context(&current->context, &next->context);
    }
}

_Noreturn void moe_exit_fiber(uint32_t exit_code) {
    moe_fiber_t *current = moe_get_current_fiber();
    current->zombie = 1;
    current->exit_code = exit_code;
    // TODO: everything
    moe_yield();
    for (;;) io_hlt();
}


_Noreturn void scheduler_thread(void *args) {
    for (;;) {
        // TODO: everything
        moe_usleep(10000000);
    }
}


void thread_init(int ncpu) {
    char name[THREAD_NAME_SIZE];

    moe.ncpu = ncpu;
    moe.system_affinity = AFFINITY(ncpu) - 1;
    moe.thread_list = moe_alloc_object(sizeof(void *), MAX_THREADS);
    moe.ready = moe_queue_create(DEFAULT_SCHEDULE_SIZE);
    moe.retired = moe_queue_create(DEFAULT_SCHEDULE_SIZE);
    moe.next_fibid = 1;

    core_specific_data_t *_csd = moe_alloc_object(sizeof(core_specific_data_t), ncpu);
    for (int i = 0; i < ncpu; i++) {
        _csd[i].cpuid = i;
        snprintf(name, THREAD_NAME_SIZE, "(Idle Core #%d)", i);
        moe_thread_t *th = _create_thread(NULL, priority_idle, NULL, name);
        th->hard_affinity = th->soft_affinity = AFFINITY(i);
        _csd[i].idle = th;
        _csd[i].current = th;
    }
    moe.csd = _csd;

    _create_thread(&scheduler_thread, priority_realtime, NULL, "scheduler");
}

int moe_get_number_of_active_cpus() {
    return moe.ncpu;
}


/*********************************************************************/
// Spinlock

int moe_spinlock_try(moe_spinlock_t *lock) {
    uintptr_t expected = 0;
    uintptr_t desired = 1;
    if (atomic_load(lock) != expected) return 0;
    return atomic_compare_exchange_weak(lock, &expected, desired);
}

int moe_spinlock_acquire(moe_spinlock_t *lock) {
    for (;;) {
        if (moe_spinlock_try(lock)) {
            return 0;
        } else {
            io_pause();
        }
    }
}

void moe_spinlock_release(moe_spinlock_t *lock) {
    atomic_store(lock, 0);
}


/*********************************************************************/
// Semaphore

typedef struct moe_semaphore_t {
    moe_thread_t * _Atomic thread;
    _Atomic intptr_t value;
} moe_semaphore_t;

void moe_sem_init(moe_semaphore_t *self, intptr_t value) {
    self->thread = NULL;
    self->value = value;
}

moe_semaphore_t *moe_sem_create(intptr_t value) {
    moe_semaphore_t *result = moe_alloc_object(sizeof(moe_semaphore_t), 1);
    if (result) {
        moe_sem_init(result, value);
    }
    return result;
}

intptr_t moe_sem_getvalue(moe_semaphore_t *self) {
    return atomic_load(&self->value);
}

int moe_sem_trywait(moe_semaphore_t *self) {
    intptr_t value = atomic_load(&self->value);
    while (value > 0) {
        if (atomic_compare_exchange_weak(&self->value, &value, value - 1)) {
            return 0;
        } else {
            io_pause();
        }
    }
    return -1;
}

int moe_sem_wait(moe_semaphore_t *self, int64_t us) {

    if (!moe_sem_trywait(self)) {
        return 0;
    }

    // if (atomic_load(&self->thread) != NULL) {
    //     int64_t timeout_us = MIN(us, 1000);
    //     moe_measure_t deadline1 = moe_create_measure(timeout_us);
    //     do {
    //         if (!moe_sem_trywait(self)) {
    //             return 0;
    //         } else {
    //             io_pause();
    //         }
    //     } while (moe_measure_until(deadline1));
    // }

    moe_measure_t deadline2 = moe_create_measure(us);
    const int64_t timeout_min = 1000;
    const int64_t timeout_max = 100000;
    int64_t timeout = timeout_min;
    moe_thread_t *current = _get_current_thread();
    do {
        moe_thread_t *expected = NULL;
        if (atomic_compare_exchange_weak(&self->thread, &expected, current)) {
            moe_wait_for_object(&self->thread, timeout);
            if (!moe_sem_trywait(self)) {
                return 0;
            }
            timeout = timeout_min;
        }
        moe_usleep(timeout);
        timeout = MIN(timeout_max, timeout * 2);
    } while (moe_measure_until(deadline2));

    return -1;
}

void moe_sem_signal(moe_semaphore_t *self) {
    moe_thread_t *thread = atomic_load(&self->thread);
    atomic_fetch_add(&self->value, 1);
    if (thread) {
        moe_signal_object(&self->thread);
        atomic_compare_exchange_weak(&self->thread, &thread, NULL);
    }
}


/*********************************************************************/
// Queue

typedef struct moe_queue_t {
    moe_semaphore_t read_sem;
    _Atomic uint32_t read, write, free;
    uint32_t mask;
} moe_queue_t;


moe_queue_t *moe_queue_create(size_t capacity) {
    moe_queue_t *self = moe_alloc_object(sizeof(moe_queue_t) + sizeof(uintptr_t) * capacity, 1);
    moe_sem_init(&self->read_sem, 0);
    self->read = 0;
    self->write = 0;
    self->free = capacity - 1;
    self->mask = capacity - 1;
    return self;
}

static _Atomic intptr_t *_queue_get_data_ptr(moe_queue_t *self) {
    return (void *)((intptr_t)self + sizeof(moe_queue_t));
}

static intptr_t queue_read_main(moe_queue_t* self) {
    uintptr_t read_ptr = atomic_fetch_add(&self->read, 1);
    intptr_t retval = atomic_load(_queue_get_data_ptr(self) + (read_ptr & self->mask));
    atomic_fetch_add(&self->free, 1);
    return retval;
}

intptr_t moe_queue_read(moe_queue_t* self, intptr_t default_val) {
    if (!moe_sem_trywait(&self->read_sem)) {
        return queue_read_main(self);
    } else {
        return default_val;
    }
}

int moe_queue_wait(moe_queue_t* self, intptr_t* result, uint64_t us) {
    if (!moe_sem_wait(&self->read_sem, us)) {
        *result = queue_read_main(self);
        return 1;
    } else {
        return 0;
    }
}

int moe_queue_write(moe_queue_t* self, intptr_t data) {
    uint32_t free = atomic_load(&self->free);
    while (free > 0) {
        if (atomic_compare_exchange_weak(&self->free, &free, free - 1)) {
            uintptr_t write_ptr = atomic_fetch_add(&self->write, 1);
            atomic_store(_queue_get_data_ptr(self) + (write_ptr & self->mask), data);
            moe_sem_signal(&self->read_sem);
            return 0;
        } else {
            io_pause();
        }
    }
    return -1;
}

size_t moe_queue_get_estimated_count(moe_queue_t* self) {
    intptr_t result = moe_sem_getvalue(&self->read_sem);
    return (result > 0) ? result : 0;
}

size_t moe_queue_get_estimated_free(moe_queue_t* self) {
    return atomic_load(&self->free);
}


/*********************************************************************/

int atomic_bit_scan(_Atomic uint64_t *p) {
    uint64_t word = atomic_load(p);
    if (word == 0) return -1;
    return __builtin_ctz(word);
}


size_t atomic_bit_scan_and_reset(_Atomic uint32_t *p, size_t limit, size_t def_val) {
    size_t words = (limit + 31) / 32;
    for (size_t i = 0; i < words; i++) {
        uint32_t word = p[i];
        while (word != 0) {
            size_t position = __builtin_ctz(word);
            uint32_t mask = 1 << position;
            uint32_t desired = word & ~mask;
            if (atomic_compare_exchange_weak(&p[i], &word, desired)) {
                return i * 32 + position;
            }
        }
    }
    return def_val;
}


/*********************************************************************/
