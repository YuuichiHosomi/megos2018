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

/*********************************************************************/

void start_kernel(moe_bootinfo_t* bootinfo) {

    uintptr_t memsize = mm_init(bootinfo->mmap, bootinfo->mmap_size, bootinfo->mmap_desc_size);
    acpi_init(bootinfo->acpi);
    arch_init();
    mgs_init(&bootinfo->video);

    mgs_fill_rect(50, 50, 300, 300, 0xFF77CC);
    mgs_fill_rect(150, 150, 300, 300, 0x77FFCC);
    mgs_fill_rect(250, 100, 300, 300, 0x77CCFF);

    printf("Minimal OS v0.0 [Memory %dMB]\n", (int)(memsize >> 20));
    printf("\n");
    printf("Hello, world!\n");

    void* fadt = acpi_find_table("FACP");
    printf("ACPI %p, FADT %p\n", (void*)bootinfo->acpi, fadt);

    volatile intptr_t* hoge = (intptr_t*)(0x123456789abc);
    *hoge = *hoge;
    __asm__ volatile ("int $3");

    for(;;) __asm__ volatile ("hlt");
}
