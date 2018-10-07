/*

    Minimal Memory Management Subsystem

    Copyright (c) 1998,2000,2018 MEG-OS project, All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

*/
#include "moe.h"
#include "efi.h"

typedef struct {
    uintptr_t base;
    uintptr_t size;
    uintptr_t type;
} moe_mmap;

#define HEAP_SIZE   0x400000
static uint8_t static_heap[HEAP_SIZE];
static volatile uintptr_t static_start;

#define ROUNDUP_4K(n) ((n + 0xFFF) & ~0xFFF)

void* mm_alloc_static(size_t n) {
    uintptr_t result = atomic_exchange_add(&static_start, ROUNDUP_4K(n));
    return (void*)result;
}

uintptr_t total_memory = 0;

static int efi_mm_type_convert(uint32_t type) {
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

uintptr_t mm_init(void* efi_mmap, uintptr_t efi_mmap_size, uintptr_t efi_mmap_desc_size) {

    static_start = ROUNDUP_4K((uintptr_t)static_heap);

    uint8_t* mmap_ptr = (uint8_t*)efi_mmap;
    int n_mmap = efi_mmap_size / efi_mmap_desc_size;
    for (int i = 0; i < n_mmap; i++) {
        EFI_MEMORY_DESCRIPTOR* efi_mem = (EFI_MEMORY_DESCRIPTOR*)(mmap_ptr + i * efi_mmap_desc_size);
        if (efi_mem->PhysicalStart >= 0x100000) {
            moe_mmap mem = { efi_mem->PhysicalStart, efi_mem->NumberOfPages*0x1000, efi_mm_type_convert(efi_mem->Type) };
            if (mem.type > 0) {
                total_memory += mem.size;
            }
        }
    }

    return total_memory;
}
