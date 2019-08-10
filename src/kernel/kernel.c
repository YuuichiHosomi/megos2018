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
    for (;;) io_hlt();
}

_Noreturn void moe_bsod(const char *message) {

    for (const char *p = message; *p; p++) {
        putchar(*p);
    }

    for (;;) io_hlt();
}


/*********************************************************************/

moe_bootinfo_t bootinfo;

_Noreturn void start_kernel() {

    mm_init(&bootinfo);
    page_init(&bootinfo);
    gs_init(&bootinfo);
    acpi_init((void *)bootinfo.acpi);
    arch_init(&bootinfo);

    printf("MEG-OS v0.6.0 (codename warbler) [%d Active Cores, Memory %dMB]\n",
        moe_get_number_of_active_cpus(), (int)(bootinfo.total_memory >> 8));

    printf("\n");
    printf("Hello, world!\n");
    printf("\n");

    for (int i = 0; i < 5; i++) {
        putchar('.');
        moe_usleep(1000000);
    }

    // __asm__ volatile("int3");
    __asm__ volatile("movl %%eax, (%0)":: "r"(0xdeadbeef));

    for (;;) io_hlt();
}

/*********************************************************************/

_Noreturn void efi_main(moe_bootinfo_t *info) {
    // TODO: UEFI stub is no longer supported
    bootinfo = *info;
    start_kernel();
}
