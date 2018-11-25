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
#define VER_SYSTEM_NAME_SHORT   "MOE"
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

    int old_cursor_state = moe_set_cursor_enabled(NULL, 1);
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
    moe_set_cursor_enabled(NULL, old_cursor_state);
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
_Noreturn void statusbar_thread(void *args) {

    uint32_t statusbar_bgcolor = 0xFFFFFF;
    uint32_t fgcolor = 0x555555;

    moe_rect_t rect_statusbar = {{0, 0}, {desktop_dib->width, 22}};
    moe_dib_t *statusbar_dib = moe_create_dib(&rect_statusbar.size, 0, statusbar_bgcolor);
    moe_view_t *statusbar = moe_create_view(NULL, statusbar_dib, BORDER_BOTTOM | window_level_higher);

    const size_t size_buff = 256;
    char buff[size_buff];

    int width_clock = 8 * 8, height = 20, padding_x = 8, padding_y = 1;
    moe_rect_t rect_c = { {desktop_dib->width - width_clock - padding_x, padding_y}, {width_clock, height} };

    int width_usage = 6 * 8;
    moe_rect_t rect_u = {{rect_c.origin.x - padding_x - width_usage, padding_y}, {width_usage, height}};

    EFI_TIME etime;
    gRT->GetTime(&etime, NULL);
    uint64_t time_base = 1000000LL * (etime.Second + etime.Minute * 60 + etime.Hour * 3600) + (etime.Nanosecond / 1000) - moe_get_measure();

    {
        moe_point_t origin = {4, padding_y};
        moe_draw_string(statusbar_dib, &origin, NULL, VER_SYSTEM_NAME_SHORT, fgcolor);
        // moe_draw_string(statusbar_dib, &origin, NULL, "[Start] <- HAJIMERU TOKI HA START WO OSU", fgcolor);
    }

    moe_add_view(statusbar);

    moe_rect_t rect_redraw = {{rect_u.origin.x, 0}, {rect_statusbar.size.width - rect_u.origin.x, rect_statusbar.size.height - 2}};

    for (;;) {
        moe_fill_rect(statusbar_dib, &rect_redraw, statusbar_bgcolor);

        uint32_t now = ((time_base + moe_get_measure()) / 1000000LL);
        unsigned time0 = now % 60;
        unsigned time1 = (now / 60) % 60;
        unsigned time2 = (now / 3600) % 100;
        snprintf(buff, size_buff, "%02d:%02d:%02d", time2, time1, time0);
        moe_draw_string(statusbar_dib, NULL, &rect_c, buff, fgcolor);

        int usage = moe_get_usage();
        int usage0 = usage % 10;
        int usage1 = usage / 10;
        snprintf(buff, size_buff, "%3d.%1d%%", usage1, usage0);
        moe_draw_string(statusbar_dib, NULL, &rect_u, buff, fgcolor);

        moe_invalidate_view(statusbar, &rect_redraw);
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

void cmd_ver() {
    printf("%s v%d.%d.%d\n", VER_SYSTEM_NAME, VER_SYSTEM_MAJOR, VER_SYSTEM_MINOR, VER_SYSTEM_REVISION);
}

extern void console_init(moe_console_context_t *self, moe_view_t* view, moe_dib_t *dib, const moe_edge_insets_t* insets);
_Noreturn void start_init(void* args) {

    // TODO: Waiting for initializing window manager
    while (!desktop_dib) moe_yield();

    moe_create_thread(&statusbar_thread, 0, 0, "Statusbar");

    // //  Show BGRT (Boot Graphics Resource Table) from ACPI
    // acpi_bgrt_t* bgrt = acpi_find_table(ACPI_BGRT_SIGNATURE);
    // if (bgrt) {
    //     draw_logo_bitmap(desktop_dib, (uint8_t*)bgrt->Image_Address, bgrt->Image_Offset_X, bgrt->Image_Offset_Y);
    // }

    moe_view_t* splash;

    //  Splash window
    {
        const size_t buff_size = 256;
        char *buff = mm_alloc_static(buff_size);
        moe_size_t size = {320, 240};
        moe_rect_t frame;
        frame.origin.x = (desktop_dib->width - size.width) / 2;
        frame.origin.y = (desktop_dib->height - size.height) /2;
        frame.size = size;
        snprintf(buff, buff_size, "%s v%d.%d.%d\nMemory %dMB, %d Active Cores\n", VER_SYSTEM_NAME, VER_SYSTEM_MAJOR, VER_SYSTEM_MINOR, VER_SYSTEM_REVISION, (int)(total_memory >> 8), n_active_cpu);

        moe_dib_t *dib = moe_create_dib(&size, 0, 0xFFFFFF);
        splash = moe_create_view(&frame, dib, BORDER_ALL | window_level_popup);
        moe_point_t cursor = {4, 180};
        moe_point_t cursor_shadow = {5, 181};
        moe_rect_t client_rect = {{4, 4}, {size.width - 8, size.height - 28}};
        moe_draw_string(dib, &cursor_shadow, &client_rect, buff, 0xAAAAAA);
        moe_draw_string(dib, &cursor, &client_rect, buff, 0x000000);

        moe_add_view(splash);
    }
    moe_usleep(1000000);

    // Init root console
    {
        uint32_t console_attributes = 0xF8;
        moe_rect_t frame = {{16, 32}, {640, 480}};
        moe_edge_insets_t insets = { 24, 4, 4, 4 };
        moe_dib_t *dib = moe_create_dib(&frame.size, 0, 0xFFFFFF);
        moe_view_t *view = moe_create_view(&frame, dib, BORDER_ALL);
        moe_add_view(view);
        console_init(NULL, view, dib, &insets);
        moe_set_console_attributes(NULL, console_attributes);
    }

    moe_usleep(1000000);
    moe_remove_view(splash);

    // for (int i = 0; i < 5; i++){
    //     moe_create_thread(&demo_thread, 0, (void *)(intptr_t)i, "DEMO");
    // }

    //  Pseudo shell
    {
        const size_t cmdline_size = 80;
        char* cmdline = mm_alloc_static(cmdline_size);

        cmd_ver();
        // printf("Hello, world!\n");

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
                    cmd_ver();
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
