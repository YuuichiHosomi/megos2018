// Minimal Operating Environment
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: BSD
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


int printf(const char* format, ...);
void* memcpy(void* p, const void* q, size_t n);
void* memset(void * p, int v, size_t n);
void memset32(uint32_t* p, uint32_t v, size_t n);
int atomic_bit_test_and_set(void *p, uintptr_t bit);
int atomic_bit_test_and_clear(void *p, uintptr_t bit);
int atomic_bit_test(void *p, uintptr_t bit);


void moe_assert(const char* file, uintptr_t line, ...);
#define MOE_ASSERT(cond, ...) if (!(cond)) { moe_assert(__FILE__, __LINE__, __VA_ARGS__); }


//  Minimal Graphics Subsystem
void mgs_cls();
void mgs_fill_rect(int x, int y, int width, int height, uint32_t color);

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

typedef struct moe_dib_t {
    uint32_t* dib;
    int width, height;
    uint32_t flags, delta, color_key;
} moe_dib_t;

typedef struct moe_view_t moe_view_t;

#define MOE_DIB_COLOR_KEY   0x0001
#define MOE_DIB_ROTATE      0x0002

extern const moe_point_t *moe_point_zero;
extern const moe_size_t *moe_size_zero;
extern const moe_rect_t *moe_rect_zero;

moe_dib_t *moe_create_dib(moe_size_t *size, uint32_t flags, uint32_t color);
void moe_blt(moe_dib_t* dest, moe_dib_t* src, moe_point_t *origin, moe_rect_t *rect, uint32_t options);
void moe_fill_rect(moe_dib_t* dest, moe_rect_t *rect, uint32_t color);
void moe_invalidate_screen(moe_rect_t *rect);


//  Minimal Memory Subsystem
void* mm_alloc_static_page(size_t n);
void* mm_alloc_static(size_t n);


//  Threading Service
typedef void (*moe_start_thread)(void* args);
int moe_create_thread(moe_start_thread start, void* args, const char* name);
void moe_yield();
void moe_consume_quantum();
int moe_usleep(uint64_t us);
int moe_get_current_thread();

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


//  HID Service
#define INVALID_UNICHAR     0xFFFE
#define HID_MOD_LCTRL       0x0001
#define HID_MOD_LSHIFT      0x0002
#define HID_MOD_LALT        0x0004
#define HID_MOD_LGUI        0x0008
#define HID_MOD_RCTRL       0x0010
#define HID_MOD_RSHIFT      0x0020
#define HID_MOD_RALT        0x0040
#define HID_MOD_RGUI        0x0080

typedef struct {
    union {
        uint8_t buttons;
        struct {
            uint8_t l_button:1;
            uint8_t r_button:1;
            uint8_t m_button:1;
        };
        int32_t PADDING_1;
    };
    int16_t x, y;
} moe_hid_mouse_report_t;

typedef struct {
    uint8_t modifier;
    uint8_t RESERVED_1;
    uint8_t keydata[6];
} moe_hid_keyboard_report_t;

int hid_getchar();
