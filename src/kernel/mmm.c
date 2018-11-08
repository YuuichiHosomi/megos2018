// Minimal Memory Management Subsystem
// Copyright (c) 1998,2000,2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "efi.h"

#define ROUNDUP_4K(n) ((n + 0xFFF) & ~0xFFF)

typedef struct {
    uintptr_t base;
    uintptr_t size;
    uintptr_t type;
} moe_mmap;

#define HEAP_SIZE   0x400000
static uint8_t static_heap[HEAP_SIZE] __attribute__((aligned(4096)));
static atomic_uintptr_t static_start;


void* mm_alloc_static_page(size_t n) {
    uintptr_t result = atomic_fetch_add(&static_start, ROUNDUP_4K(n));
    return (void*)result;
}

void* mm_alloc_static(size_t n) {
    //  TODO:
    return mm_alloc_static_page(n);
}


/*********************************************************************/


uintptr_t total_memory = 0;

static int mm_type_for_count(uint32_t type) {
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

extern EFI_RUNTIME_SERVICES* gRT;

void mm_init(moe_bootinfo_mmap_t* mmap) {

    static_start = ROUNDUP_4K((uintptr_t)static_heap);

    uintptr_t mmap_ptr = (uintptr_t)mmap->mmap;
    int n_mmap = mmap->size / mmap->desc_size;
    for (int i = 0; i < n_mmap; i++) {
        EFI_MEMORY_DESCRIPTOR* efi_mem = (EFI_MEMORY_DESCRIPTOR*)(mmap_ptr + i * mmap->desc_size);
        efi_mem->VirtualStart = efi_mem->PhysicalStart;
        if (efi_mem->PhysicalStart >= 0x100000) {
            // moe_mmap mem = { efi_mem->PhysicalStart, efi_mem->NumberOfPages*0x1000, mm_type_for_count(efi_mem->Type) };
            // printf("%016llx %08zx %08zx\n", mem.base, mem.size, mem.type);
            if (mm_type_for_count(efi_mem->Type) > 0) {
                total_memory += efi_mem->NumberOfPages;
            }
        }
    }
    gRT->SetVirtualAddressMap(mmap->size, mmap->desc_size, mmap->desc_version, mmap->mmap);

    // printf("mm: %08zx %08zx\n", mmap->size, mmap->desc_size);

}
