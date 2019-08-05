// MOE
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include "moe.h"
#include "kernel.h"


int moe_usleep(uint64_t us) {
    // TODO:
    moe_timer_t timer = moe_create_interval_timer(us);
    while (moe_check_timer(&timer)) {
        io_hlt();
    }
    return 0;
}
