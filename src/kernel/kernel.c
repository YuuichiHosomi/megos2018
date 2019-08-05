// Minimal OS Kernel
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include "moe.h"
#include "kernel.h"


extern void acpi_init(void *);
extern void arch_init(moe_bootinfo_t* info);
extern void gs_init(moe_bootinfo_t *bootinfo);
extern void mm_init(moe_bootinfo_t *bootinfo);
extern void page_init(moe_bootinfo_t *bootinfo);

extern char *strchr(const char *s, int c);
extern int vprintf(const char *format, va_list args);
extern int putchar(int);

void moe_assert(const char* file, uintptr_t line, ...) {
    // mgs_bsod();
    va_list list;
    va_start(list, line);

    const char* msg = va_arg(list, const char*);
    if (strchr(msg, '%')) {
        printf("%s(%zu):", file, line);
        vprintf(msg, list);
    } else {
        printf("%s(%zu): %s\n", file, line, msg);
    }

    va_end(list);
    // __asm__ volatile("int3");
    // for (;;) io_hlt();
}

_Noreturn void moe_bsod(const char *message) {

    for (const char *p = message; *p; p++) {
        putchar(*p);
    }

    for (;;) { __asm__ volatile("hlt"); }
}


moe_bootinfo_t bootinfo;

_Noreturn void start_kernel(moe_bootinfo_t* info) {

    mm_init(info);
    page_init(info);
    gs_init(info);
    acpi_init((void *)info->acpi);
    arch_init(info);

    printf("MEG-OS v0.6.0 (codename warbler) [Memory %dMB]\n\n",
        (int)(info->total_memory >> 8));

    printf("Hello, world!\n");

    for (int i = 0; i < 5; i++) {
        putchar('.');
        moe_usleep(1000000);
    }

    __asm__ volatile("movl %%eax, (%0)":: "r"(0xdeadbeef));

    for (;;) { __asm__ volatile("hlt"); }
}
