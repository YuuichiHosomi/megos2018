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
void *memcpy(void *p, const void *q, size_t n);
void *memset(void *p, int v, size_t n);
void memset32(uint32_t *p, uint32_t v, size_t n);
char *strncpy(char *s1, const char *s2, size_t n);
int snprintf(char* buffer, size_t n, const char* format, ...);
int atomic_bit_test_and_set(void *p, uintptr_t bit);
int atomic_bit_test_and_clear(void *p, uintptr_t bit);
int atomic_bit_test(void *p, uintptr_t bit);


const char *moe_kname();

void moe_assert(const char *file, uintptr_t line, ...);
#define MOE_ASSERT(cond, ...) if (!(cond)) { moe_assert(__FILE__, __LINE__, __VA_ARGS__); }
void moe_reboot();
void moe_shutdown_system();


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

typedef struct moe_console_context_t moe_console_context_t;
typedef struct moe_font_t moe_font_t;
typedef struct moe_hid_kbd_report_t moe_hid_kbd_report_t;

#define MOE_BMP_ALPHA       0x0001
#define MOE_BMP_ROTATE      0x0002

extern const moe_point_t *moe_point_zero;
extern const moe_size_t *moe_size_zero;
extern const moe_rect_t *moe_rect_zero;
extern const moe_edge_insets_t *moe_edge_insets_zero;

#define MOE_COLOR_TRANSPARENT   0x00000000

moe_bitmap_t *moe_create_dib(moe_size_t *size, uint32_t flags, uint32_t color);
void moe_blt(moe_bitmap_t *dest, moe_bitmap_t *src, moe_point_t *origin, moe_rect_t *rect, uint32_t options);
void moe_fill_rect(moe_bitmap_t *dest, moe_rect_t *rect, uint32_t color);
void moe_blend_rect(moe_bitmap_t *dest, moe_rect_t *rect, uint32_t color);
void moe_fill_round_rect(moe_bitmap_t *dest, moe_rect_t *rect, intptr_t radius, uint32_t color);
void moe_draw_round_rect(moe_bitmap_t *dest, moe_rect_t *rect, intptr_t radius, uint32_t color);
void moe_draw_pixel(moe_bitmap_t *dest, intptr_t x, intptr_t y, uint32_t color);
void moe_draw_multi_pixels(moe_bitmap_t *dest, size_t count, moe_point_t *points, uint32_t color);
moe_rect_t moe_edge_insets_inset_rect(moe_rect_t *rect, moe_edge_insets_t *insets);
moe_point_t moe_draw_string(moe_bitmap_t *dib, moe_font_t *font, moe_point_t *cursor, moe_rect_t *rect, const char *s, uint32_t color);
moe_font_t *moe_get_system_font(int type);

void moe_set_console_attributes(moe_console_context_t *self, uint32_t attributes);
int moe_set_console_cursor_visible(moe_console_context_t *self, int visible);


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
int moe_usleep(uint64_t us);
int moe_get_current_thread_id();
const char *moe_get_current_thread_name();
// int moe_get_usage();
_Noreturn void moe_exit_thread(uint32_t exit_code);
int moe_get_number_of_active_cpus();

typedef _Atomic uintptr_t * moe_spinlock_t;
int moe_spinlock_try(moe_spinlock_t lock);
int moe_spinlock_acquire(moe_spinlock_t lock, uintptr_t ms);
void moe_spinlock_release(moe_spinlock_t lock);

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

typedef struct moe_fifo_t moe_fifo_t;
moe_fifo_t *moe_fifo_init(size_t capacity);
intptr_t moe_fifo_read(moe_fifo_t *self, intptr_t default_val);
int moe_fifo_wait(moe_fifo_t* self, intptr_t* result, uint64_t us);
int moe_fifo_write(moe_fifo_t *self, intptr_t data);
size_t moe_fifo_get_estimated_count(moe_fifo_t *self);
size_t moe_fifo_get_estimated_free(moe_fifo_t *self);

