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
extern void pg_enter_strict_mode();

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
    for (;;) io_hlt();
}

// _Noreturn void moe_bsod(const char *message) {
//     for (const char *p = message; *p; p++) {
//         putchar(*p);
//     }
//     for (;;) io_hlt();
// }


/*********************************************************************/

moe_bootinfo_t bootinfo;

_Noreturn void thread_2(void *args) {
    int k = moe_get_current_thread_id();
    int padding = 2;
    int w = 10;
    moe_rect_t rect = {{k * w, padding}, {w - padding, w - padding}};
    uint32_t color = k * 0x010101;
    for (;;) {
        moe_fill_rect(NULL, &rect, color);
        color += k;
        moe_usleep(k);
    }
}

_Noreturn void kernel_thread(void *args) {
    printf("Minimal Operating Environment v0.6.0 (codename warbler) [%d Active Cores, Memory %dMB]\n",
        moe_get_number_of_active_cpus(), (int)(bootinfo.total_memory >> 8));

    printf("\n");
    printf("Hello, world!\n");
    printf("\n");

    for (int i = 0; i < 20; i++) {
        moe_create_thread(&thread_2, 0, NULL, "test");
    }

    for (int i = 0; i < 10; i++) {
        putchar('.');
        moe_usleep(1000000);
    }

    moe_exit_thread(0);
}

_Noreturn void start_kernel() {

    mm_init(&bootinfo);
    page_init(&bootinfo);
    gs_init(&bootinfo);
    acpi_init((void *)bootinfo.acpi);
    arch_init(&bootinfo);

    pg_enter_strict_mode();

    moe_create_thread(&kernel_thread, 0, NULL, "kernel");

    for (;;) io_hlt();
}

/*********************************************************************/

_Noreturn void efi_main(moe_bootinfo_t *info) {
    // TODO: UEFI stub is no longer supported
    bootinfo = *info;
    start_kernel();
}
