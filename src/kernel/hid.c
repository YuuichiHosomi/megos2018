//  Human Interface Device Service
// Copyright (c) 1998,2000,2018 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"

moe_fifo_t hid_fifo;
extern int ps2_init();
int ps2_exist = 0;

extern int32_t ps2_get_data();
extern uint32_t ps2_scan_to_unicode(uint32_t);

int hid_getchar() {
    const int EOF = -1;
    return moe_fifo_read(&hid_fifo, EOF);
}

void hid_thread(void *context) {
    for (;;) {
        int cont;
        do {
            cont = 0;
            if (ps2_exist) {
                int32_t scan = ps2_get_data();
                if (scan > 0) {
                    cont = 1;
                    moe_fifo_write(&hid_fifo, ps2_scan_to_unicode(scan));
                }
            }
        } while (cont);
        moe_next_thread();
    }
}

void hid_init() {

    const uintptr_t fifo_size = 256;
    intptr_t* buffer = mm_alloc_static(fifo_size * sizeof(intptr_t));
    moe_fifo_init(&hid_fifo, buffer, fifo_size);

    moe_create_thread(hid_thread, 0, 0);
    ps2_exist = ps2_init();
}
