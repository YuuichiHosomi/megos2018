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
#define VER_SYSTEM_REVISION 5


extern void arch_init();
extern void acpi_init(acpi_rsd_ptr_t* rsd);
extern void gs_init(moe_dib_t* screen);
extern void mm_init(moe_bootinfo_mmap_t* mmap);
extern void hid_init();
extern void window_init();

extern void display_threads();
extern void cmd_mem();
extern void cmd_win();

extern char *strchr(const char *s, int c);
extern int putchar(char);
extern int getchar();
extern int vprintf(const char *format, va_list args);
extern int snprintf(char* buffer, size_t n, const char* format, ...);

extern uintptr_t total_memory;
extern int n_active_cpu;

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

void moe_ctrl_alt_del() {
    gRT->ResetSystem(EfiResetWarm, 0, 0, NULL);
}

extern moe_dib_t main_screen_dib;
_Noreturn void demo_thread(void *args) {
    const size_t size_buff = 16;
    char buff[size_buff];
    double count = 0.0;
    const double pi = 3.14;
    int pid = (int)args;
    const int width = 48, height = 16, padding_x = 4, padding_y = 2; 
    moe_rect_t rect = {{ padding_x + pid * (width + padding_x * 2), padding_y }, { width, height }};
    for (;;) {
        count += pi * (1 + pid * pid);
        if (count > 0x1000000) count -= 0x1000000;
        uint32_t color = count;
        uint32_t bgcolor = color ^ 0x7F7F7F;
        snprintf(buff, size_buff, "%06x", color);
        moe_fill_rect(&main_screen_dib, &rect, bgcolor);
        moe_draw_string(&main_screen_dib, NULL, &rect, buff, color);
        moe_yield();
    }
}

//  Clock and Statusbar thread
extern moe_dib_t *desktop_dib;
_Noreturn void clock_thread(void *args) {

    uint32_t taskbar_bgcolor = 0xFFFFFF;
    uint32_t taskbar_border_color = 0;
    uint32_t fgcolor = 0x555555;

    moe_rect_t rect_taskbar = {{0, 0}, {desktop_dib->width, 23}};
    moe_dib_t *taskbar_dib = moe_create_dib(&rect_taskbar.size, 0, 0);
    moe_view_t *taskbar = moe_create_view(NULL, taskbar_dib, window_level_higher);
    moe_add_next_view(NULL, taskbar);

    const size_t size_buff = 16;
    char buff[size_buff];

    moe_dib_t *clock_dib = &main_screen_dib;
    int width = 8 * 8, height = 16, padding_x = 12, padding_y = 2;
    moe_rect_t rect_c = { {clock_dib->width - width - padding_x, padding_y}, {width, height} };

    int width_usage = 6 * 8;
    moe_rect_t rect_u = {{rect_c.origin.x - padding_x - width_usage, padding_y}, {width_usage, height}};

    EFI_TIME etime;
    gRT->GetTime(&etime, NULL);
    uint64_t time_base = 1000000LL * (etime.Second + etime.Minute * 60 + etime.Hour * 3600) + (etime.Nanosecond / 1000) - moe_get_measure();

    moe_fill_rect(taskbar_dib, NULL, taskbar_bgcolor);
    moe_rect_t rect0 = {{rect_taskbar.origin.x, rect_taskbar.origin.y + rect_taskbar.size.height -1 },
        {rect_taskbar.size.width, 1}};
    moe_fill_rect(taskbar_dib, &rect0, taskbar_border_color);
    moe_invalidate_screen(&rect_taskbar);

    moe_rect_t rect_redraw = {{rect_u.origin.x, 0}, {rect_taskbar.size.width - rect_u.origin.x, rect_taskbar.size.height - 1}};

    for (;;) {
        moe_fill_rect(taskbar_dib, &rect_redraw, taskbar_bgcolor);

        uint32_t now = ((time_base + moe_get_measure()) / 1000000LL);
        unsigned time0 = now % 60;
        unsigned time1 = (now / 60) % 60;
        unsigned time2 = (now / 3600) % 100;
        snprintf(buff, size_buff, "%02d:%02d:%02d", time2, time1, time0);
        moe_draw_string(taskbar_dib, NULL, &rect_c, buff, fgcolor);

        int usage = moe_get_usage();
        int usage0 = usage % 10;
        int usage1 = usage / 10;
        snprintf(buff, size_buff, "%3d.%1d%%", usage1, usage0);
        moe_draw_string(taskbar_dib, NULL, &rect_u, buff, fgcolor);

        moe_invalidate_screen(&rect_redraw);
        moe_usleep(250000);
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

_Noreturn void start_init(void* args) {

    // TODO: Waiting for initializing window manager
    while (!desktop_dib) moe_yield();

    // mgs_fill_rect( 50,  50, 300, 300, 0xFF77CC);
    // mgs_fill_rect(150, 150, 300, 300, 0x77FFCC);
    // mgs_fill_rect(250, 100, 300, 300, 0x77CCFF);

    //  Show BGRT (Boot Graphics Resource Table) from ACPI
    acpi_bgrt_t* bgrt = acpi_find_table(ACPI_BGRT_SIGNATURE);
    if (bgrt) {
        draw_logo_bitmap(desktop_dib, (uint8_t*)bgrt->Image_Address, bgrt->Image_Offset_X, bgrt->Image_Offset_Y);
    }

    printf("%s v%d.%d.%d [%d Active Cores, Memory %dMB]\n", VER_SYSTEM_NAME, VER_SYSTEM_MAJOR, VER_SYSTEM_MINOR, VER_SYSTEM_REVISION, n_active_cpu, (int)(total_memory >> 8));
    // printf("Hello, world!\n");

    moe_create_thread(&clock_thread, 0, 0, "Clock");

    // for (int i = 0; i < 5; i++){
    //     moe_create_thread(&demo_thread, 0, (void *)(intptr_t)i, "DEMO");
    // }

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
                    printf("  m   display memory info\n");
                    printf("  c   Clear screen\n");
                    printf("  a   display Acpi table (experimental)\n");
                    break;

                case 'v':
                    printf("%s v%d.%d.%d\n", VER_SYSTEM_NAME, VER_SYSTEM_MAJOR, VER_SYSTEM_MINOR, VER_SYSTEM_REVISION);
                    break;

                case 'c':
                    mgs_cls();
                    break;

                case 't':
                    display_threads();
                    break;

                // case 'f':
                // {
                //     int thid = moe_create_thread(&demo_thread, 0, 0, "DEMO");
                //     printf("DEMO Thread started (%d)\n", thid);
                // }
                //     break;

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
                    break;
                }

                case 'm':
                {
                    cmd_mem();
                    break;
                }

                case 'w':
                {
                    cmd_win();
                    // moe_usleep(1000000);
                }
                    break;

                default:
                    printf("Bad command or file name\n");
                    break;

            }
        }
    }

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

_Noreturn void start_kernel(moe_bootinfo_t* bootinfo) {

    gRT = bootinfo->efiRT;
    gs_init(&bootinfo->screen);
    mm_init(&bootinfo->mmap);
    acpi_init(bootinfo->acpi);
    arch_init();

    window_init();
    hid_init();
    moe_create_thread(&start_init, 0, 0, "kernel");

    //  Do Idle
    for (;;) io_hlt();

}
