// Minimal Operating Environment
// Copyright (c) 1998,2000,2018 MEG-OS project, All rights reserved.
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


typedef volatile struct {
    volatile intptr_t* data;
    volatile uintptr_t read, write, free, count;
    uintptr_t mask, flags;
} moe_fifo_t;


//  Architecture Specific
extern void start_kernel(moe_bootinfo_t* bootinfo) __attribute__((__noreturn__));
void arch_init();

typedef int (*IRQ_HANDLER)(int irq, void* context);

uintptr_t atomic_exchange_add(volatile uintptr_t*, uintptr_t);
int atomic_compare_and_swap(volatile uintptr_t* p, uintptr_t expected, uintptr_t new_value);
void io_hlt();
void io_pause();

typedef uintptr_t MOE_PHYSICAL_ADDRESS;
void* PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(MOE_PHYSICAL_ADDRESS va);
uint8_t READ_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p);
void WRITE_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p, uint8_t v);
uint32_t READ_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p);
void WRITE_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p, uint32_t v);
uint64_t READ_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p);
void WRITE_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p, uint64_t v);


//  ACPI
void acpi_init(acpi_rsd_ptr_t* rsd);
void* acpi_find_table(const char* signature);
int acpi_get_number_of_table_entries();
void* acpi_enum_table_entry(int index);


//  Minimal Graphics Subsystem
void mgs_init(moe_video_info_t* _video);
void mgs_fill_rect(int x, int y, int width, int height, uint32_t color);
void mgs_fill_block(int x, int y, int width, int height, uint32_t color);
void mgs_cls();
void mgs_bsod();


//  Minimal Memory Subsystem
uintptr_t mm_init(void * efi_rt, moe_bootinfo_mmap_t* mmap);
void* mm_alloc_static_page(size_t n);
void* mm_alloc_static(size_t n);
void moe_fifo_init(moe_fifo_t* self, intptr_t* data, uintptr_t capacity);
intptr_t moe_fifo_read(moe_fifo_t* self, intptr_t default_val);
int moe_fifo_write(moe_fifo_t* self, intptr_t data);


//  Timer Service
typedef uint64_t moe_timer_t;
typedef double moe_time_interval_t;
moe_timer_t moe_create_interval_timer(moe_time_interval_t);
int moe_wait_for_timer(moe_timer_t*);
int moe_check_timer(moe_timer_t*);
