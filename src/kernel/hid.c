//  Human Interface Device Service
// Copyright (c) 1998,2000,2018 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"

moe_fifo_t hid_fifo;
extern int ps2_init();
int ps2_exists = 0;

extern int ps2_get_data(moe_hid_keyboard_report_t* keyreport, moe_hid_mouse_report_t* mouse_report);
extern uint32_t ps2_scan_to_unicode(uint32_t scan, uint32_t modifier);

int hid_getchar() {
    const int EOF = -1;
    return moe_fifo_read(&hid_fifo, EOF);
}

int mouse_x, mouse_y;

//  PS2 delete
#define SCAN_DELETE 0x53

extern void moe_ctrl_alt_del();

// HID Thread
void hid_thread(void *context) {
    for (;;) {
        int cont;
        do {
            moe_hid_mouse_report_t mouse_report;
            moe_hid_keyboard_report_t keyreport;

            cont = 0;
            if (ps2_exists) {
                int state = ps2_get_data(&keyreport, &mouse_report);

                switch(state) {
                    case 1:
                        if (keyreport.keydata[0]) {
                            if ((keyreport.keydata[0] & 0x7F) == SCAN_DELETE &&
                                (keyreport.modifier & (0x11)) != 0 && (keyreport.modifier & (0x44)) != 0) {
                                moe_ctrl_alt_del();
                            }
                            moe_fifo_write(&hid_fifo, ps2_scan_to_unicode(keyreport.keydata[0], keyreport.modifier));
                        }
                        break;

                    case 2:
                        mouse_x += mouse_report.x;
                        mouse_y += mouse_report.y;
                        const int cursor_r = 10;
                        mgs_fill_rect(mouse_x - cursor_r / 2, mouse_y - cursor_r / 2, cursor_r, cursor_r, 0xFFFFFF);
                        mgs_fill_rect(mouse_x - cursor_r / 2 + 2, mouse_y - cursor_r / 2 + 2, cursor_r - 4, cursor_r - 4, 0x007700);
                        break;

                    case 3:
                        cont = 1;
                }
            }
        } while (cont);
        moe_yield();
    }
}

void hid_init() {

    const uintptr_t fifo_size = 256;
    intptr_t* buffer = mm_alloc_static(fifo_size * sizeof(intptr_t));
    moe_fifo_init(&hid_fifo, buffer, fifo_size);

    moe_create_thread(hid_thread, 0, 0);
    ps2_exists = ps2_init();
}
