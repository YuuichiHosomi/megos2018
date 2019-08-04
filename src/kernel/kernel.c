// Minimal OS Kernel
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include "moe.h"
#include "kernel.h"


extern void mm_init(moe_bootinfo_t *bootinfo);
extern void gs_init(moe_bitmap_t *screen);
extern void arch_init();


_Noreturn void moe_bsod(const char *message) {

    for (const char *p = message; *p; p++) {
        putchar(*p);
    }

    for (;;) { __asm__ volatile("hlt"); }
}


moe_bootinfo_t bootinfo;

_Noreturn void start_kernel(moe_bootinfo_t* info) {

    mm_init(info);

    moe_bitmap_t main_screen = {0};
    main_screen.width = info->screen.width;
    main_screen.height = info->screen.height;
    main_screen.delta = info->screen.delta;
    main_screen.bitmap = (void *)info->vram_base;
    gs_init(&main_screen);

    arch_init();

    printf("MEG-OS ver 0.6.0 (codename WARBLER) [Memory %dMB]\n\n",
        (int)(info->total_memory >> 8));

    printf("Hello, world!\n");

    __asm__ volatile("movl %%eax, (%0)":: "r"(0xdeadbeef));

    for (;;) { __asm__ volatile("hlt"); }
}
