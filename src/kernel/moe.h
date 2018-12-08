// Minimal Operating Environment
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: BSD
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define MAX(a, b)   ((a > b) ? (a) : (b))
#define MIN(a, b)   ((a < b) ? (a) : (b))

int printf(const char *format, ...);
void *memcpy(void *p, const void *q, size_t n);
void *memset(void *p, int v, size_t n);
void memset32(uint32_t *p, uint32_t v, size_t n);
char *strncpy(char *s1, const char *s2, size_t n);
int snprintf(char* buffer, size_t n, const char* format, ...);
int atomic_bit_test_and_set(void *p, uintptr_t bit);
int atomic_bit_test_and_clear(void *p, uintptr_t bit);
int atomic_bit_test(void *p, uintptr_t bit);


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

typedef struct moe_dib_t {
    uint32_t *dib;
    int width, height;
    uint32_t flags, delta, color_key;
} moe_dib_t;

typedef struct moe_window_t moe_window_t;
typedef struct moe_console_context_t moe_console_context_t;
typedef struct moe_font_t moe_font_t;

#define MOE_DIB_ALPHA       0x0001
#define MOE_DIB_COLOR_KEY   0x0100
#define MOE_DIB_ROTATE      0x0200
#define MOE_DIB_UNMANAGED   0x1000

extern const moe_point_t *moe_point_zero;
extern const moe_size_t *moe_size_zero;
extern const moe_rect_t *moe_rect_zero;
extern const moe_edge_insets_t *moe_edge_insets_zero;

#define MOE_COLOR_TRANSPARENT   0x00000000

moe_dib_t *moe_create_dib(moe_size_t *size, uint32_t flags, uint32_t color);
void moe_blt(moe_dib_t *dest, moe_dib_t *src, moe_point_t *origin, moe_rect_t *rect, uint32_t options);
void moe_fill_rect(moe_dib_t *dest, moe_rect_t *rect, uint32_t color);
void moe_blend_rect(moe_dib_t *dest, moe_rect_t *rect, uint32_t color);
void moe_fill_round_rect(moe_dib_t *dest, moe_rect_t *rect, int radius, uint32_t color);
void moe_draw_round_rect(moe_dib_t *dest, moe_rect_t *rect, int radius, uint32_t color);
void moe_draw_pixel(moe_dib_t *dest, int x, int y, uint32_t color);
void moe_draw_multi_pixels(moe_dib_t *dest, size_t count, moe_point_t *points, uint32_t color);
moe_rect_t moe_edge_insets_inset_rect(moe_rect_t *rect, moe_edge_insets_t *insets);
moe_point_t moe_draw_string(moe_dib_t *dib, moe_font_t *font, moe_point_t *cursor, moe_rect_t *rect, const char *s, uint32_t color);
moe_font_t *moe_get_system_font(int type);

void moe_set_console_attributes(moe_console_context_t *self, uint32_t attributes);
int moe_set_console_cursor_visible(moe_console_context_t *self, int visible);

void mgs_bsod(const char *);

typedef enum {
    window_level_desktop,
    window_level_desktop_items,
    window_level_normal = 32,
    window_level_higher = 64,
    window_level_popup_barrier = 96,
    window_level_popup,
    window_level_pointer = 127,
} moe_window_level_t;

#define MOE_WS_BORDER       0x0100
#define MOE_WS_CAPTION      0x0200
#define MOE_WS_TRANSPARENT  0x0400
#define MOE_WS_CLIENT_RECT  0x0800
#define MOE_WS_PINCHABLE    0x1000

moe_size_t moe_get_screen_size();
moe_edge_insets_t moe_add_screen_insets(moe_edge_insets_t *insets);
moe_window_t *moe_create_window(moe_rect_t *frame, uint32_t stlye, moe_window_level_t window_level, const char *title);
int moe_destroy_window(moe_window_t *self);
void moe_set_window_bgcolor(moe_window_t *self, uint32_t color);
moe_dib_t *moe_get_window_bitmap(moe_window_t *self);
moe_rect_t moe_get_window_bounds(moe_window_t *window);
moe_edge_insets_t moe_get_client_insets(moe_window_t *window);
moe_rect_t moe_get_client_rect(moe_window_t *window);
moe_point_t moe_convert_window_point_to_screen(moe_window_t *window, moe_point_t *point);
void moe_blt_to_window(moe_window_t *window, moe_dib_t *dib);
void moe_invalidate_rect(moe_window_t *window, moe_rect_t *rect);
void moe_show_window(moe_window_t *window);
void moe_hide_window(moe_window_t *window);
void moe_set_active_window(moe_window_t *window);
void moe_set_window_title(moe_window_t *window, const char *title);
int moe_alert(const char *title, const char *message, uint32_t flags);

typedef struct moe_hid_keyboard_report_t moe_hid_keyboard_report_t;
int moe_send_key_event(moe_hid_keyboard_report_t* report);
int moe_send_event(moe_window_t *window, uintptr_t event);
uintptr_t moe_get_event(moe_window_t *window, int wait);
uint32_t moe_translate_key_event(moe_window_t *window, uintptr_t event);


//  Minimal Memory Subsystem
void *mm_alloc_static_page(size_t n);
void *mm_alloc_static(size_t n);


//  Threading Service
typedef struct moe_thread_t moe_thread_t;
typedef uint8_t moe_priority_level_t;
typedef enum {
    priority_idle = 0,
    priority_low,
    priority_normal,
    priority_high,
    priority_realtime,
    priority_max,
} moe_priority_type_t;

typedef void (*moe_thread_start)(void *args);
int moe_create_thread(moe_thread_start start, moe_priority_level_t priority, void *args, const char *name);
void moe_yield();
int moe_usleep(uint64_t us);
int moe_get_current_thread();
int moe_get_usage();
_Noreturn void moe_exit_thread(uint32_t exit_code);

int moe_wait_for_object(void *obj, uint64_t us);
int moe_signal_object(moe_thread_t *thread, void *obj);

typedef uint64_t moe_timer_t;
typedef double moe_time_interval_t;
moe_timer_t moe_create_interval_timer(uint64_t);
int moe_check_timer(moe_timer_t*);
uint64_t moe_get_measure();

typedef struct moe_fifo_t moe_fifo_t;
moe_fifo_t *moe_fifo_init(size_t capacity);
intptr_t moe_fifo_read(moe_fifo_t *self, intptr_t default_val);
int moe_fifo_read_and_wait(moe_fifo_t* self, intptr_t* result, uint64_t us);
int moe_fifo_write(moe_fifo_t *self, intptr_t data);
size_t moe_fifo_get_estimated_count(moe_fifo_t *self);
size_t moe_fifo_get_estimated_free(moe_fifo_t *self);
