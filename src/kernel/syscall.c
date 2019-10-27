// System call
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

#include "moe.h"
#include "kernel.h"

extern int putchar(int);


uintptr_t syscall(uintptr_t func_no, uintptr_t params) {
    switch (func_no) {
        case 1:
            moe_exit_thread(params);

        case 126:
            putchar((int)params);
            break;

        default:
            moe_panic("Unknown syscall %zx %zx\n", func_no, params);
    }
    return 0;
}
