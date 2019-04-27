// MOE Service
// Copyright (c) 1998,2002,2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "efi.h"
#include "setjmp.h"


#define DEFAULT_QUANTUM     3
#define CONSUME_QUANTUM_THRESHOLD 2500
#define THREAD_NAME_SIZE    32
#define CLEANUP_LOAD_TIME   1000
#define DEFAULT_SCHEDULE_SIZE   1024
#define DEFAULT_SCHEDULE_QUEUES 2
#define PREEMPT_PENALTY     10000


typedef uint32_t moe_affinity_t;
typedef int thid_t;

typedef struct moe_thread_t {
    moe_thread_t* next;
    union {
        _Atomic uint32_t flags;
        struct {
            moe_priority_level_t priority;
            uint8_t last_cpuid;
        };
    };
    union {
        _Atomic uint32_t sysflag;
        struct {
            uint32_t :26;
            uint32_t fpu_allocated:1;
            uint32_t fpu_used:1;
            uint32_t :2;
            uint32_t zombie:1;
            uint32_t running:1;
        };
    };
    thid_t thid;
    _Atomic uint32_t measure0;
    _Atomic uint64_t cputime;
    _Atomic uint32_t load00, load0, load;
    moe_affinity_t affinity;
    _Atomic uint8_t quantum_left;
    uint8_t quantum, quantum_min, quantum_max;

    _Atomic (moe_thread_t **) signal_object;
    moe_timer_t block;

    void *fpu_context;
    jmp_buf jmpbuf;
    char name[THREAD_NAME_SIZE];
} moe_thread_t;

enum {
    thread_flags_running = 31,
};

extern void io_finit();
extern void io_fsave(void*);
extern void io_fload(void*);
extern void setjmp_new_thread(jmp_buf env, uintptr_t* new_sp);
extern uintptr_t moe_get_current_cpuid();
extern int io_set_lazy_fpu_restore();


typedef struct {
    int cpuid;
    moe_thread_t* idle;
    _Atomic (moe_thread_t*) current;
    _Atomic (moe_thread_t*) retired;
    // _Atomic moe_irql_t irql;
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
    uintptr_t flags = io_lock_irq();
    uintptr_t cpuid = moe_get_current_cpuid();
    moe_thread_t *result = atomic_load(&core_data[cpuid].current);
    io_unlock_irq(flags);
    return result;
}

int moe_get_current_thread() {
    return get_current_thread()->thid;
}

const char *moe_get_current_thread_name() {
    return get_current_thread()->name;
}

uint32_t moe_get_thread_load(moe_thread_t* thread) {
    return moe_get_measure() - atomic_load(&thread->measure0);
}

int sch_add(moe_thread_t *thread) {
    if (thread->priority) {
        int pri = thread->priority >= priority_high ? 0 : 1;
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
        int retval = moe_fifo_write(thread_queue.retired, (uintptr_t)thread);
        atomic_bit_test_and_clear(&thread_queue.lock, 0);
        return retval;
    } else {
        return -1;
    }
}

moe_thread_t *sch_next(uint32_t cpuid) {
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

    return core_data[cpuid].idle;
}

static void unlock_core(uint32_t cpuid) {
    atomic_bit_test_and_clear(&thread_queue.core_lock, cpuid);
}

//  Context Swicth
static void next_thread(uint32_t cpuid, moe_thread_t* current, void *obj, moe_timer_t timer) {
    if (!atomic_bit_test_and_set(&thread_queue.core_lock, cpuid)) {
        moe_thread_t* next = sch_next(cpuid);
        uint64_t load = moe_get_thread_load(current);
        MOE_ASSERT(load >= 0, "CPU LOAD EXCEED");
        atomic_fetch_add(&current->cputime, load);
        atomic_fetch_add(&current->load0, load);
        if (next != current) {
            MOE_ASSERT(!next->running, "THREAD RUNNING %d <= %d #%d\n", next->thid, current->thid, cpuid);
            core_data[cpuid].retired = current;
            if (!setjmp(current->jmpbuf)) {
                if (current->fpu_used) {
                    io_fsave(current->fpu_context);
                    current->fpu_used = 0;
                }
                moe_thread_t* expected_current = current;
                if (atomic_compare_exchange_strong(&core_data[cpuid].current, &expected_current, next)) {
                    current->block = timer;
                    current->signal_object = obj;
                    current->running = 0;
                    longjmp(next->jmpbuf, 0);
                } else {
                    MOE_ASSERT(false, "WTF? #%d %08zx(%d) != %08zx(%d)\n", cpuid, (uintptr_t)expected_current, expected_current->thid, (uintptr_t)current, current->thid);
                }
            }
            cpuid = moe_get_current_cpuid();
            current->measure0 = moe_get_measure();
            current->last_cpuid = cpuid;
            current->running = 1;
            sch_retire(core_data[cpuid].retired);
        } else {
            current->measure0 = moe_get_measure();
        }
        unlock_core(cpuid);
    }
}

