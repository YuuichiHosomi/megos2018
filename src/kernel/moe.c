/*

    Minimal Kernel

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

/*********************************************************************/

void memset32(uint32_t* p, uint32_t v, size_t n) {
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < n; i++) {
        *p++ = v;
    }
}

int puts(const char* s) {
    return printf("%s\n", s);
}

/*********************************************************************/

void start_kernel(moe_bootinfo_t* bootinfo) {

    mgs_init(&bootinfo->video);

    mgs_fill_rect(50, 50, 300, 300, 0xFF77CC);
    mgs_fill_rect(150, 150, 300, 300, 0x77FFCC);
    mgs_fill_rect(250, 100, 300, 300, 0xCC77FF);

    printf("Minimal Operating Environment v0.0 [%zdbit kernel]\n", 8*sizeof(void*));
    printf("----\nBOOTINFO: @%p\n", (void*)bootinfo);
    printf("ACPI: @%p\n", (void*)bootinfo->acpi);
    printf("Screen: %dx%d @%p\n", bootinfo->video.res_x, bootinfo->video.res_y, bootinfo->video.vram);
    printf("\n");
    printf("Hello, world!\n");

    for(;;) __asm__ volatile ("hlt");
}
