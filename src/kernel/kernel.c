// Minimal OS Kernel
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include "moe.h"
#include "kernel.h"


extern void gs_init(moe_bitmap_t *screen);

moe_bootinfo_t bootinfo;

_Noreturn void start_kernel(moe_bootinfo_t* info) {

    moe_bitmap_t main_screen = {0};
    main_screen.width = info->screen.width;
    main_screen.height = info->screen.height;
    main_screen.delta = info->screen.delta;
    main_screen.bitmap = (void *)info->vram_base;
    gs_init(&main_screen);

    printf("MEG-OS ver 0.6.0 (codename warbler) [Memory %dMB]\n\n",
        (int)(info->total_memory >> 8));

    printf("Hello, world!\n");

    for (;;) { __asm__ volatile("hlt"); }
}
