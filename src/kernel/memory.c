// Minimal Memory Management Subsystem
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "efi.h"

#define PAGE_SIZE   0x1000
#define ROUNDUP_PAGE(n) ((n + 0xFFF) & ~0xFFF)

typedef struct {
    uintptr_t base;
    uintptr_t size;
    uintptr_t type;
} moe_mmap_t;


uintptr_t total_memory = 0;
static _Atomic uintptr_t free_memory;
static _Atomic uintptr_t static_start;

void *mm_alloc_static_page(size_t n) {
    uintptr_t size = ROUNDUP_PAGE(n);
    uintptr_t free = atomic_load(&free_memory);
    while (free > size) {
        if (atomic_compare_exchange_strong(&free_memory, &free, free - size)) {
            uintptr_t result = atomic_fetch_add(&static_start, size);
            return (void*)result;
        } else {
            io_pause();
        }
    }
    return NULL;
}

void* mm_alloc_static(size_t n) {
    //  TODO:
    return mm_alloc_static_page(n);
}


/*********************************************************************/



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
static uintptr_t kma_base, kma_size = 0x1000;

void mm_init(moe_bootinfo_mmap_t* mmap) {

    uintptr_t mmap_ptr = (uintptr_t)mmap->mmap;
    int n_mmap = mmap->size / mmap->desc_size;
    for (int i = 0; i < n_mmap; i++) {
        EFI_MEMORY_DESCRIPTOR* efi_mem = (EFI_MEMORY_DESCRIPTOR*)(mmap_ptr + i * mmap->desc_size);
        efi_mem->VirtualStart = efi_mem->PhysicalStart;
        uint32_t type = mm_type_for_count(efi_mem->Type);
        if (type == EfiConventionalMemory && kma_size < efi_mem->NumberOfPages && ROUNDUP_PAGE(efi_mem->PhysicalStart) == efi_mem->PhysicalStart && efi_mem->PhysicalStart < UINT32_MAX ) {
            kma_base = efi_mem->PhysicalStart;
            kma_size = efi_mem->NumberOfPages;
        }
        if (type > 0) {
            total_memory += efi_mem->NumberOfPages;
        }
        // moe_mmap_t mem = { efi_mem->PhysicalStart, efi_mem->NumberOfPages*0x1000, mm_type_for_count(efi_mem->Type) };
        // printf("%016llx %08zx %08zx\n", mem.base, mem.size, mem.type);
    }
    gRT->SetVirtualAddressMap(mmap->size, mmap->desc_size, mmap->desc_version, mmap->mmap);

    static_start = kma_base;
    free_memory = kma_size * PAGE_SIZE;

}

int cmd_mem(int argc, char **argv) {
    printf("Memory: %d MB\n", (int)(total_memory >> 8));
    printf("Kernel: %08zx-%08zx %zuKB/%zuKB (%zuMB)\n",
    kma_base, (kma_base + kma_size * PAGE_SIZE - 1), free_memory /1024, kma_size * PAGE_SIZE / 1024,
    (kma_size * PAGE_SIZE) >> 20);
    return 0;
}
