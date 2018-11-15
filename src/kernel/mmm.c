// Minimal Memory Management Subsystem
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "efi.h"

#define ROUNDUP_PAGE(n) ((n + 0xFFF) & ~0xFFF)

typedef struct {
    uintptr_t base;
    uintptr_t size;
    uintptr_t type;
} moe_mmap;


static atomic_uintptr_t static_start;

void* mm_alloc_static_page(size_t n) {
    uintptr_t result = atomic_fetch_add(&static_start, ROUNDUP_PAGE(n));
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

// static int mm_type_for_free(uint32_t type) {
//     switch (type) {
//         case EfiReservedMemoryType:
//         case EfiUnusableMemory:
//         case EfiMemoryMappedIO:
//         case EfiMemoryMappedIOPortSpace:
//         default:
//             return 0;

//         case EfiConventionalMemory:
//         case EfiBootServicesCode:
//         case EfiBootServicesData:
//             return EfiConventionalMemory;

//         case EfiLoaderCode:
//         case EfiLoaderData:
//         case EfiACPIMemoryNVS:
//         case EfiACPIReclaimMemory:
//         case EfiRuntimeServicesCode:
//         case EfiRuntimeServicesData:
//             return type;
//     }
// }

extern EFI_RUNTIME_SERVICES* gRT;

void mm_init(moe_bootinfo_mmap_t* mmap) {

    uint64_t mabase = 0, masize = 0x1000;

    uintptr_t mmap_ptr = (uintptr_t)mmap->mmap;
    int n_mmap = mmap->size / mmap->desc_size;
    for (int i = 0; i < n_mmap; i++) {
        EFI_MEMORY_DESCRIPTOR* efi_mem = (EFI_MEMORY_DESCRIPTOR*)(mmap_ptr + i * mmap->desc_size);
        efi_mem->VirtualStart = efi_mem->PhysicalStart;
        uint32_t type = mm_type_for_count(efi_mem->Type);
        if (type == EfiConventionalMemory && masize < efi_mem->NumberOfPages && ROUNDUP_PAGE(efi_mem->PhysicalStart) == efi_mem->PhysicalStart && efi_mem->PhysicalStart < UINT32_MAX ) {
            mabase = efi_mem->PhysicalStart;
            masize = efi_mem->NumberOfPages;
        }
        if (type > 0) {
            total_memory += efi_mem->NumberOfPages;
        }
        // moe_mmap mem = { efi_mem->PhysicalStart, efi_mem->NumberOfPages*0x1000, mm_type_for_count(efi_mem->Type) };
        // printf("%016llx %08zx %08zx\n", mem.base, mem.size, mem.type);
    }
    gRT->SetVirtualAddressMap(mmap->size, mmap->desc_size, mmap->desc_version, mmap->mmap);

    static_start = mabase;

    // printf("mm: %08zx %08zx\n", mmap->size, mmap->desc_size);

}
