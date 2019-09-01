// MEG-OS Kernel
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

#include "moe.h"
#include <stdarg.h>
#include "kernel.h"


extern void acpi_init(void *);
extern void arch_init(moe_bootinfo_t* info);
extern void gs_init(moe_bootinfo_t *bootinfo);
extern void mm_init(moe_bootinfo_t *bootinfo);
extern void page_init(moe_bootinfo_t *bootinfo);
extern void xhci_init();
extern void pg_enter_strict_mode();
extern void hid_init();
extern void sysinit(void *);

extern int vprintf(const char *format, va_list args);
extern int putchar(int);
extern void gs_bsod();


/*********************************************************************/


_Noreturn void _zpanic(const char* file, uintptr_t line, ...) {
    gs_bsod();
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

_Noreturn void moe_reboot() {
    acpi_reset();
    arch_reset();
    for (;;) io_hlt();
}

_Noreturn void moe_shutdown_system() {
    acpi_enter_sleep_state(5);
    moe_usleep(5);
    moe_reboot();
}

static moe_semaphore_t *sem_zpf;
int _zprintf(const char *format, ...) {
    va_list list;
    va_start(list, format);
    moe_sem_wait(sem_zpf, MOE_FOREVER);
    int retval = vprintf(format, list);
    moe_sem_signal(sem_zpf);
    va_end(list);
    return retval;
}

void _zputs(const char *string) {
    moe_sem_wait(sem_zpf, MOE_FOREVER);
    for (int i = 0; string[i]; i++) {
        putchar(string[i]);
    }
    moe_sem_signal(sem_zpf);
}


/*********************************************************************/

moe_bootinfo_t bootinfo;

void *moe_kname(char *buffer, size_t limit) {
    snprintf(buffer, limit, "MEG-OS v0.6.2 (codename warbler) [%d Cores, Memory %dMB]\n",
        moe_get_number_of_active_cpus(), (int)(bootinfo.total_memory >> 8));
    return buffer;
}

static _Noreturn void start_kernel() {

    mm_init(&bootinfo);
    page_init(&bootinfo);
    gs_init(&bootinfo);
    acpi_init((void *)bootinfo.acpi);
    arch_init(&bootinfo);
    pg_enter_strict_mode();
    sem_zpf = moe_sem_create(1);

    hid_init();
    xhci_init();

    moe_create_thread(&sysinit, 0, NULL, "sysinit");

    // Idle thread
    for (;;) io_hlt();
}

/*********************************************************************/

_Noreturn void efi_main(moe_bootinfo_t *info) {
    // UEFI stub is no longer supported
    bootinfo = *info;
    start_kernel();
}
