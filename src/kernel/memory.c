// Minimal Memory Manager
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"


uintptr_t total_memory = 0;
static _Atomic uintptr_t free_memory;
static _Atomic uintptr_t static_start;

static uintptr_t ceil_pagesize(size_t n) {
    return (((n) + 0xFFF) & ~0xFFF);
}

uintptr_t moe_alloc_physical_page(size_t n) {
    uintptr_t size = ceil_pagesize(n);
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
    size_t sz = ceil_pagesize(size * count);
    void *va = NULL;
    uintptr_t pa = moe_alloc_physical_page(sz);
    if (pa) {
        va = (void *)pa;
        // va = pg_valloc(pa, sz);
        // zmemset(va, sz);
        memset(va, 0, sz);
    }
    return va;
}

void mm_init(moe_bootinfo_t *bootinfo) {
    static_start = bootinfo->static_start;
    free_memory = bootinfo->free_memory;
    total_memory = bootinfo->total_memory;
    // memset32((void*)static_start, 0xdeadbeef, free_memory / 4);

    // memcpy(gates_memory_bitmap, bootinfo->gates_memory_bitmap, sizeof(gates_memory_bitmap));

    // page_init(bootinfo);
}
