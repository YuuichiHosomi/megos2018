// Minimal Boot Time Paging Manager
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

#include <stdatomic.h>
#include "boot.h"
#include "x86.h"


enum {
    MAX_PAGE_LEVEL = 4,
    SHIFT_PER_LEVEL = 9,
    NATIVE_PAGE_SIZE = 0x00001000,
    LARGE_2M_PAGE_SIZE = 0x00200000,
    KERNEL_HEAP_PAGE = 0x1FF,
    KERNEL_HEAP_PAGE3 = 0x1FE,
    MAX_GATES_MEMORY = 0xA0000,
};

static const uint64_t MAX_VA = UINT64_C(0x0000FFFFFFFFFFFF);
static moe_bootinfo_t *bootinfo;
typedef uint64_t pte_t;
static uintptr_t kma_base, kma_size = 0x1000;
static uint64_t master_cr3;

static int mm_type_for_count(int type) {
    switch (type) {
        case EfiReservedMemoryType:
        case EfiUnusableMemory:
        case EfiMemoryMappedIO:
        case EfiMemoryMappedIOPortSpace:
        default:
            return 0;

        case EfiConventionalMemory:
            return EfiConventionalMemory;
        case EfiBootServicesCode:
        case EfiBootServicesData:
        case EfiLoaderCode:
        case EfiLoaderData:
        case EfiACPIMemoryNVS:
        case EfiACPIReclaimMemory:
        case EfiRuntimeServicesCode:
        case EfiRuntimeServicesData:
            return type;
    }
}

static uintptr_t ceil(uintptr_t n, size_t page_size) {
    return ((n + page_size - 1) & ~(page_size - 1));
}

static uintptr_t alloc_page(size_t n) {
    uintptr_t size = n * NATIVE_PAGE_SIZE;
    uintptr_t result = atomic_fetch_add(&bootinfo->static_start, size);
    atomic_fetch_add(&bootinfo->free_memory, size);
    void *p = (void *)result;
    memset(p, 0, size);
    return result;
}

pte_t *pml2kv;

static pte_t *va_set_l2(uint64_t base) {
    const pte_t common_attributes = PTE_PRESENT | PTE_WRITE;
    uintptr_t page = (base / LARGE_2M_PAGE_SIZE) & 0x1FF;
    uintptr_t offset = (base / NATIVE_PAGE_SIZE) & 0x1FF;
    pte_t pml1 = pml2kv[page];
    if (!pml1) {
        pte_t pml1 = alloc_page(1);
        pml2kv[page] = pml1 | common_attributes;
    }
    pte_t *pml1v = (pte_t*)(pml1 & ~0xFFF);
    return pml1v + offset;
}

void *valloc(uint64_t base, size_t size) {

    const pte_t common_attributes = PTE_PRESENT | PTE_WRITE;
    size_t sz = ceil(size, NATIVE_PAGE_SIZE) / NATIVE_PAGE_SIZE;
    uintptr_t blob = alloc_page(sz);

    for (int i = 0; i < sz; i++) {
        pte_t *p = va_set_l2(base + i * NATIVE_PAGE_SIZE);
        *p = (blob + i * NATIVE_PAGE_SIZE) | common_attributes;
    }

    return (void*)blob;
}

void vprotect(uint64_t base, size_t size, int attr) {

    pte_t attrmask = ~(PTE_NOT_EXECUTE | PTE_WRITE | PTE_PRESENT);
    pte_t attrval = ((attr & PROT_READ) ? PTE_PRESENT : 0)
        | ((attr & PROT_WRITE) ? PTE_WRITE : 0)
        | ((attr & PROT_EXEC) ? 0 : PTE_NOT_EXECUTE)
        ;
    size_t sz = ceil(size, NATIVE_PAGE_SIZE) / NATIVE_PAGE_SIZE;

    for (int i = 0; i < sz; i++) {
        pte_t *p = va_set_l2(base + i * NATIVE_PAGE_SIZE);
        *p = (*p & attrmask) | attrval;
    }
}


