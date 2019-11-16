// Minimal Memory Manager
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

#include "moe.h"
#include "kernel.h"


uintptr_t total_memory = 0;
static _Atomic uintptr_t free_memory;
static _Atomic uintptr_t static_start;
static _Atomic uint32_t gates_memory_bitmap[MAX_GATES_INDEX];

static uintptr_t ceil_pagesize(size_t n) {
    return (((n) + 0xFFF) & ~0xFFF);
}


// Reference counting object
moe_shared_t *moe_retain(moe_shared_t *self) {
    if (!self) return NULL;
    int delta = 1;
    int ref_cnt = self->ref_cnt;
    while (ref_cnt > 0) {
        if (atomic_compare_exchange_weak(&self->ref_cnt, &ref_cnt, ref_cnt + delta)) {
            return self;
        } else {
            cpu_relax();
        }
    }
    return NULL;
}

void moe_release(moe_shared_t *self, MOE_DEALLOC dealloc) {
    if (!self) return;
    int old_cnt = atomic_fetch_add(&self->ref_cnt, -1);
    if (old_cnt == 1) {
        if (dealloc) {
            dealloc(self->context);
        }
    } else if (old_cnt <= 0) {
        // TODO: double free
    }
}


uintptr_t moe_alloc_gates_memory() {
    for (uintptr_t i = 1; i < 160; i++) {
        if (atomic_bit_test_and_clear(gates_memory_bitmap, i)) {
            return i << 12;
        }
    }
    return 0;
}

uintptr_t moe_alloc_physical_page(size_t n) {
    uintptr_t size = ceil_pagesize(n);
    uintptr_t free = atomic_load(&free_memory);
    while (free > size) {
        if (atomic_compare_exchange_strong(&free_memory, &free, free - size)) {
            uintptr_t result = atomic_fetch_add(&static_start, size);
            return result;
        } else {
            cpu_relax();
        }
    }
    return 0;
}

void *moe_alloc_object(size_t size, size_t count) {
    size_t sz = ceil_pagesize(size * count);
    void *va = NULL;
    uintptr_t pa = moe_alloc_physical_page(sz);
    if (pa) {
        va = pg_valloc(pa, sz);
        memset(va, 0, sz);
    }
    return va;
}


uintptr_t moe_alloc_io_buffer(size_t size) {
    size_t sz = ceil_pagesize(size);
    uintptr_t pa = moe_alloc_physical_page(sz);
    if (pa) {
        void *va = MOE_PA2VA(pa);
        memset(va, 0, sz);
    }
    return pa;
}


void mm_init(moe_bootinfo_t *bootinfo) {
    static_start = bootinfo->static_start;
    free_memory = bootinfo->free_memory;
    total_memory = bootinfo->total_memory;
    // memset32((void*)static_start, 0xdeadbeef, free_memory / 4);

    memcpy(gates_memory_bitmap, bootinfo->gates_memory_bitmap, sizeof(gates_memory_bitmap));
}
