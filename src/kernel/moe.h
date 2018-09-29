/*

    Minimal Operating Environment

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
#include <stdint.h>
#include "acpi.h"

#ifndef NULL
#define	NULL (0)
#endif

void memset32(uint32_t* p, uint32_t v, size_t n);

typedef struct {
    void* vram;
    size_t vram_size;
    int res_x, res_y, pixel_per_scan_line;
} moe_video_info_t;

typedef struct {
    moe_video_info_t video;
    acpi_rsd_ptr_t* acpi;
} moe_bootinfo_t;

extern void start_kernel(moe_bootinfo_t* bootinfo) __attribute__((__noreturn__));

//  Minimal Graphics Subsystem
void mgs_init(moe_video_info_t* _video);
void mgs_fill_rect(int x, int y, int width, int height, uint32_t color);
void mgs_fill_block(int x, int y, int width, int height, uint32_t color);
void mgs_cls();
int printf(const char* format, ...);
