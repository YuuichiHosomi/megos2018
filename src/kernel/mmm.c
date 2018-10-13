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

void* mm_alloc_static_pages(size_t n) {
    uintptr_t result = atomic_exchange_add(&static_start, ROUNDUP_4K(n));
    return (void*)result;
}

void* mm_alloc_static(size_t n) {
    //  TODO:
    return mm_alloc_static_pages(n);
}


/*********************************************************************/


void moe_ring_buffer_init(moe_ring_buffer_t* self, intptr_t* data, uintptr_t capacity) {
    self->data = data;
    self->read = self->write = self->count = self->flags = 0;
    self->mask = self->free = capacity - 1;
}

intptr_t moe_ring_buffer_read(moe_ring_buffer_t* self, intptr_t default_val) {
    uintptr_t count = self->count;
    while (count > 0) {
        if (atomic_compare_and_swap(&self->count, count, count - 1)) {
            uintptr_t read_ptr = atomic_exchange_add(&self->read, 1);
            intptr_t retval = self->data[read_ptr & self->mask];
            atomic_exchange_add(&self->free, 1);
            return retval;
        } else {
            count = self->count;
            io_pause();
        }
    }
    return default_val;
}

int moe_ring_buffer_write(moe_ring_buffer_t* self, intptr_t data) {
    uintptr_t free = self->free;
    while (free > 0) {
        if (atomic_compare_and_swap(&self->free, free, free - 1)) {
            uintptr_t write_ptr = atomic_exchange_add(&self->write, 1);
            self->data[write_ptr & self->mask] = data;
            atomic_exchange_add(&self->count, 1);
            return 0;
        } else {
            free = self->free;
            io_pause();
        }
    }
    return -1;
}


/*********************************************************************/


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

uintptr_t mm_init(void * efi_rt, moe_bootinfo_mmap_t* mmap) {

    static_start = ROUNDUP_4K((uintptr_t)static_heap);

    EFI_RUNTIME_SERVICES* rt = (EFI_RUNTIME_SERVICES*)efi_rt;
    uint8_t* mmap_ptr = (uint8_t*)mmap->mmap;
    int n_mmap = mmap->size / mmap->desc_size;
    for (int i = 0; i < n_mmap; i++) {
        EFI_MEMORY_DESCRIPTOR* efi_mem = (EFI_MEMORY_DESCRIPTOR*)(mmap_ptr + i * mmap->desc_size);
        efi_mem->VirtualStart = efi_mem->PhysicalStart;
        if (efi_mem->PhysicalStart >= 0x100000) {
            moe_mmap mem = { efi_mem->PhysicalStart, efi_mem->NumberOfPages*0x1000, efi_mm_type_convert(efi_mem->Type) };
            if (mem.type > 0) {
                total_memory += mem.size;
            }
        }
    }
    rt->SetVirtualAddressMap(mmap->size, mmap->desc_size, mmap->desc_version, mmap->mmap);

    return total_memory;
}
