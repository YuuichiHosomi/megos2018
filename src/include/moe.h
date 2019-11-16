// Minimal Operating Environment
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: MIT
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>


#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#define MIN(a, b)   ((a) < (b) ? (a) : (b))

int printf(const char *format, ...);
int vprintf(const char *format, va_list args);
int snprintf(char* buffer, size_t n, const char* format, ...);
char *strchr(const char *s, int c);
char *strncpy(char *s1, const char *s2, size_t n);
int strncmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char *s);
void *memcpy(void *p, const void *q, size_t n);
void *memset(void *p, int v, size_t n);

_Noreturn void _zpanic(const char *file, uintptr_t line, ...);
#define moe_assert(cond, ...) if (!(cond)) { _zpanic(__FILE__, __LINE__, __VA_ARGS__); }
#define moe_panic(...) _zpanic(__FILE__, __LINE__, __VA_ARGS__)
_Noreturn void moe_reboot(void);
_Noreturn void moe_shutdown_system(void);


//  Minimal Graphics Subsystem
typedef struct moe_point_t {
    int x, y;
} moe_point_t;

typedef struct moe_size_t {
    int width, height;
} moe_size_t;

typedef struct moe_rect_t {
    moe_point_t origin;
    moe_size_t size;
} moe_rect_t;

typedef struct moe_edge_insets_t {
    int top, left, bottom, right;
} moe_edge_insets_t;

typedef struct moe_bitmap_t {
    uint32_t *bitmap;
    int width, height;
    uint32_t flags, delta, color_key;
} moe_bitmap_t;

#define MOE_BMP_ALPHA       0x0001
#define MOE_BMP_ROTATE      0x0002

// extern const moe_point_t *moe_point_zero;
// extern const moe_size_t *moe_size_zero;
// extern const moe_rect_t *moe_rect_zero;
// extern const moe_edge_insets_t *moe_edge_insets_zero;

#define MOE_COLOR_TRANSPARENT   0x00000000

moe_bitmap_t *moe_create_bitmap(moe_size_t *size, uint32_t flags, uint32_t color);
void moe_blt(moe_bitmap_t *dest, moe_bitmap_t *src, moe_point_t *origin, moe_rect_t *rect, uint32_t options);
void moe_fill_rect(moe_bitmap_t *dest, moe_rect_t *rect, uint32_t color);

int moe_set_console_cursor_visible(void *context, int visible);


//  Minimal Memory Subsystem
void *moe_alloc_object(size_t size, size_t count);

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define PROT_NONE   0x0

typedef struct {
    void *context;
    _Atomic int ref_cnt;
} moe_shared_t;
typedef void (*MOE_DEALLOC)(void *self);

static inline moe_shared_t *moe_shared_init(moe_shared_t *self, void *context) {
    if (self) {
        self->ref_cnt = 1;
        self->context = context;
    }
    return self;
}
moe_shared_t *moe_retain(moe_shared_t *self);
void moe_release(moe_shared_t *self, MOE_DEALLOC dealloc);


//  Threading Service
typedef struct moe_thread_t moe_thread_t;
typedef enum {
    priority_idle = 0,
    priority_low,
    priority_normal,
    priority_high,
    priority_realtime,
    priority_max,
} moe_priority_level_t;

typedef void (*moe_thread_start)(void *args);
int moe_create_thread(moe_thread_start start, moe_priority_level_t priority, void *args, const char *name);
int moe_usleep(int64_t us);
int moe_get_current_thread_id(void);
const char *moe_get_current_thread_name(void);
_Noreturn void moe_exit_thread(uint32_t exit_code);
int moe_get_number_of_active_cpus(void);

int moe_get_pid(void);
int moe_raise_pid(void);
int moe_create_process(moe_thread_start start, moe_priority_level_t priority, void *args, const char *name);


typedef _Atomic uintptr_t moe_spinlock_t;
int moe_spinlock_try(moe_spinlock_t *lock);
int moe_spinlock_acquire(moe_spinlock_t *lock);
void moe_spinlock_release(moe_spinlock_t *lock);


typedef struct moe_semaphore_t {
    _Atomic (moe_thread_t *) thread;
    _Atomic intptr_t value;
} moe_semaphore_t;

static inline moe_semaphore_t *moe_sem_init(moe_semaphore_t *self, intptr_t value) {
    self->thread = NULL;
    self->value = value;
    return self;
}

moe_semaphore_t *moe_sem_create(intptr_t value);
int moe_sem_trywait(moe_semaphore_t *self);
int moe_sem_wait(moe_semaphore_t *self, int64_t us);
void moe_sem_signal(moe_semaphore_t *self);

static inline intptr_t moe_sem_getvalue(moe_semaphore_t *self) {
    return self->value;
}


#define MOE_FOREVER INT64_MAX
typedef uint64_t moe_measure_t;
moe_measure_t moe_create_measure(int64_t);
int moe_measure_until(moe_measure_t);
int64_t moe_measure_diff(moe_measure_t from);

typedef struct moe_queue_t moe_queue_t;
moe_queue_t *moe_queue_create(size_t capacity);
intptr_t moe_queue_read(moe_queue_t *self, intptr_t default_val);
int moe_queue_wait(moe_queue_t* self, intptr_t* result, uint64_t us);
int moe_queue_write(moe_queue_t *self, intptr_t data);
size_t moe_queue_get_estimated_count(moe_queue_t *self);
size_t moe_queue_get_estimated_free(moe_queue_t *self);

