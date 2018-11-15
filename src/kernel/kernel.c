// Minimal Operating Environment - Kernel
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
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


#define VER_SYSTEM_NAME     "Minimal Operating Environment"
#define VER_SYSTEM_MAJOR    0
#define VER_SYSTEM_MINOR    4
#define VER_SYSTEM_REVISION 1


extern void arch_init();
extern void acpi_init(acpi_rsd_ptr_t* rsd);
extern void mgs_init(moe_video_info_t* _video);
extern void mm_init(moe_bootinfo_mmap_t* mmap);
extern void thread_init();
extern void hid_init();
extern void mwm_init();

extern void display_threads();

EFI_RUNTIME_SERVICES* gRT;


/*********************************************************************/

void memset32(uint32_t* p, uint32_t v, size_t n) {
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < n; i++) {
        *p++ = v;
    }
}

/*********************************************************************/

int rgb32_to_luminance(uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return ((r * 19589 + g * 38444 + b * 7502) + 32767) >> 16;
}

void draw_logo_bitmap(moe_dib_t* screen, const uint8_t* bmp, int offset_x, int offset_y) {
    uintptr_t offset = *((uint32_t*)(bmp + 10));
    int bmp_w = *((uint32_t*)(bmp + 18));
    int bmp_h = *((uint32_t*)(bmp + 22));
    int bmp_bpp = *((uint16_t*)(bmp + 28));
    int bmp_bpp8 = (bmp_bpp + 7) / 8;
    int bmp_ppl = (bmp_bpp8 * bmp_w + 3) & 0xFFFFFFFC;
    int delta = screen->delta;
    const uint8_t* dib = bmp+offset;

    if (offset_x < 0) offset_x = (screen->width - bmp_w) / 2;
    if (offset_y < 0) offset_y = (screen->height - bmp_h) / 2;

    moe_rect_t rect = { { offset_x, offset_y}, {bmp_w, bmp_h} };
    uint32_t* vram = screen->dib;
    vram += offset_x + offset_y * delta;

    switch (bmp_bpp) {
        case 24:
        case 32:
            for (int i = bmp_h-1; i >= 0; i--) {
                const uint8_t* p = dib + (i * bmp_ppl);
                for (int j = 0; j < bmp_w; j++) {
                    uint32_t rgb = (p[j * bmp_bpp8 + 0]) + (p[j * bmp_bpp8 + 1] * 0x100) + (p[j * bmp_bpp8 + 2] * 0x10000);
                    if (rgb) {
                        uint8_t b = 255 - rgb32_to_luminance(rgb);
                        vram[j] = b + (b<<8) + (b<<16);
                    }
                }
                vram += delta;
            }
            break;

        case 8:
            for (int i = bmp_h-1; i >= 0; i--) {
                const uint8_t* p = dib + (i * bmp_ppl);
                for (int j = 0; j < bmp_w; j++) {
                    float alpha = p[j * bmp_bpp8 + 0] / 255.0;
                    uint8_t* vram8 = (uint8_t*)(vram+j);
                    for (int k = 0; k < 3; k++) {
                        vram8[k] = vram8[k] * alpha;
                    }
                }
                vram += delta;
            }
            break;
    }
    moe_invalidate_screen(&rect);
}


