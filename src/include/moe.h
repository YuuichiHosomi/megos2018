// Minimal Operating Environment
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: MIT
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#define MIN(a, b)   ((a) < (b) ? (a) : (b))

int printf(const char *format, ...);
int snprintf(char* buffer, size_t n, const char* format, ...);
char *strncpy(char *s1, const char *s2, size_t n);
void *memcpy(void *p, const void *q, size_t n);
void *memset(void *p, int v, size_t n);
void memset32(uint32_t *p, uint32_t v, size_t n);

_Noreturn void _panic(const char *file, uintptr_t line, ...);
#define moe_assert(cond, ...) if (!(cond)) { _panic(__FILE__, __LINE__, __VA_ARGS__); }
#define moe_panic(...) _panic(__FILE__, __LINE__, __VA_ARGS__)
_Noreturn void moe_reboot();
_Noreturn void moe_shutdown_system();


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


//  Minimal Memory Subsystem
void *moe_alloc_object(size_t size, size_t count);

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define PROT_NONE   0x0


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
int moe_get_current_thread_id();
const char *moe_get_current_thread_name();
_Noreturn void moe_exit_thread(uint32_t exit_code);
int moe_get_number_of_active_cpus();

typedef _Atomic uintptr_t moe_spinlock_t;
int moe_spinlock_try(moe_spinlock_t *lock);
int moe_spinlock_acquire(moe_spinlock_t *lock);
void moe_spinlock_release(moe_spinlock_t *lock);

typedef struct moe_semaphore_t moe_semaphore_t;
moe_semaphore_t *moe_sem_create(intptr_t value);
// void moe_sem_init(moe_semaphore_t *self, intptr_t value);
int moe_sem_trywait(moe_semaphore_t *self);
int moe_sem_wait(moe_semaphore_t *self, int64_t us);
void moe_sem_signal(moe_semaphore_t *self);
intptr_t moe_sem_getvalue(moe_semaphore_t *self);

typedef uint64_t moe_measure_t;
moe_measure_t moe_create_measure(int64_t);
int moe_measure_until(moe_measure_t);

typedef struct moe_queue_t moe_queue_t;
moe_queue_t *moe_queue_create(size_t capacity);
intptr_t moe_queue_read(moe_queue_t *self, intptr_t default_val);
int moe_queue_wait(moe_queue_t* self, intptr_t* result, uint64_t us);
int moe_queue_write(moe_queue_t *self, intptr_t data);
size_t moe_queue_get_estimated_count(moe_queue_t *self);
size_t moe_queue_get_estimated_free(moe_queue_t *self);

