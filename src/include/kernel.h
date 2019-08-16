// Minimal Operating Environment
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: MIT
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "acpi.h"


#define MAX_GATES_INDEX     8
typedef struct {
    uint64_t master_cr3;
    uint64_t acpi;
    uint64_t smbios;
    uint64_t vram_base;
    union {
        uint64_t _PADDING;
        struct {
            int16_t width, height, delta;
        };
    } screen;
    uint64_t mmbase, mmsize, mmdescsz, mmver;
    uint64_t kernel_base;
    uint64_t total_memory;
    _Atomic uint32_t free_memory;
    _Atomic uint32_t static_start;
    uint32_t boottime[4];
    _Atomic uint32_t gates_memory_bitmap[MAX_GATES_INDEX];
} moe_bootinfo_t;


//  Architecture Specific
static inline void io_out8(uintptr_t port, uint8_t value) {
    __asm__ volatile ("outb %%al, %%dx":: "a"(value), "d"(port));
}

static inline void io_out16(uintptr_t port, uint16_t value) {
    __asm__ volatile ("outw %%ax, %%dx":: "a"(value), "d"(port));
}

static inline void io_out32(uintptr_t port, uint32_t value) {
    __asm__ volatile ("outl %%eax, %%dx":: "a"(value), "d"(port));
}

static inline uint8_t io_in8(uintptr_t port) {
    uint16_t value;
    __asm__ volatile ("inb %%dx, %%al": "=a"(value) :"d"(port));
    return value;
}

static inline uint16_t io_in16(uintptr_t port) {
    uint16_t value;
    __asm__ volatile ("inw %%dx, %%ax": "=a"(value) :"d"(port));
    return value;
}

static inline uint32_t io_in32(uintptr_t port) {
    uint32_t value;
    __asm__ volatile ("inl %%dx, %%eax": "=a"(value) :"d"(port));
    return value;
}

static inline void io_hlt() { __asm__ volatile("hlt"); }
#define io_pause __builtin_ia32_pause
// static inline void io_pause() { __asm__ volatile("pause"); }

// int atomic_bit_test_and_set(void *p, size_t bit);
// static int atomic_bit_test_and_clear(void *p, size_t bit) {
// int atomic_bit_test(void *p, size_t bit);


typedef void (*MOE_IRQ_HANDLER)(int irq);
int moe_install_irq(uint8_t irq, MOE_IRQ_HANDLER handler);
int moe_uninstall_irq(uint8_t irq);
int moe_install_msi(MOE_IRQ_HANDLER handler);
uint8_t moe_make_msi_data(int irq, int mode, uint64_t *addr, uint32_t *data);

typedef uintptr_t MOE_PHYSICAL_ADDRESS;
void *MOE_PA2VA(MOE_PHYSICAL_ADDRESS va);
uint8_t READ_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p);
void WRITE_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p, uint8_t v);
uint16_t READ_PHYSICAL_UINT16(MOE_PHYSICAL_ADDRESS _p);
void WRITE_PHYSICAL_UINT16(MOE_PHYSICAL_ADDRESS _p, uint16_t v);
uint32_t READ_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p);
void WRITE_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p, uint32_t v);
uint64_t READ_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p);
void WRITE_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p, uint64_t v);


uintptr_t io_lock_irq();
void io_unlock_irq(uintptr_t);


// Low Level Memory Manager
uintptr_t moe_alloc_physical_page(size_t n);
uintptr_t moe_alloc_gates_memory();

uint64_t pg_get_pte(uintptr_t ptr, int level);
void pg_set_pte(uintptr_t ptr, uint64_t pte, int level);
int smp_send_invalidate_tlb();
void *pg_map_mmio(uintptr_t base, size_t size);
void *pg_valloc(uintptr_t pa, size_t size);
void *pg_map_vram(uintptr_t base, size_t size);


//  ACPI
void* acpi_find_table(const char* signature);
int acpi_get_number_of_table_entries();
void* acpi_enum_table_entry(int index);
void acpi_enter_sleep_state(int state);
void acpi_reset();
int acpi_get_pm_timer_type();
uint32_t acpi_read_pm_timer();


//  PCI
uint32_t pci_make_reg_addr(uint8_t bus, uint8_t dev, uint8_t func, uintptr_t reg);
uint32_t pci_read_config(uint32_t addr);
void pci_write_config(uint32_t addr, uint32_t val);
int pci_parse_bar(uint32_t _base, unsigned idx, uint64_t *_bar, uint64_t *_size);
uint32_t pci_find_by_class(uint32_t cls, uint32_t mask);
void pci_dump_config(uint32_t base, void *p);
uint32_t pci_find_capability(uint32_t base, uint8_t id);
