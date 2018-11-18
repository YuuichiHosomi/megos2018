// Page Manager
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "x86.h"

#define NATIVE_PAGE_SIZE   0x1000

// static MOE_PHYSICAL_ADDRESS master_cr3;
// void page_init() {
//     master_cr3 = mm_alloc_static_page(PAGE_SIZE);
//     memset(master_cr3, 0, PAGE_SIZE);

//     MOE_PHYSICAL_ADDRESS pdpt0 = mm_alloc_static_page(PAGE_SIZE);
//     memset(pdpt0, 0, PAGE_SIZE);
//     WRITE_PHYSICAL_UINT64(master_cr3, pdpt0 | PTE_GLOBAL | PTE_WRITE | PTE_PRESENT);
// }

void* PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(MOE_PHYSICAL_ADDRESS pa) {
    // TODO:
    return (void*)(pa);
}

uint8_t READ_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p) {
    _Atomic uint8_t* p = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(_p);
    return atomic_load(p);
}

void WRITE_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p, uint8_t v) {
    _Atomic uint8_t* p = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(_p);
    atomic_store(p, v);
}

uint32_t READ_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p) {
    _Atomic uint32_t* p = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(_p);
    return atomic_load(p);
}

void WRITE_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p, uint32_t v) {
    _Atomic uint32_t* p = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(_p);
    atomic_store(p, v);
}

uint64_t READ_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p) {
    _Atomic uint64_t* p = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(_p);
    return atomic_load(p);
}

void WRITE_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p, uint64_t v) {
    _Atomic uint64_t* p = PHYSICAL_ADDRESS_TO_VIRTUAL_ADDRESS(_p);
    atomic_store(p, v);
}
