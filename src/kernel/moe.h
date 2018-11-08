// Minimal Operating Environment
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: BSD
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "acpi.h"


int printf(const char* format, ...);
void* memcpy(void* p, const void* q, size_t n);
void* memset(void * p, int v, size_t n);
void memset32(uint32_t* p, uint32_t v, size_t n);


typedef struct {
    void* vram;
    int res_x, res_y, pixel_per_scan_line;
} moe_video_info_t;

typedef struct {
    void* mmap; // EFI_MEMORY_DESCRIPTOR
    uintptr_t size, desc_size;
    uint32_t desc_version;
} moe_bootinfo_mmap_t;

typedef struct {
    moe_video_info_t video;
    acpi_rsd_ptr_t* acpi;

    void* efiRT; // EFI_RUNTIME_SERVICES
    moe_bootinfo_mmap_t mmap;
} moe_bootinfo_t;


void moe_assert(const char* file, uintptr_t line, ...);
#define MOE_ASSERT(cond, ...) if (!(cond)) { moe_assert(__FILE__, __LINE__, __VA_ARGS__); }


//  Architecture Specific
typedef int (*IRQ_HANDLER)(int irq, void* context);

static inline void io_hlt() { __asm__ volatile("hlt"); }
static inline void io_pause() { __asm__ volatile("pause"); }

typedef uintptr_t MOE_PHYSICAL_ADDRESS;
void* PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(MOE_PHYSICAL_ADDRESS va);
uint8_t READ_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p);
void WRITE_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p, uint8_t v);
uint32_t READ_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p);
void WRITE_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p, uint32_t v);
uint64_t READ_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p);
void WRITE_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p, uint64_t v);


//  ACPI
void* acpi_find_table(const char* signature);
int acpi_get_number_of_table_entries();
void* acpi_enum_table_entry(int index);


//  Minimal Graphics Subsystem
void mgs_fill_rect(int x, int y, int width, int height, uint32_t color);
void mgs_fill_block(int x, int y, int width, int height, uint32_t color);
void mgs_cls();
void mgs_bsod();


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

typedef struct _moe_fifo_t moe_fifo_t;
void moe_fifo_init(moe_fifo_t** result, uintptr_t capacity);
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