void on_thread_start() {
    uint32_t cpuid = moe_get_current_cpuid();
    moe_thread_t* current = core_data[cpuid].current;
    current->measure0 = moe_get_measure();
    current->last_cpuid = cpuid;
    current->running = 1;
    sch_retire(core_data[cpuid].retired);
    unlock_core(cpuid);
}

//  Lazy FPU restore
void moe_fpu_restore(uintptr_t delta) {
    moe_thread_t* current = get_current_thread();
    if (current->fpu_context) {
        io_fload(current->fpu_context);
    } else {
        io_finit();
        current->fpu_allocated = 1;
        current->fpu_context = moe_alloc_object(delta, 1);
    }
    current->fpu_used = 1;
}


void reschedule() {
    if (!core_data) return;
    uint32_t cpuid = moe_get_current_cpuid();
    moe_thread_t* current = core_data[cpuid].current;
    if (current->priority >= priority_realtime) {
        // do nothing
    } else if (current->priority == priority_idle || (current->priority < priority_high && moe_fifo_get_estimated_count(thread_queue.ready[0]))) {
        next_thread(cpuid, current, NULL, 0);
    } else {
        uint32_t cload = moe_get_thread_load(current);
        uint32_t load = atomic_fetch_add(&current->load00, cload);
        if (load + cload > CONSUME_QUANTUM_THRESHOLD) {
            atomic_fetch_sub(&current->load00, CONSUME_QUANTUM_THRESHOLD);
            if (atomic_fetch_add(&current->quantum_left, -1) <= 0) {
                uint8_t quantum = current->quantum;
                quantum--;
                current->quantum = MAX(quantum, current->quantum_min);
                atomic_fetch_add(&current->quantum_left, current->quantum);
                next_thread(cpuid, current, NULL, moe_create_interval_timer(PREEMPT_PENALTY));
            }
        }
    }
}

void moe_yield() {
    if (core_data) {
        moe_usleep(0);
    } else {
        io_hlt();
    }
}

int moe_wait_for_object(moe_thread_t **obj, uint64_t us) {
    uintptr_t flags = io_lock_irq();
    uint32_t cpuid = moe_get_current_cpuid();
    moe_thread_t* current = core_data[cpuid].current;
    if (obj) {
        *obj = current;
    }
    moe_timer_t timer = moe_create_interval_timer(us);
    next_thread(cpuid, current, obj, timer);
    io_unlock_irq(flags);
    return 0;
}

int moe_usleep(uint64_t us) {
    if (core_data) {
        return moe_wait_for_object(NULL, us);
    } else {
        moe_timer_t timer = moe_create_interval_timer(us);
        while (moe_check_timer(&timer)) {
            io_hlt();
        }
        return 0;
    }
}

int moe_signal_object(moe_thread_t **obj) {
    moe_thread_t *thread = *obj;
    moe_thread_t **expected = obj;
    if (atomic_compare_exchange_strong(&thread->signal_object, &expected, NULL)) {
        thread->block = 0;
        return 0;
    } else {
        return -1;
    }
}


_Noreturn void moe_exit_thread(uint32_t exit_code) {
    moe_thread_t* current = get_current_thread();
    current->zombie = 1;
    moe_yield();
    for (;;) io_hlt();
}

