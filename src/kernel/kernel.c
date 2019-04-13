// Minimal Operating Environment - Kernel
// Copyright (c) 1998,2002,2018 MEG-OS project, All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
#include "moe.h"
#include "kernel.h"
#include "efi.h"


#define VER_SYSTEM_NAME     "MOE"
#define VER_STRING          "v0.5.5"

extern void acpi_init(acpi_rsd_ptr_t* rsd);
extern void arch_init();
extern void gs_init(moe_dib_t* screen);
extern void hid_init();
extern void mm_init(moe_bootinfo_t *bootinfo);
extern void window_init();
extern void xhci_init();

_Noreturn void arch_do_reset();
extern char *strchr(const char *s, int c);
extern int vprintf(const char *format, va_list args);

EFI_RUNTIME_SERVICES* gRT;


/*********************************************************************/

void memset32(uint32_t* p, uint32_t v, size_t n) {
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < n; i++) {
        *p++ = v;
    }
}


const char *moe_kname() {
    return VER_SYSTEM_NAME " " VER_STRING;
}

_Noreturn void moe_reboot() {
    acpi_reset();
    if (gRT) gRT->ResetSystem(EfiResetWarm, 0, 0, NULL);
    arch_do_reset();
}

_Noreturn void moe_shutdown_system() {
    acpi_enter_sleep_state(5);
    if (gRT) gRT->ResetSystem(EfiResetShutdown, 0, 0, NULL);
    moe_usleep(3000000);
    moe_reboot();
}


static uint64_t base_time;
uint64_t fw_get_time() {
    return base_time + moe_get_measure();
}


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


extern uintptr_t total_memory;
extern int n_active_cpu;
extern _Noreturn void start_init(void* args);
_Noreturn void start_kernel(moe_bootinfo_t *info) {

    EFI_TIME etime = *(EFI_TIME *)(&info->boottime);
    base_time = 1000000LL * (etime.Second + etime.Minute * 60 + etime.Hour * 3600) + (etime.Nanosecond / 1000);

    gRT = 0; //bootinfo->efiRT;
    mm_init(info);

    moe_dib_t main_screen = {0};
    main_screen.width = info->screen.width;
    main_screen.height = info->screen.height;
    main_screen.delta = info->screen.delta;
    main_screen.dib = pg_map_vram(info->vram_base, main_screen.delta * info->screen.height * 4);
    gs_init(&main_screen);

    acpi_init((void *)info->acpi);
    arch_init();
    hid_init();

    printf("%s [%d Cores, Memory %d MB]\n", moe_kname(), n_active_cpu, (int)(total_memory >> 8));

    // xhci_init();
    window_init();

    moe_create_thread(&start_init, 0, 0, "init");

    //  Do Idle
    for (;;) io_hlt();

}
