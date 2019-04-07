// Page Manager
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "x86.h"

enum {
    MAX_PAGE_LEVEL = 4,
    SHIFT_PER_LEVEL = 9,
    NATIVE_PAGE_SIZE = 0x1000,
    DIRECT_MAP_PAGE = 0x140,
    KERNEL_PAGE = 0x1FC,
    RECURSIVE_PAGE = 0x1FE,
};
static const uint64_t max_physical_address = 0x000000FFFFFFFFFFLL;
static const uint64_t max_virtual_address =  0x0000FFFFFFFFFFFFLL;
static MOE_PHYSICAL_ADDRESS global_cr3;
static uintptr_t base_direct_map = 0;
typedef uint64_t pte_t;

#define ROUNDUP_PAGE(n) ((n + NATIVE_PAGE_SIZE - 1) & ~(NATIVE_PAGE_SIZE - 1))


static uint64_t get_rec_base(int level) {
    uintptr_t base = ~max_virtual_address;
    uintptr_t rec = 0;
    for (int i = 0; i < level; i++) {
        rec |= (uint64_t)RECURSIVE_PAGE << (SHIFT_PER_LEVEL * (MAX_PAGE_LEVEL - i));
    }
    rec <<= 3;
    return base | rec;
}

pte_t pg_get_pte(uintptr_t ptr, int level) {
    uintptr_t base = get_rec_base(level);
    uintptr_t ptr_ = ((ptr & max_virtual_address) >> (SHIFT_PER_LEVEL * level)) & ~7;
    _Atomic pte_t *pta = (void *)(base + ptr_);
    // printf("(%d) %012llx %012llx [%012llx] ", level, base, ptr_, (uintptr_t)pta);
    pte_t result = *pta;
    // printf("%012llx => %012llx\n", ptr, result);
    return result;
}

void pg_set_pte(uintptr_t ptr, pte_t pte, int level) {
    uintptr_t base = get_rec_base(level);
    ptr = ((ptr & max_virtual_address) >> (SHIFT_PER_LEVEL * level)) & ~7;
    _Atomic pte_t *pta = (void *)(base + ptr);
    *pta = pte;
}

static uintptr_t root_page_to_va(uintptr_t page) {
    if (page < 0x100) {
        return page << (MAX_PAGE_LEVEL * SHIFT_PER_LEVEL + 3);
    } else {
        return ~max_virtual_address | (page << (MAX_PAGE_LEVEL * SHIFT_PER_LEVEL + 3));
    }
}


void pg_alloc_mmio(uintptr_t _base, size_t _size) {
    uint32_t eflags = io_lock_irq();

    size_t size = ROUNDUP_PAGE(_size) / NATIVE_PAGE_SIZE;
    uintptr_t base = (_base & max_physical_address) & ~(NATIVE_PAGE_SIZE - 1);
    uintptr_t target = root_page_to_va(DIRECT_MAP_PAGE) + base;

    for (int i = 4; i > 1; i--) {
        pte_t parent_tbl = pg_get_pte(target, i);
        if (!parent_tbl || (parent_tbl & PTE_LARGE)) {
            pte_t p_tbl = moe_alloc_physical_page(NATIVE_PAGE_SIZE);
            pte_t *table = MOE_PA2VA(p_tbl);
            memset(table, 0, NATIVE_PAGE_SIZE);
            uintptr_t offset = base >> (3 + SHIFT_PER_LEVEL * i);
            pte_t *rec_tbl = (void *)get_rec_base(i);
            // printf("(%d) %012llx %08x <= %012llx\n", i, rec_tbl, offset, p_tbl);
            rec_tbl[offset] = p_tbl | PTE_PRESENT | PTE_WRITE;
            io_set_ptbr(io_get_ptbr());
        }
    }
    for (int i = 0; i < size; i++) {
        pte_t pte = (base + i * NATIVE_PAGE_SIZE) | PTE_PRESENT | PTE_WRITE | PTE_PCD;
        // printf("MMIO %012llx -> %012llx\n", target + i * NATIVE_PAGE_SIZE, pte);
        pg_set_pte(target + i * NATIVE_PAGE_SIZE, pte, 1);
    }
    io_set_ptbr(io_get_ptbr());
    apic_send_invalidate_tlb();
    io_unlock_irq(eflags);

}