void page_init(moe_bootinfo_t *_bootinfo, void *mmap, size_t mmsize, size_t mmdescsz) {

    bootinfo = _bootinfo;

    // Scan memmap
    uintptr_t total_memory = 0;
    uintptr_t mmap_ptr = (uintptr_t)mmap;
    uintptr_t n_mmap = mmsize / mmdescsz;
    EFI_PHYSICAL_ADDRESS last_pa_4G = 0;
    for (uintptr_t i = 0; i < n_mmap; i++) {
        EFI_MEMORY_DESCRIPTOR* efi_mem = (EFI_MEMORY_DESCRIPTOR*)(mmap_ptr + i * mmdescsz);
        efi_mem->VirtualStart = efi_mem->PhysicalStart;
        uint32_t type = mm_type_for_count(efi_mem->Type);
        size_t n_pages = efi_mem->NumberOfPages;
        EFI_PHYSICAL_ADDRESS base_pa = efi_mem->PhysicalStart;
        EFI_PHYSICAL_ADDRESS last_pa = base_pa + n_pages * NATIVE_PAGE_SIZE - 1;
        if (type == EfiConventionalMemory) {
            if (last_pa < MAX_GATES_MEMORY) {
                for (int i = (base_pa >> 12); i < n_pages; i++) {
                    int offset = i / 32;
                    int position = i % 32;
                    bootinfo->gates_memory_bitmap[offset] |= (1 << position);
                }
            }
            if (kma_size < n_pages && ceil(base_pa, NATIVE_PAGE_SIZE) == base_pa && base_pa < UINT32_MAX ) {
                kma_base = base_pa;
                kma_size = n_pages;
            }
        }
        if (type > 0) {
            total_memory += n_pages;
            if (last_pa <= UINT32_MAX && last_pa > last_pa_4G) {
                last_pa_4G = last_pa;
            }
        }
    }
    bootinfo->total_memory = total_memory;
    bootinfo->static_start = kma_base;
    bootinfo->free_memory = kma_size * NATIVE_PAGE_SIZE;

    // Initialize minimal paging
    const pte_t common_attributes = PTE_PRESENT | PTE_WRITE;
    master_cr3 = alloc_page(1);
    pte_t *pml4v = (pte_t *)master_cr3;
    bootinfo->master_cr3 = master_cr3;

    pte_t pml3 = alloc_page(1);
    pte_t* pml3v = (pte_t *)pml3;
    pml4v[0] = pml3 | common_attributes;

    const int n_first_directmap = 4;
    pte_t pml2 = alloc_page(n_first_directmap);
    pte_t *pml2v = (pte_t *)pml2;
    for (uintptr_t i = 0; i < n_first_directmap; i++) {
        pml3v[i] = (pml2 + i * NATIVE_PAGE_SIZE) | common_attributes;
    }
    uintptr_t limit = (last_pa_4G + LARGE_2M_PAGE_SIZE) / LARGE_2M_PAGE_SIZE;
    for (uintptr_t i = 0; i < limit; i++) {
        pte_t la = i * LARGE_2M_PAGE_SIZE;
        pml2v[i] = la | common_attributes | PTE_LARGE;
    }

    // kernel memory
    pte_t pml3k = alloc_page(1);
    pte_t *pml3kv = (pte_t*)pml3k;
    pml4v[KERNEL_HEAP_PAGE] = pml3k | common_attributes;

    pte_t pml2k = alloc_page(1);
    pml2kv = (pte_t *)pml2k;
    pml3kv[KERNEL_HEAP_PAGE3] = pml2k | common_attributes;

    bootinfo->kernel_base = ~MAX_VA
        | ((uint64_t)KERNEL_HEAP_PAGE << (MAX_PAGE_LEVEL * SHIFT_PER_LEVEL + 3))
        | ((uint64_t)KERNEL_HEAP_PAGE3 << (3 * SHIFT_PER_LEVEL + 3))
        ;

    // vram (temp)
    // uintptr_t vram_base = bootinfo->vram_base;
    // size_t vram_size = ceil(bootinfo->screen.delta * bootinfo->screen.height * 4, LARGE_2M_PAGE_SIZE) / LARGE_2M_PAGE_SIZE;
    // uintptr_t offset = vram_base / LARGE_2M_PAGE_SIZE;
    // for (uintptr_t i = 0 ; i < vram_size; i++) {
    //     pml2v[offset + i] = (vram_base + i * LARGE_2M_PAGE_SIZE) | common_attributes | PTE_LARGE;
    // }

}
