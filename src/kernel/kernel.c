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


extern void arch_init();
extern void acpi_init(acpi_rsd_ptr_t* rsd);
extern void gs_init(moe_dib_t* screen);
extern void mm_init(moe_bootinfo_mmap_t* mmap);
extern void lpc_init();
// extern void hid_init();
extern void window_init();

extern void acpi_enter_sleep_state(int state);
extern void acpi_reset();

extern void display_threads();

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


_Noreturn void moe_reboot() {
    acpi_reset();
    gRT->ResetSystem(EfiResetWarm, 0, 0, NULL);
}

_Noreturn void moe_shutdown_system() {
    acpi_enter_sleep_state(5);
    gRT->ResetSystem(EfiResetShutdown, 0, 0, NULL);
}


uint64_t fw_get_time() {
    EFI_TIME etime;
    gRT->GetTime(&etime, NULL);
    return 1000000LL * (etime.Second + etime.Minute * 60 + etime.Hour * 3600) + (etime.Nanosecond / 1000);
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


extern _Noreturn void start_init(void* args);
_Noreturn void start_kernel(moe_bootinfo_t* bootinfo) {

    gRT = bootinfo->efiRT;
    gs_init(&bootinfo->screen);
    mm_init(&bootinfo->mmap);
    acpi_init(bootinfo->acpi);
    arch_init();
    window_init();
    lpc_init();

    moe_create_thread(&start_init, 0, 0, "init");

    //  Do Idle
    for (;;) io_hlt();

}