void page_init() {

    pte_t common_attributes = PTE_PRESENT | PTE_WRITE;

    // master PML4
    // global_cr3 = moe_alloc_gates_memory();
    global_cr3 = moe_alloc_physical_page(NATIVE_PAGE_SIZE);
    pte_t *pml4 = (pte_t *)global_cr3;
    memset(pml4, 0, NATIVE_PAGE_SIZE);
    pml4[RECURSIVE_PAGE] = global_cr3 | common_attributes; // recursive

    // PML3
    pte_t PML30 = moe_alloc_physical_page(NATIVE_PAGE_SIZE);
    pte_t *pml30 = (pte_t *)PML30;
    memset(pml30, 0, NATIVE_PAGE_SIZE);
    pml4[0x000] = PML30 | common_attributes;
    pml4[DIRECT_MAP_PAGE] = PML30 | common_attributes;

    // PML2
    const int n_first_directmap = 8;
    pte_t PML20 = moe_alloc_physical_page(NATIVE_PAGE_SIZE * n_first_directmap);
    pte_t *pml20 = (pte_t *)PML20;
    for (uintptr_t i = 0; i < n_first_directmap; i++) {
        pml30[i] = (PML20 + (i << 12)) | common_attributes;
    }
    for (uintptr_t i = 0; i < 512 * n_first_directmap; i++) {
        pte_t la = (i << 21);
        pte_t attrs = common_attributes | PTE_LARGE;
        pml20[i] = la | attrs;
    }

    // Kernel PML3
    // MOE_PHYSICAL_ADDRESS KERNEL_PML3 = moe_alloc_physical_page(NATIVE_PAGE_SIZE);
    // uint64_t *kpml3 = (uint64_t *)KERNEL_PML3;
    // memset(kpml3, 0, NATIVE_PAGE_SIZE);
    // pml4[KERNEL_PAGE] = KERNEL_PML3 | common_attributes;

    io_set_ptbr(global_cr3);

    base_direct_map = root_page_to_va(DIRECT_MAP_PAGE);

}


/*********************************************************************/


void *MOE_PA2VA(MOE_PHYSICAL_ADDRESS pa) {
    // TODO:
    return (void*)(pa + root_page_to_va(DIRECT_MAP_PAGE));
}

uint8_t READ_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p) {
    _Atomic uint8_t *p = MOE_PA2VA(_p);
    return atomic_load(p);
}

void WRITE_PHYSICAL_UINT8(MOE_PHYSICAL_ADDRESS _p, uint8_t v) {
    _Atomic uint8_t *p = MOE_PA2VA(_p);
    atomic_store(p, v);
    // __asm__ volatile("movb %%dl, (%%rcx)":: "c"(p), "d"(v));
}

uint16_t READ_PHYSICAL_UINT16(MOE_PHYSICAL_ADDRESS _p) {
    _Atomic uint16_t *p = MOE_PA2VA(_p);
    return atomic_load(p);
}

void WRITE_PHYSICAL_UINT16(MOE_PHYSICAL_ADDRESS _p, uint16_t v) {
    _Atomic uint16_t *p = MOE_PA2VA(_p);
    atomic_store(p, v);
    // __asm__ volatile("movw %%dx, (%%rcx)":: "c"(p), "d"(v));
}

uint32_t READ_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p) {
    _Atomic uint32_t *p = MOE_PA2VA(_p);
    return atomic_load(p);
}

void WRITE_PHYSICAL_UINT32(MOE_PHYSICAL_ADDRESS _p, uint32_t v) {
    _Atomic uint32_t *p = MOE_PA2VA(_p);
    atomic_store(p, v);
    // __asm__ volatile("movl %%edx, (%%rcx)":: "c"(p), "d"(v));
}

uint64_t READ_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p) {
    _Atomic uint64_t *p = MOE_PA2VA(_p);
    return atomic_load(p);
}

void WRITE_PHYSICAL_UINT64(MOE_PHYSICAL_ADDRESS _p, uint64_t v) {
    _Atomic uint64_t *p = MOE_PA2VA(_p);
    atomic_store(p, v);
    // __asm__ volatile("movq %%rdx, (%%rcx)":: "c"(p), "d"(v));
}
