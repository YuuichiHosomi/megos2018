// MEG-OS Minimal Loader
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

#include "moe.h"
#include "kernel.h"
#include "efi.h"


_Noreturn void start_kernel(moe_bootinfo_t* bootinfo, uint64_t* param);

void page_init(moe_bootinfo_t *bootinfo, void *mmap, size_t mmsize, size_t mmdescsize);
void *valloc(uint64_t base, size_t size);
void vprotect(uint64_t base, size_t size, int attr);

typedef uint64_t (*IMAGE_LOCATOR)(uint64_t);
IMAGE_LOCATOR recognize_kernel_signature(void *obj, size_t size);
