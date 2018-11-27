// Minimal Operating Environment
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: BSD
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


int printf(const char *format, ...);
void* memcpy(void *p, const void *q, size_t n);
void* memset(void *p, int v, size_t n);
void memset32(uint32_t *p, uint32_t v, size_t n);
int atomic_bit_test_and_set(void *p, uintptr_t bit);
int atomic_bit_test_and_clear(void *p, uintptr_t bit);
int atomic_bit_test(void *p, uintptr_t bit);


void moe_assert(const char *file, uintptr_t line, ...);
#define MOE_ASSERT(cond, ...) if (!(cond)) { moe_assert(__FILE__, __LINE__, __VA_ARGS__); }


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

typedef struct moe_dib_t {
    uint32_t* dib;
    int width, height;
    uint32_t flags, delta, color_key;
} moe_dib_t;

typedef struct moe_view_t moe_view_t;
typedef struct moe_console_context_t moe_console_context_t;

#define MOE_DIB_COLOR_KEY   0x0001
#define MOE_DIB_ROTATE      0x0002
#define MOE_DIB_ALPHA       0x0004

extern const moe_point_t *moe_point_zero;
extern const moe_size_t *moe_size_zero;
extern const moe_rect_t *moe_rect_zero;
extern const moe_edge_insets_t *moe_edge_insets_zero;

#define COLOR_TRANSPARENT   0xFF000000

moe_dib_t *moe_create_dib(moe_size_t *size, uint32_t flags, uint32_t color);
void moe_blt(moe_dib_t *dest, moe_dib_t *src, moe_point_t *origin, moe_rect_t *rect, uint32_t options);
void moe_fill_rect(moe_dib_t *dest, moe_rect_t *rect, uint32_t color);
void moe_fill_round_rect(moe_dib_t *dest, moe_rect_t *rect, int radius, uint32_t color);
void moe_draw_round_rect(moe_dib_t *dest, moe_rect_t *rect, int radius, uint32_t color);
void moe_draw_pixel(moe_dib_t *dest, moe_point_t *point, uint32_t color);
moe_rect_t moe_edge_insets_inset_rect(moe_rect_t *rect, moe_edge_insets_t *insets);
moe_point_t moe_draw_string(moe_dib_t *dib, moe_point_t *cursor, moe_rect_t *rect, const char *s, uint32_t color);

void moe_set_console_attributes(moe_console_context_t *self, uint32_t attributes);
int moe_set_console_cursor_visible(moe_console_context_t *self, int visible);

void mgs_cls();

typedef enum {
    window_level_desktop,
    window_level_desktop_items,
    window_level_normal = 32,
    window_level_higher = 64,
    window_level_popup = 96,
    window_level_pointer = 127,
} moe_window_level_t;

#define WINDOW_BORDER       0x0100
#define WINDOW_CAPTION      0x0200
#define WINDOW_CENTER       0x0400
#define WINDOW_TRANSPARENT  0x0800

moe_size_t moe_get_screen_size();
moe_view_t *moe_create_view(moe_rect_t *frame, moe_dib_t* dib, uint32_t flags, const char *title);
moe_edge_insets_t moe_get_client_insets(moe_view_t *view);
moe_rect_t moe_get_client_rect(moe_view_t *view);
void moe_show_window(moe_view_t* view);
void moe_hide_window(moe_view_t *view);
void moe_invalidate_view(moe_view_t *view, moe_rect_t *rect);
void moe_invalidate_screen(moe_rect_t *rect);
int moe_alert(const char *title, const char *message, uint32_t flags);


//  Minimal Memory Subsystem
void* mm_alloc_static_page(size_t n);
void* mm_alloc_static(size_t n);


//  Threading Service
typedef uint8_t moe_priority_level_t;
typedef enum {
    priority_idle = 0,
    priority_lowest,
    priority_lower,
    priority_normal,
    priority_higher,
    priority_highest,
    priority_realtime,
    priority_max,
} moe_priority_type_t;

typedef void (*moe_thread_start)(void* args);
int moe_create_thread(moe_thread_start start, moe_priority_level_t priority, void* args, const char* name);
void moe_yield();
void moe_consume_quantum();
int moe_usleep(uint64_t us);
int moe_get_current_thread();
int moe_get_usage();
_Noreturn void moe_exit_thread(uint32_t exit_code);

typedef uint64_t moe_timer_t;
typedef double moe_time_interval_t;
moe_timer_t moe_create_interval_timer(uint64_t);
int moe_wait_for_timer(moe_timer_t*);
int moe_check_timer(moe_timer_t*);
uint64_t moe_get_measure();

typedef struct moe_fifo_t moe_fifo_t;
moe_fifo_t* moe_fifo_init(uintptr_t capacity);
intptr_t moe_fifo_read(moe_fifo_t* self, intptr_t default_val);
int moe_fifo_write(moe_fifo_t* self, intptr_t data);
uintptr_t moe_fifo_get_estimated_count(moe_fifo_t* self);
uintptr_t moe_fifo_get_estimated_free(moe_fifo_t* self);
