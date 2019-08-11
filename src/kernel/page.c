// Page Manager
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "x86.h"


enum {
    MAX_PAGE_LEVEL = 4,
    SHIFT_PER_LEVEL = 9,
    NATIVE_PAGE_SIZE = 0x00001000,
    VIRTUAL_PAGE_SIZE = 0x00010000,
    LARGE_PAGE_SIZE = 0x00200000,
    VRAM_PAGE = 0x100,
    DIRECT_MAP_PAGE = 0x140,
    RECURSIVE_PAGE = 0x1FE,
    KERNEL_HEAP_PAGE = 0x1FF,
};
static const uint64_t MAX_PA = 0x000000FFFFFFFFFFLL;
static const uint64_t MAX_VA = 0x0000FFFFFFFFFFFFLL;
static MOE_PHYSICAL_ADDRESS global_cr3;
static _Atomic uintptr_t base_kernel_heap = 0;
typedef uint64_t pte_t;


static void io_set_cr3(uintptr_t cr3) {
    __asm__ volatile("movq %0, %%cr3"::"r"(cr3));
}

static void io_invalidate_tlb() {
    uintptr_t rax;
    __asm__ volatile("movq %%cr3, %0; movq %0, %%cr3;": "=r"(rax));
}


static uintptr_t ceil_page(uintptr_t n, size_t page_size) {
    return ((n + page_size - 1) & ~(page_size - 1));
}

static uint64_t get_rec_base(int level) {
    uintptr_t base = ~MAX_VA;
    uintptr_t rec = 0;
    for (int i = 0; i < level; i++) {
        rec |= (uint64_t)RECURSIVE_PAGE << (SHIFT_PER_LEVEL * (MAX_PAGE_LEVEL - i));
    }
    rec <<= 3;
    return base | rec;
}

static uintptr_t va_to_offset(uintptr_t ptr, int level) {
    return ((ptr & MAX_VA) >> (SHIFT_PER_LEVEL * level)) & ~7;
}

pte_t pg_get_pte(uintptr_t ptr, int level) {
    _Atomic pte_t *pta = (void *)(get_rec_base(level) + va_to_offset(ptr, level));
    return *pta;
}

void pg_set_pte(uintptr_t ptr, pte_t pte, int level) {
    _Atomic pte_t *pta = (void *)(get_rec_base(level) + va_to_offset(ptr, level));
    *pta = pte;
}

static uintptr_t root_page_to_va(uintptr_t page) {
    if (page < 0x100) {
        return page << (MAX_PAGE_LEVEL * SHIFT_PER_LEVEL + 3);
    } else {
        return ~MAX_VA | (page << (MAX_PAGE_LEVEL * SHIFT_PER_LEVEL + 3));
    }
}


void invalidate_tlb() {
    io_invalidate_tlb();
    // smp_send_invalidate_tlb();
}


void *pg_map(uintptr_t base_pa, void *base_va, size_t size, uint64_t attributes) {
    size_t count = ceil_page(size, NATIVE_PAGE_SIZE) / NATIVE_PAGE_SIZE;
    pte_t common_attributes = PTE_PRESENT | PTE_WRITE;
    uintptr_t flags = io_lock_irq();
    for (size_t i = 0; i < count; i++) {
        uintptr_t target_va = (uintptr_t)base_va + i * NATIVE_PAGE_SIZE;
        uintptr_t target_pa = base_pa + i * NATIVE_PAGE_SIZE;
        for (size_t j = MAX_PAGE_LEVEL; j > 1; j--) {
            pte_t parent_tbl = pg_get_pte(target_va, j);
            if (!parent_tbl) {
                pte_t p_tbl = moe_alloc_physical_page(NATIVE_PAGE_SIZE);
                pte_t *table = MOE_PA2VA(p_tbl);
                memset(table, 0, NATIVE_PAGE_SIZE);
                pg_set_pte(target_va, (p_tbl | common_attributes), j);
                io_invalidate_tlb();
            }
        }
        pg_set_pte(target_va, target_pa | attributes, 1);
    }
    invalidate_tlb();
    io_unlock_irq(flags);

    return base_va;
}


