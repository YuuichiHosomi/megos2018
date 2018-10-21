// Minimal Operating Environment - Kernel
// Copyright (c) 1998,2000,2018 MEG-OS project, All rights reserved.
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
#include "efi.h"


#define VER_SYSTEM_NAME     "Minimal Operating Environment"
#define VER_SYSTEM_MAJOR    0
#define VER_SYSTEM_MINOR    3
#define VER_SYSTEM_REVISION 1


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

void draw_logo_bitmap(moe_video_info_t* video, const uint8_t* bmp, int offset_x, int offset_y) {
    uintptr_t offset = *((uint32_t*)(bmp + 10));
    int bmp_w = *((uint32_t*)(bmp + 18));
    int bmp_h = *((uint32_t*)(bmp + 22));
    int bmp_bpp = *((uint16_t*)(bmp + 28));
    int bmp_bpp8 = (bmp_bpp + 7) / 8;
    int bmp_ppl = (bmp_bpp8 * bmp_w + 3) & 0xFFFFFFFC;
    int delta = video->pixel_per_scan_line;
    const uint8_t* dib = bmp+offset;

    if (offset_x < 0) offset_x = (video->res_x - bmp_w) / 2;
    if (offset_y < 0) offset_y = (video->res_y - bmp_h) / 2;

    uint32_t* vram = (uint32_t*)video->vram;
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
}

/*********************************************************************/

#include "setjmp.h"
extern void new_jmpbuf(jmp_buf env, uintptr_t* new_sp);

typedef uintptr_t pid_t;
typedef uintptr_t thid_t;

typedef struct _moe_fiber_t moe_fiber_t;

typedef struct _moe_fiber_t {
    moe_fiber_t* next;
    thid_t  thid;
    pid_t   pid;
    jmp_buf jmpbuf;
} moe_fiber_t;

volatile thid_t next_thid = 1;
moe_fiber_t* current_thread;
moe_fiber_t root_thread;

void moe_switch_context(moe_fiber_t* next) {
    if (!next) next = &root_thread;
    if (!setjmp(current_thread->jmpbuf)) {
        current_thread = next;
        longjmp(next->jmpbuf, 0);
    }
}

void moe_next_thread() {
    moe_switch_context(current_thread->next);
}

void moe_yield() {
    moe_next_thread();
}

int moe_wait_for_timer(moe_timer_t* timer) {
    while (moe_check_timer(timer)) {
        moe_yield();
    }
    return 0;
}

int moe_usleep(uint64_t us) {
    moe_timer_t timer = moe_create_interval_timer(us);
    return moe_wait_for_timer(&timer);
}

void link_thread(moe_fiber_t* parent, moe_fiber_t* child) {
    child->next = parent->next;
    parent->next = child;
}

int moe_create_thread(moe_start_thread start, void* context, uintptr_t reserved1) {

    moe_fiber_t* new_thread = mm_alloc_static(sizeof(moe_fiber_t));
    memset(new_thread, 0, sizeof(moe_fiber_t));
    new_thread->thid = atomic_exchange_add(&next_thid, 1);
    const uintptr_t stack_count = 0x1000;
    const uintptr_t stack_size = stack_count * sizeof(uintptr_t);
    uintptr_t* stack = mm_alloc_static(stack_size);
    memset(stack, 0, stack_size);
    uintptr_t* sp = stack + stack_count;
    *--sp = 0x00007fffdeadbeef;
    *--sp = (uintptr_t)context;
    *--sp = (uintptr_t)start;
    new_jmpbuf(new_thread->jmpbuf, sp);

    link_thread(current_thread, new_thread);

    // moe_switch_context(new_thread);

    return new_thread->thid;
}


/*********************************************************************/

EFI_RUNTIME_SERVICES* gRT;

void dump_madt(uint8_t* p, size_t n) {
    for (int i = 0; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

extern int putchar(char);

int getchar() {
    for(;;) {
        int c = hid_getchar();
        if (c >= 0) {
            return c;
        }
        moe_yield();
    }
}

int read_cmdline(char* buffer, size_t max_len) {
    int cont_flag = 1;
    int len = 0, limit = max_len - 1;

    while (cont_flag) {
        char c = getchar();
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
                    buffer[len++] = c;
                    if (c < 0x20) { // ^X
                        printf("^%c", c | 0x40);
                    } else {
                        putchar(c);
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

void start_init(void* context)  {

    mgs_fill_rect( 50,  50, 300, 300, 0xFF77CC);
    mgs_fill_rect(150, 150, 300, 300, 0x77FFCC);
    mgs_fill_rect(250, 100, 300, 300, 0x77CCFF);

    //  Show BGRT (Boot Graphics Resource Table) from ACPI
    acpi_bgrt_t* bgrt = acpi_find_table(ACPI_BGRT_SIGNATURE);
    if (bgrt) {
        draw_logo_bitmap(video, (uint8_t*)bgrt->Image_Address, bgrt->Image_Offset_X, bgrt->Image_Offset_Y);
    }

    printf("%s v%d.%d.%d [Memory %dMB]\n", VER_SYSTEM_NAME, VER_SYSTEM_MAJOR, VER_SYSTEM_MINOR, VER_SYSTEM_REVISION, (int)(total_memory >> 8));
    printf("Hello, world!\n");

    hid_init();

    //  Pseudo shell
    {
        const size_t cmdline_size = 80;
        char* cmdline = mm_alloc_static(cmdline_size);

        EFI_TIME time;
        gRT->GetTime(&time, NULL);
        printf("Current Time: %d-%02d-%02d %02d:%02d:%02d\n", time.Year, time.Month, time.Day, time.Hour, time.Minute, time.Second);

        printf("Checking Timer...");
        moe_usleep(1000000);
        printf("Ok\n");

        for (;;) {
            printf("C>");
            read_cmdline(cmdline, cmdline_size);

            switch (cmdline[0]) {
                case 0:
                break;

                case 'r':
                    gRT->ResetSystem(EfiResetWarm, 0, 0, NULL);
                    break;

                case 'u':
                    gRT->ResetSystem(EfiResetShutdown, 0, 0, NULL);
                    break;

                case 'a':
                {
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

void start_kernel(moe_bootinfo_t* bootinfo) {

    current_thread = &root_thread;
    gRT = bootinfo->efiRT;
    mgs_init(&bootinfo->video);
    mm_init(&bootinfo->mmap);
    acpi_init(bootinfo->acpi);
    arch_init();

    moe_create_thread(&start_init, 0, 0);

    //  Do Idle
    for (;;) io_hlt();

}
