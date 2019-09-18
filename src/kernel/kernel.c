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
extern void xhci_init(void);
extern void pg_enter_strict_mode(void);
extern void hid_init(void);
extern void shell_start(const wchar_t *cmdline);

extern int vsnprintf(char *buffer, size_t limit, const char *format, va_list args);
extern int putchar(int);
extern void gs_bsod(void);


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

void _zprint(const char *s, size_t count) {
    for (size_t i = 0; i < count; i++) {
        putchar(s[i]);
    }
}

#define PRINTF_BUFFER_SIZE 0x1000
static char printf_buffer[PRINTF_BUFFER_SIZE];
static moe_semaphore_t *sem_zpf;

int vprintf(const char *format, va_list args) {
    if (sem_zpf) moe_sem_wait(sem_zpf, MOE_FOREVER);
    int count = vsnprintf(printf_buffer, PRINTF_BUFFER_SIZE, format, args);
    _zprint(printf_buffer, count);
    if (sem_zpf) moe_sem_signal(sem_zpf);
    return count;
}

void _zputs(const char *s) {
    _zprint(s, strlen(s));
}


/*********************************************************************/

moe_bootinfo_t bootinfo;

void *moe_kname(char *buffer, size_t limit) {
    snprintf(buffer, limit, "MEG-OS v0.7.0 (codename neapolis) [%d Cores, Memory %dMB]\n",
        moe_get_number_of_active_cpus(), (int)(bootinfo.total_memory >> 8));
    return buffer;
}

void sysinit(void *args) {
    moe_bootinfo_t *info = args;

    hid_init();
    xhci_init();

    MOE_PHYSICAL_ADDRESS pa_cmdline = info->cmdline;
    shell_start(pa_cmdline ? MOE_PA2VA(pa_cmdline) : NULL);
}

static _Noreturn void start_kernel() {

    mm_init(&bootinfo);
    page_init(&bootinfo);
    gs_init(&bootinfo);
    acpi_init((void *)bootinfo.acpi);
    arch_init(&bootinfo);
    pg_enter_strict_mode();
    sem_zpf = moe_sem_create(1);

    moe_create_process(&sysinit, 0, &bootinfo, "sysinit");
    // moe_create_thread(&sysinit, 0, NULL, "sysinit");

    // Idle thread
    for (;;) io_hlt();
}

/*********************************************************************/

_Noreturn void efi_main(moe_bootinfo_t *info) {
    // UEFI stub is no longer supported
    bootinfo = *info;
    start_kernel();
}