void dump_madt(uint8_t* p, size_t n) {
    for (int i = 0; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

extern int putchar(char);
extern int getchar();

int read_cmdline(char* buffer, size_t max_len) {
    int cont_flag = 1;
    int len = 0, limit = max_len - 1;

    while (cont_flag) {
        uint32_t c = getchar();
        switch (c) {
            case '\x08': // bs
            case '\x7F': // del
                if (len > 0) {
                    len--;
                    printf("\b \b");
                    if (buffer[len] < 0x20) { // ^X
                        printf("\b \b");
                    }
                }
                break;

            case '\x0D': // cr
                cont_flag = 0;
                break;
            
            default:
                if (len < limit) {
                    if (c < 0x80) {
                        buffer[len++] = c;
                        if (c < 0x20) { // ^X
                            printf("^%c", c | 0x40);
                        } else {
                            putchar(c);
                        }
                    } else { // non ascii
                        printf("?+%04x", c);
                    }
                }
                break;
        }
    }
    buffer[len] = '\0';
    printf("\n");
    return len;
}

extern uintptr_t total_memory;
extern moe_video_info_t* video;

void moe_ctrl_alt_del() {
    gRT->ResetSystem(EfiResetWarm, 0, 0, NULL);
}

_Noreturn void fpu_thread(void* args) {
    double count = 0.0;
    double pi = 3.14;
    int pid = moe_get_current_thread();
    for (;;) {
        count += pi * pid;
        int b = (int)count;
        mgs_fill_rect(pid * 10, 2, 8, 8, b);
        moe_yield();
    }
}


void io_out8(uint16_t port, uint8_t val);
void acpi_enable(int enabled) {
    acpi_fadt_t* fadt = acpi_enum_table_entry(0);
    if (fadt->SMI_CMD) {
        if (enabled) {
            io_out8(fadt->SMI_CMD, fadt->ACPI_ENABLE);
        } else {
            io_out8(fadt->SMI_CMD, fadt->ACPI_DISABLE);
        }
    }
}

extern moe_dib_t *desktop_dib;
_Noreturn void start_init(void* args) {

    // TODO: Waiting for initializing window manager
    if (!desktop_dib) moe_yield();

    mgs_fill_rect( 50,  50, 300, 300, 0xFF77CC);
    mgs_fill_rect(150, 150, 300, 300, 0x77FFCC);
    mgs_fill_rect(250, 100, 300, 300, 0x77CCFF);

    //  Show BGRT (Boot Graphics Resource Table) from ACPI
    acpi_bgrt_t* bgrt = acpi_find_table(ACPI_BGRT_SIGNATURE);
    if (bgrt) {
        draw_logo_bitmap(desktop_dib, (uint8_t*)bgrt->Image_Address, bgrt->Image_Offset_X, bgrt->Image_Offset_Y);
    }

    printf("%s v%d.%d.%d [Memory %dMB]\n", VER_SYSTEM_NAME, VER_SYSTEM_MAJOR, VER_SYSTEM_MINOR, VER_SYSTEM_REVISION, (int)(total_memory >> 8));
    // printf("Hello, world!\n");

    for (int i = 0; i < 5; i++){
        moe_create_thread(&fpu_thread, 0, "FPU DEMO");
    }

    // display_threads();

    //  Pseudo shell
    {
        const size_t cmdline_size = 80;
        char* cmdline = mm_alloc_static(cmdline_size);

        // EFI_TIME time;
        // gRT->GetTime(&time, NULL);
        // printf("Current Time: %d-%02d-%02d %02d:%02d:%02d %04d\n", time.Year, time.Month, time.Day, time.Hour, time.Minute, time.Second, time.TimeZone);

        // printf("Checking Timer...");
        moe_usleep(1000000);
        // printf("Ok\n");

        for (;;) {
            printf("# ");
            read_cmdline(cmdline, cmdline_size);

            switch (cmdline[0]) {
                case 0:
                break;

                case '!':
                {
                    volatile uintptr_t* p = (uintptr_t*)0x7777deadbeef;
                    *p = *p;
                }
                    break;

                case 'h':
                    printf("mini shell commands:\n");
                    printf("  v   display Version\n");
                    printf("  t   display Thread list\n");
                    printf("  c   Clear screen\n");
                    printf("  f   add FPU test thread (experimental)\n");
                    printf("  a   display Acpi table (experimental)\n");
                    printf("  m   display Madt table (experimental)\n");
                    break;

                case 'v':
                    printf("%s v%d.%d.%d [Memory %dMB]\n", VER_SYSTEM_NAME, VER_SYSTEM_MAJOR, VER_SYSTEM_MINOR, VER_SYSTEM_REVISION, (int)(total_memory >> 8));
                    break;

                case 'c':
                    mgs_cls();
                    break;

                case 't':
                    display_threads();
                    break;

                case 'f':
                    moe_create_thread(&fpu_thread, 0, "FPU");
                    printf("FPU Thread started\n");
                    break;

                case 'r':
                    gRT->ResetSystem(EfiResetWarm, 0, 0, NULL);
                    break;

                case 'q':
                    gRT->ResetSystem(EfiResetShutdown, 0, 0, NULL);
                    break;

                case 'a':
                {
                    switch(cmdline[1]) {
                        case '0':
                            acpi_enable(0);
                            break;
                        case '1':
                            acpi_enable(1);
                            break;
                        default:
                            int n = acpi_get_number_of_table_entries();
                            printf("ACPI Tables: %d\n", n);
                            for (int i = 0; i < n; i++) {
                                acpi_header_t* table = acpi_enum_table_entry(i);
                                if (table) {
                                    printf("%p: %.4s %d\n", (void*)table, table->signature, table->length);
                                }
                            }
                            break;
                    }
                    break;
                }

                case 'm':
                {
                    acpi_madt_t* madt = acpi_find_table(ACPI_MADT_SIGNATURE);
                    if (madt) {
                        printf("Dump of MADT:\n");
                        size_t max_length = madt->Header.length - 44;
                        uint8_t* p = madt->Structure;
                        for (size_t loc = 0; loc < max_length; ) {
                            size_t len = p[loc+1];
                            dump_madt(p+loc, len);
                            loc += len;
                        }
                    }
                    break;
                }

                default:
                    printf("Bad command or file name\n");
                    break;

            }
        }
    }

}

int vprintf(const char *format, va_list args);
void moe_assert(const char* file, uintptr_t line, ...) {
    // mgs_bsod();
	va_list list;
	va_start(list, line);

    printf("ASSERT(File %s Line %zu):", file, line);
    const char* msg = va_arg(list, const char*);
    vprintf(msg, list);

	va_end(list);
    // __asm__ volatile("int3");
    // for (;;) io_hlt();
}

_Noreturn void start_kernel(moe_bootinfo_t* bootinfo) {

    gRT = bootinfo->efiRT;
    mgs_init(&bootinfo->video);
    mm_init(&bootinfo->mmap);
    thread_init();
    acpi_init(bootinfo->acpi);
    arch_init();
    mwm_init();
    hid_init();

    moe_create_thread(&start_init, 0, "main");

    //  Do Idle
    for (;;) io_hlt();

}
