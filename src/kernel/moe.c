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
#define DEFAULT_SCHEDULE_SIZE   1024
#define DEFAULT_SCHEDULE_QUEUES 2
#define MAX_THREADS 512


typedef uint32_t moe_affinity_t;
typedef int thid_t;

typedef struct moe_thread_t {
    thid_t thid;
    moe_priority_level_t priority;
    moe_affinity_t soft_affinity, hard_affinity;
    _Atomic uint8_t quantum_left;
    uint8_t quantum, quantum_min, quantum_max;
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
    _Atomic thid_t next_thid;
    moe_affinity_t system_affinity;
    int n_active_cpu;
} moe;


/*********************************************************************/
// Synchronization Objects Manager

// Spinlock
int moe_spinlock_try(moe_spinlock_t lock) {
    uintptr_t expected = 0;
    uintptr_t desired = 1;
    return atomic_compare_exchange_weak(lock, &expected, desired);
}

int moe_spinlock_acquire(moe_spinlock_t lock, uintptr_t ms) {
    for (;;) {
        if (moe_spinlock_try(lock)) {
            return 0;
        } else {
            io_pause();
        }
    }
}

void moe_spinlock_release(moe_spinlock_t lock) {
    atomic_store(lock, 0);
}


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

int moe_sem_trywait(moe_semaphore_t *self) {
    return 0;
}

int moe_sem_wait(moe_semaphore_t *self, int64_t us) {
    return 0;
}

void moe_sem_signal(moe_semaphore_t *self) {

}

intptr_t moe_sem_getvalue(moe_semaphore_t *self) {
    return 0;
}


/*********************************************************************/

static moe_affinity_t AFFINITY(int n) {
    return (1ULL << n);
}

static moe_thread_t *_get_current_thread() {
    return NULL;
}

static moe_thread_t *sch_next_thread() {
    return NULL;
}

// Do switch context
static void _next_thread() {
    uintptr_t flags = io_lock_irq();


    io_unlock_irq(flags);
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
        const uintptr_t stack_size = stack_count * sizeof(uintptr_t);
        uintptr_t* stack = moe_alloc_object(stack_size, 1);
        uintptr_t* sp = stack + stack_count;
        *--sp = 0;
        *--sp = 0x00007fffdeadbeef;
        *--sp = (uintptr_t)args;
        *--sp = (uintptr_t)start;
        // setjmp_new_thread(new_thread->jmpbuf, sp);
    }

    for (int i = 0; i< MAX_THREADS; i++) {
        moe_thread_t *expected = NULL;
        if (atomic_compare_exchange_weak(&moe.thread_list[i], &expected, new_thread))
            break;
    }

    // sch_add(new_thread);

    return new_thread;
}

_Noreturn void moe_exit_thread(uint32_t exit_code) {
    for (;;) io_hlt();
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

int moe_usleep(uint64_t us) {
    // TODO:
    moe_measure_t deadline = moe_create_measure(us);
    while (moe_measure_until(deadline)) {
        io_hlt();
    }
    return 0;
}


void thread_init(int n_cpu) {
    char name[THREAD_NAME_SIZE];

    moe.n_active_cpu = n_cpu;
    moe.system_affinity = AFFINITY(n_cpu) - 1;
    moe.thread_list = moe_alloc_object(sizeof(void *), MAX_THREADS);

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


typedef struct moe_queue_t {
    void *raw_data;
    size_t data_size, capacity;
    _Atomic size_t free;
} moe_queue_t;


/*********************************************************************/


