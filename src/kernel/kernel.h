// Minimal Operating Environment
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: BSD
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "acpi.h"


typedef struct {
    void* mmap; // EFI_MEMORY_DESCRIPTOR
    uintptr_t size, desc_size;
    uint32_t desc_version;
} moe_bootinfo_mmap_t;

typedef struct {
    moe_dib_t screen;
    acpi_rsd_ptr_t* acpi;

    void* efiRT; // EFI_RUNTIME_SERVICES
    moe_bootinfo_mmap_t mmap;
} moe_bootinfo_t;


//  Architecture Specific
typedef int (*MOE_IRQ_HANDLER)(int irq);
int moe_enable_irq(uint8_t irq, MOE_IRQ_HANDLER handler);
int moe_disable_irq(uint8_t irq);

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
void acpi_enter_sleep_state(int state);
void acpi_reset();


//  PCI
uint32_t pci_make_reg_addr(uint8_t bus, uint8_t dev, uint8_t func, uintptr_t reg);
uint32_t pci_read_config(uint32_t addr);
void pci_write_config(uint32_t addr, uint32_t val);