moe_thread_t* create_thread(moe_thread_start start, moe_priority_level_t priority, void* args, const char* name) {

    moe_thread_t* new_thread = moe_alloc_object(sizeof(moe_thread_t), 1);
    // memset(new_thread, 0, sizeof(moe_thread_t));
    new_thread->thid = atomic_fetch_add(&next_thid, 1);
    new_thread->priority = priority;
    if (priority) {
        new_thread->quantum_min = priority;
        new_thread->quantum_max = DEFAULT_QUANTUM * priority * priority;
        new_thread->quantum = new_thread->quantum_max;
        new_thread->quantum_left = new_thread->quantum_max;
    }
    new_thread->affinity = system_affinity;
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
    int last_timer = 0;
    for (;;) {
        int timer = moe_get_tick_count();
        if ((timer - last_timer) >= CLEANUP_LOAD_TIME) {

            moe_thread_t* thread = root_thread;
            for (; thread; thread = thread->next) {
                int load = atomic_load(&thread->load0);
                atomic_store(&thread->load, load);
                atomic_fetch_add(&thread->load0, -load);
                if (thread->priority) {
                    uint8_t quantum = thread->quantum;
                    quantum++;
                    thread->quantum = MIN(quantum, thread->quantum_max);
                }
            }

            last_timer = timer;
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
    core_specific_data_t* _core_data = moe_alloc_object(sizeof(core_specific_data_t), n_active_cpu);

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

    io_set_lazy_fpu_restore();

    moe_thread_t *sc = create_thread(&scheduler_thread, priority_high, 0, "Scheduler");
    sc->affinity = 1;
}


/*********************************************************************/
// Semaphore

typedef struct moe_semaphore_t {
    _Atomic (moe_thread_t *) waiting;
    _Atomic intptr_t value;
} moe_semaphore_t;

moe_semaphore_t *moe_sem_create(intptr_t value) {
    moe_semaphore_t *result = moe_alloc_object(sizeof(moe_semaphore_t), 1);
    moe_sem_init(result, value);
    return result;
}

void moe_sem_init(moe_semaphore_t *self, intptr_t value) {
    self->value = value;
    self->waiting = NULL;
}

intptr_t moe_sem_getvalue(moe_semaphore_t *self) {
    intptr_t result = self->value;
    if (result > 0) {
        return result;
    } else {
        if (self->waiting) result--;
        return result;
    }
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

int moe_sem_wait(moe_semaphore_t *self, uint64_t us) {
    if (moe_sem_trywait(self)) {
        return 1;
    }
    const uint64_t div_max = 100000;
    uint64_t time_div = 1000;
    moe_thread_t* desired = get_current_thread();
    while (us > 0) {
        moe_thread_t* expected = NULL;
        if (atomic_compare_exchange_weak(&self->waiting, &expected, desired)) {
            if (moe_sem_trywait(self)) {
                atomic_compare_exchange_weak(&self->waiting, &desired, NULL);
                return 1;
            }
            moe_wait_for_object(&self->waiting, us);
            atomic_compare_exchange_weak(&self->waiting, &desired, NULL);
            return moe_sem_trywait(self);
        } else {
            // MOE_ASSERT(false, "DOUBLE WAIT %08x %08x\n", expected, desired);
            if (us > time_div) {
                moe_usleep(time_div);
                us -= time_div;
            } else {
                moe_usleep(us);
                us = 0;
            }
            time_div *= 2;
            if (time_div > div_max) {
                time_div = div_max;
            }
        }
    }
    return 0;
}

void moe_sem_signal(moe_semaphore_t *self) {
    atomic_fetch_add(&self->value, 1);

    if (self->waiting) {
        moe_signal_object(&self->waiting);
    }

}


/*********************************************************************/
//  First In, First Out method

typedef struct moe_fifo_t {
    moe_semaphore_t read_sem;
    _Atomic intptr_t *data;
    _Atomic uint32_t read, write, free;
    uint32_t mask;
} moe_fifo_t;


moe_fifo_t *moe_fifo_init(size_t capacity) {
    moe_fifo_t *self = moe_alloc_object(sizeof(moe_fifo_t), 1);
    moe_sem_init(&self->read_sem, 0);
    self->data = moe_alloc_object(sizeof(uintptr_t), capacity);
    self->read = 0;
    self->write = 0;
    self->free = capacity - 1;
    self->mask = capacity - 1;
    return self;
}

intptr_t fifo_read_main(moe_fifo_t* self) {
    uintptr_t read_ptr = atomic_fetch_add_explicit(&self->read, 1, memory_order_seq_cst);
    intptr_t retval = atomic_load_explicit(&self->data[read_ptr & self->mask], memory_order_seq_cst);
    atomic_fetch_add_explicit(&self->free, 1, memory_order_seq_cst);
    return retval;
}

intptr_t moe_fifo_read(moe_fifo_t* self, intptr_t default_val) {
    if (moe_sem_trywait(&self->read_sem)) {
        return fifo_read_main(self);
    } else {
        return default_val;
    }
}

int moe_fifo_wait(moe_fifo_t* self, intptr_t* result, uint64_t us) {
    if (moe_sem_wait(&self->read_sem, us)) {
        *result = fifo_read_main(self);
        return 1;
    } else {
        return 0;
    }
}

int moe_fifo_write(moe_fifo_t* self, intptr_t data) {
    uint32_t free = atomic_load(&self->free);
    while (free > 0) {
        if (atomic_compare_exchange_weak(&self->free, &free, free - 1)) {
            uintptr_t write_ptr = atomic_fetch_add_explicit(&self->write, 1, memory_order_seq_cst);
            atomic_store_explicit(&self->data[write_ptr & self->mask], data, memory_order_seq_cst);
            moe_sem_signal(&self->read_sem);
            return 0;
        } else {
            io_pause();
        }
    }
    return -1;
}

size_t moe_fifo_get_estimated_count(moe_fifo_t* self) {
    intptr_t result = moe_sem_getvalue(&self->read_sem);
    return (result > 0) ? result : 0;
}

size_t moe_fifo_get_estimated_free(moe_fifo_t* self) {
    return atomic_load(&self->free);
}


/*********************************************************************/

int cmd_ps(int argc, char **argv) {
    moe_thread_t* p = root_thread;
    printf("ID context      attr     usage cpu time   name\n");
    for (; p; p = p->next) {
        uint64_t time = p->cputime / 1000000;
        uint32_t time0 = time % 60;
        uint32_t time1 = (time / 60) % 60;
        uint32_t time2 = (time / 3600);
        int usage = p->load / 1000;
        if (usage > 999) usage = 999;
        int usage0 = usage % 10, usage1 = usage / 10;
        printf("%2d %012zx %08x %2d.%d%% %4u:%02u:%02u %s\n",
            (int)p->thid, (uintptr_t)p & 0xFFFFFFFFFFFF, p->flags,
            // p->affinity, p->quantum_left, p->quantum,
            usage1, usage0, time2, time1, time0,
            p->name);
    }
    return 0;
}


int cmd_top(int argc, char **argv) {

    const size_t buff_size = 1024;
    char buff[buff_size];

    const uint32_t bgcolor = 0x80000000;
    const uint32_t fgcolor = 0xFFFFFF00;

    moe_rect_t frame = {{-1, -1}, {384, 256}};

    moe_window_t *window = moe_create_window(&frame, MOE_WS_TRANSPARENT | MOE_WS_CAPTION, window_level_higher, "Task Manager");
    moe_set_window_bgcolor(window, bgcolor);
    moe_show_window(window);

    uintptr_t event;
    while ((event = moe_get_event(window, 1000000))) {
        moe_font_t *font = moe_get_system_font(1);
        moe_rect_t rect = moe_get_client_rect(window);
        moe_point_t cursor = *moe_point_zero;
        moe_set_window_bgcolor(window, bgcolor);
        cursor = moe_draw_string(moe_get_window_bitmap(window), font, &cursor, &rect,
        "ID context      attr     quan  usage cpu time   name\n", fgcolor);

        moe_thread_t* p = root_thread;
        for (; p; p = p->next) {
            uint64_t time = p->cputime / 1000000;
            uint32_t time0 = time % 60;
            uint32_t time1 = (time / 60) % 60;
            uint32_t time2 = (time / 3600);
            int usage = p->load / 1000;
            if (usage > 999) usage = 999;
            int usage0 = usage % 10, usage1 = usage / 10;
            snprintf(buff, buff_size, "%2d %012zx %08x %2d/%2d %2d.%d%% %4u:%02u:%02u %s\n",
                (int)p->thid, (uintptr_t)p & 0xFFFFFFFFFFFF, p->flags | p->sysflag,
                // (uintptr_t)p->signal_object,
                p->quantum_left, p->quantum,
                usage1, usage0, time2, time1, time0,
                p->name);
            cursor = moe_draw_string(moe_get_window_bitmap(window), font, &cursor, &rect, buff, fgcolor);
        }
        moe_invalidate_rect(window, &rect);
    }
    return 0;
}
