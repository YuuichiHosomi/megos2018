/*

    Minimal Operating Environment - Kernel

    Copyright (c) 1998,2000,2018 MEG-OS project, All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

*/
#include "moe.h"

#define VER_SYSTEM_NAME     "Minimal Operaring Environment"
#define VER_SYSTEM_MAJOR    0
#define VER_SYSTEM_MINOR    1
#define VER_SYSTEM_REVISION 2


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

void dump_madt(uint8_t* p, size_t n) {
    for (int i = 0; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

extern int putchar(char);
extern uint32_t ps2_get_data();
extern uint32_t ps2_scan_to_unicode(uint32_t);
extern uint64_t hpet_get_count();

int getchar() {
    uint32_t scan = ps2_get_data();
    if (scan) {
        return ps2_scan_to_unicode(scan);
    }
    return 0;
}


void start_kernel(moe_bootinfo_t* bootinfo) {

    mgs_init(&bootinfo->video);
    uintptr_t memsize = mm_init(bootinfo->mmap, bootinfo->mmap_size, bootinfo->mmap_desc_size);
    acpi_init(bootinfo->acpi);
    arch_init();

    mgs_fill_rect( 50,  50, 300, 300, 0xFF77CC);
    mgs_fill_rect(150, 150, 300, 300, 0x77FFCC);
    mgs_fill_rect(250, 100, 300, 300, 0x77CCFF);

    printf("%s v%d.%d.%d [Memory %dMB]\n", VER_SYSTEM_NAME, VER_SYSTEM_MAJOR, VER_SYSTEM_MINOR, VER_SYSTEM_REVISION, (int)(memsize >> 20));
    printf("Hello, world!\n");

    acpi_bgrt_t* bgrt = acpi_find_table(ACPI_BGRT_SIGNATURE);
    if (bgrt) {
        draw_logo_bitmap(&bootinfo->video, (uint8_t*)bgrt->Image_Address, bgrt->Image_Offset_X, bgrt->Image_Offset_Y);
    }

    // int n = acpi_get_number_of_table_entries();
    // for (int i = 0; i < n; i++) {
    //     acpi_header_t* table = acpi_enum_table_entry(i);
    //     if (table) {
    //         printf("%p: %.4s %d\n", (void*)table, table->signature, table->length);
    //     }
    // }

    // acpi_madt_t* madt = acpi_find_table(ACPI_MADT_SIGNATURE);
    // if (madt) {
    //     size_t max_length = madt->Header.length - 44;
    //     uint8_t* p = madt->Structure;
    //     for (size_t loc = 0; loc < max_length; ) {
    //         size_t len = p[loc+1];
    //         dump_madt(p+loc, len);
    //         loc += len;
    //     }
    // }

    {
        for (;;) {
            char c = getchar();
            if (c) {
                putchar(c);
            }
        }
    }

    // volatile intptr_t* hoge = (intptr_t*)(0x123456789abc);
    // *hoge = *hoge;
    // __asm__ volatile ("int $3");
    for (;;) __asm__ volatile ("hlt");

}