void *pg_map_mmio(uintptr_t base, size_t _size) {
    uintptr_t pa = base & MAX_PA & ~(NATIVE_PAGE_SIZE - 1);
    return pg_map(pa, MOE_PA2VA(pa), _size, PTE_PRESENT | PTE_WRITE);
}

void *pg_map_vram(uintptr_t base, size_t size) {
    return pg_map(base, (void *)root_page_to_va(VRAM_PAGE), size, PTE_USER | PTE_WRITE | PTE_PRESENT);
}


void *pg_valloc(uintptr_t pa, size_t size) {
    size_t vsize = ceil_page(size, VIRTUAL_PAGE_SIZE) + VIRTUAL_PAGE_SIZE;
    void *va = (void *)atomic_fetch_add(&base_kernel_heap, vsize);
    pg_map(pa, va, size, PTE_PRESENT | PTE_WRITE);
    return va;
}


void pg_enter_strict_mode() {
    // return;
    pg_set_pte(0, 0, 4);
    invalidate_tlb();
}


void page_init(moe_bootinfo_t *bootinfo) {

    const pte_t common_attributes = PTE_PRESENT | PTE_WRITE;

    // master PML4
    global_cr3 = bootinfo->master_cr3;
    pte_t *pml4_va = (pte_t *)global_cr3;
    pml4_va[RECURSIVE_PAGE] = global_cr3 | common_attributes;
    pml4_va[DIRECT_MAP_PAGE] = pml4_va[0];

    base_kernel_heap = root_page_to_va(KERNEL_HEAP_PAGE);

    // VRAM
    MOE_PHYSICAL_ADDRESS pml3v_pa = moe_alloc_physical_page(NATIVE_PAGE_SIZE);
    uint64_t *pml3v_va = (uint64_t *)pml3v_pa;
    memset(pml3v_va, 0, NATIVE_PAGE_SIZE);
    pml4_va[VRAM_PAGE] = pml3v_pa | common_attributes | PTE_USER;

    pte_t pml2v_pa = moe_alloc_physical_page(NATIVE_PAGE_SIZE);
    pte_t *pml2v_va = (pte_t *)pml2v_pa;
    memset(pml2v_va, 0, NATIVE_PAGE_SIZE);
    pml3v_va[0] = pml2v_pa | common_attributes | PTE_USER;

    io_set_cr3(global_cr3);

}


/*********************************************************************/


void *MOE_PA2VA(MOE_PHYSICAL_ADDRESS pa) {
    return (void*)(pa + root_page_to_va(DIRECT_MAP_PAGE));
}

uint8_t READ_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p) {
    _Atomic uint8_t *p = MOE_PA2VA(_p);
    return atomic_load(p);
}

void WRITE_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p, uint8_t v) {
    _Atomic uint8_t *p = MOE_PA2VA(_p);
    atomic_store(p, v);
}

uint16_t READ_PHYSICAL_UINT16(MOE_PHYSICAL_ADDRESS _p) {
    _Atomic uint16_t *p = MOE_PA2VA(_p);
    return atomic_load(p);
}

void WRITE_PHYSICAL_UINT16(MOE_PHYSICAL_ADDRESS _p, uint16_t v) {
    _Atomic uint16_t *p = MOE_PA2VA(_p);
    atomic_store(p, v);
}

uint32_t READ_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p) {
    _Atomic uint32_t *p = MOE_PA2VA(_p);
    return atomic_load(p);
}

void WRITE_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p, uint32_t v) {
    _Atomic uint32_t *p = MOE_PA2VA(_p);
    atomic_store(p, v);
}

uint64_t READ_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p) {
    _Atomic uint64_t *p = MOE_PA2VA(_p);
    return atomic_load(p);
}

void WRITE_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p, uint64_t v) {
    _Atomic uint64_t *p = MOE_PA2VA(_p);
    atomic_store(p, v);
}
