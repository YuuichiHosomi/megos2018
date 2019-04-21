// Minimal Memory Management Subsystem
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "efi.h"

#define PAGE_SIZE   0x1000
#define ROUNDUP_PAGE(n) ((n + 0xFFF) & ~0xFFF)

extern void page_init();

typedef struct {
    uintptr_t base;
    uintptr_t size;
    uintptr_t type;
} moe_mmap_t;


uintptr_t total_memory = 0;
static _Atomic uintptr_t free_memory;
static _Atomic uintptr_t static_start;


// "640 k ought to be enough for anybody." - Bill Gates, 1981
_Atomic uint32_t gates_memory_bitmap[MAX_GATES_INDEX];

static int popcnt32(uint32_t bits) {
    bits = (bits & 0x55555555) + (bits >> 1 & 0x55555555);
    bits = (bits & 0x33333333) + (bits >> 2 & 0x33333333);
    bits = (bits & 0x0f0f0f0f) + (bits >> 4 & 0x0f0f0f0f);
    bits = (bits & 0x00ff00ff) + (bits >> 8 & 0x00ff00ff);
    return (bits & 0x0000ffff) + (bits >>16 & 0x0000ffff);
}

uintptr_t moe_alloc_gates_memory() {
    for (int i = 1; i < 255; i++) {
        uintptr_t offset = i / 32;
        uintptr_t position = i % 32;
        if (atomic_bit_test_and_clear(gates_memory_bitmap + offset, position)) {
            return i << 10;
        }
    }
    return 0;
}


int get_free_gates_memory() {
    int result = 0;
    for (int i = 0; i < MAX_GATES_INDEX; i++) {
        uint32_t n = gates_memory_bitmap[i];
        result += popcnt32(n);
    }
    return result;
}


uintptr_t moe_alloc_physical_page(size_t n) {
    uintptr_t size = ROUNDUP_PAGE(n);
    uintptr_t free = atomic_load(&free_memory);
    while (free > size) {
        if (atomic_compare_exchange_strong(&free_memory, &free, free - size)) {
            uintptr_t result = atomic_fetch_add(&static_start, size);
            return result;
        } else {
            io_pause();
        }
    }
    return 0;
}

void *moe_alloc_object(size_t size, size_t count) {
    size_t sz = size * count;
    void *va = NULL;
    uintptr_t pa = moe_alloc_physical_page(sz);
    if (pa) {
        va = pg_valloc(pa, sz);
        memset(va, 0, sz);
    }
    return va;
}


/*********************************************************************/

static uintptr_t kma_base, kma_size = 0x1000;

void mm_init(moe_bootinfo_t *bootinfo) {
    static_start = bootinfo->static_start;
    free_memory = bootinfo->free_memory;
    total_memory = bootinfo->total_memory;
    memset32((void*)static_start, 0xdeadbeef, free_memory / 4);

    memcpy(gates_memory_bitmap, bootinfo->gates_memory_bitmap, sizeof(gates_memory_bitmap));

    page_init(bootinfo);
}

int cmd_mem(int argc, char **argv) {
    printf("Memory: %d MB, Real: %d KB\n", (int)(total_memory >> 8), get_free_gates_memory() * 4);
    printf("Kernel: %08zx-%08zx %zuKB/%zuKB (%zuMB)\n",
    kma_base, (kma_base + kma_size * PAGE_SIZE - 1), free_memory /1024, kma_size * PAGE_SIZE / 1024,
    (kma_size * PAGE_SIZE) >> 20);
    return 0;
}
