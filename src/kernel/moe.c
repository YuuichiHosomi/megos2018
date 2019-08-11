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

typedef uint32_t moe_affinity_t;
typedef int thid_t;

typedef struct moe_thread_t {
    union {
        uint8_t context_save_area[1024];
    };

    union {
        uintptr_t flags;
        struct {
            unsigned zombie:1;
            unsigned running:1;
        };
    };
    thid_t thid;
    int exit_code;
    moe_priority_level_t priority;
    moe_affinity_t soft_affinity, hard_affinity;
    _Atomic uint8_t quantum_left;
    uint8_t quantum, quantum_min, quantum_max;

    _Atomic (moe_thread_t *) *signal_object;
    moe_measure_t deadline;

    char name[THREAD_NAME_SIZE];
} moe_thread_t;

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
    _Atomic thid_t next_thid;
    moe_affinity_t system_affinity;
    int n_active_cpu;
    atomic_flag lock;
} moe;

extern moe_thread_t *io_do_context_switch(moe_thread_t *from, moe_thread_t *to);
extern void io_setup_new_thread(moe_thread_t *thread, uintptr_t* new_sp);


/*********************************************************************/

static moe_affinity_t AFFINITY(int n) {
    if (sizeof(moe_affinity_t) * 8 > n) {
        return (1ULL << n);
    } else {
        return 0;
    }
}

static core_specific_data_t *_get_current_csd() {
    return &moe.csd[moe_get_current_cpuid()];
}

static moe_thread_t *_get_current_thread() {
    uintptr_t flags = io_lock_irq();
    core_specific_data_t *csd = _get_current_csd();
    moe_thread_t *current = csd->current;
    io_unlock_irq(flags);
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
    if (thread->zombie) {
        return 0;
    }
    if (thread->priority) {
        while (atomic_bit_test_and_set(&moe.lock, 0)) {
            io_pause();
        }
        int retval = moe_queue_write(moe.retired, (intptr_t)thread);
        atomic_bit_test_and_clear(&moe.lock, 0);
        return retval;
    } else {
        return -1;
    }
}

static moe_thread_t *sch_next() {
    moe_thread_t *result;
    result = (moe_thread_t*)moe_queue_read(moe.ready, 0);
    if (result) {
        if (moe_measure_until(result->deadline)) {
            sch_retire(result);
        } else {
            return result;
        }
    }

    //  Wait queue is empty
    if (!atomic_bit_test_and_set(&moe.lock, 0)) {
        moe_thread_t *p;
        while ((p = (moe_thread_t*)moe_queue_read(moe.retired, 0))) {
            sch_add(p);
        }
        atomic_bit_test_and_clear(&moe.lock, 0);
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
        // moe_thread_t *retired = 
        io_do_context_switch(current, next);
        csd = _get_current_csd();
        sch_retire(csd->retired);
    }
}

void thread_on_start() {
    core_specific_data_t *csd = _get_current_csd();
    moe_thread_t *current = csd->current;
    current->running = 1;
    sch_retire(csd->retired);
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
    io_unlock_irq(flags);
    return 0;
}


int moe_signal_object(moe_thread_t **obj) {
    moe_thread_t *thread = *obj;

    return 0;
}


static moe_thread_t *_create_thread(moe_thread_start start, moe_priority_level_t priority, void *args, const char *name) {
    moe_thread_t* new_thread = moe_alloc_object(sizeof(moe_thread_t), 1);
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
    moe_usleep(0);
    for (;;) io_hlt();
}

void thread_reschedule() {
    if (!moe.csd) return;
    core_specific_data_t *csd = _get_current_csd();
    moe_thread_t *current = csd->current;
    if (current->priority >= priority_realtime) {
        // do nothing
    } else {
        _next_thread(csd, current, NULL, 0);
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


void thread_init(int n_cpu) {
    char name[THREAD_NAME_SIZE];

    moe.n_active_cpu = n_cpu;
    moe.system_affinity = AFFINITY(n_cpu) - 1;
    moe.thread_list = moe_alloc_object(sizeof(void *), MAX_THREADS);
    moe.ready = moe_queue_create(DEFAULT_SCHEDULE_SIZE);
    moe.retired = moe_queue_create(DEFAULT_SCHEDULE_SIZE);

    core_specific_data_t *_csd = moe_alloc_object(sizeof(core_specific_data_t), n_cpu);
    for (int i = 0; i < n_cpu; i++) {
        _csd[i].cpuid = i;
        snprintf(name, THREAD_NAME_SIZE, "(Idle Core #%d)", i);
        moe_thread_t *th = _create_thread(NULL, priority_idle, NULL, name);
        th->hard_affinity = th->soft_affinity = AFFINITY(i);
        _csd[i].idle = th;
        _csd[i].current = th;
    }
    moe.csd = _csd;

}

int moe_get_number_of_active_cpus() {
    return moe.n_active_cpu;
}



/*********************************************************************/
// Spinlock

int moe_spinlock_try(moe_spinlock_t *lock) {
    uintptr_t expected = 0;
    uintptr_t desired = 1;
    if (atomic_load(lock) != expected) return 0;
    return atomic_compare_exchange_weak(lock, &expected, desired);
}

int moe_spinlock_acquire(moe_spinlock_t *lock, uintptr_t ms) {
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
    moe_semaphore_t *result = NULL;
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
            return 1;
        } else {
            io_pause();
        }
    }
    return 0;
}

int moe_sem_wait(moe_semaphore_t *self, int64_t us) {
    if (moe_sem_trywait(self)) {
        return 1;
    }
    MOE_ASSERT(false, "not implemented moe_sem_wait");
    return 0;
}

void moe_sem_signal(moe_semaphore_t *self) {
    atomic_fetch_add(&self->value, 1);
}



/*********************************************************************/
// Concurrent Queue

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

static _Atomic intptr_t *fifo_get_data_ptr(moe_queue_t *self) {
    return (void *)((intptr_t)self + sizeof(moe_queue_t));
}

intptr_t fifo_read_main(moe_queue_t* self) {
    uintptr_t read_ptr = atomic_fetch_add(&self->read, 1);
    intptr_t retval = atomic_load(fifo_get_data_ptr(self) + (read_ptr & self->mask));
    atomic_fetch_add(&self->free, 1);
    return retval;
}

intptr_t moe_queue_read(moe_queue_t* self, intptr_t default_val) {
    if (moe_sem_trywait(&self->read_sem)) {
        return fifo_read_main(self);
    } else {
        return default_val;
    }
}

int moe_queue_wait(moe_queue_t* self, intptr_t* result, uint64_t us) {
    if (moe_sem_wait(&self->read_sem, us)) {
        *result = fifo_read_main(self);
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
            atomic_store(fifo_get_data_ptr(self) + (write_ptr & self->mask), data);
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


