//  Human Interface Device Service
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"


moe_fifo_t* hid_fifo;
extern int ps2_init();
int ps2_exists = 0;
extern void moe_ctrl_alt_del();
extern int ps2_parse_data(moe_hid_keyboard_report_t* keyreport, moe_hid_mouse_report_t* mouse_report);
extern void move_mouse(int x, int y);


int hid_getchar() {
    const int EOF = -1;
    return moe_fifo_read(hid_fifo, EOF);
}

#define SCAN_DELETE 0x4C


int scan_to_uni_table_1E[] = { // regular non alphabet
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0x0D, 0x1B, 0x08, 0x09, ' ', '-',
    '^', '@', '[', ']', INVALID_UNICHAR, ';', ':', '`', ',', '.', '/',
};

int scan_to_uni_table_4F[] = { // Arrows & Numpads
    0x2191, 0x2190, 0x2193, 0x2192, INVALID_UNICHAR,
    '/', '*', '-', '+', 0x0D, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '.',
};

//  JP 109
uint32_t hid_scan_to_unicode(uint8_t scan, uint8_t modifier) {
    uint32_t uni = INVALID_UNICHAR;

    if (scan >= 4 && scan <= 0x1D) { // Alphabet
        uni = scan - 4 + 'a';
    } else if (scan >= 0x1E && scan <= 0x38) { // Regular non alphabet
        uni = scan_to_uni_table_1E[scan - 0x1E];
        if (uni > 0x20 && uni < 0x40 && uni != 0x30 && (modifier & (HID_MOD_LSHIFT | HID_MOD_LSHIFT))) {
            uni ^= 0x10;
        }
    } else if (scan == 0x4C) { // Delete
        uni = 0x7F;
    } else if (scan >= 0x4F && scan <= 0x64) { // Arrows & Numpads
        uni = scan_to_uni_table_4F[scan - 0x4F];
    } else if (scan == 0x89) { // '\|'
        uni = '\\';
    }
    if (uni >= 0x40 && uni < 0x7F) {
        if (modifier & (HID_MOD_LCTRL | HID_MOD_RCTRL)) {
            uni &= 0x1F;
        } else if (modifier & (HID_MOD_LSHIFT | HID_MOD_LSHIFT)) {
            uni ^= 0x20;
        }
    }
    if (scan == 0x87) { // '_'
        if (modifier & (HID_MOD_LSHIFT | HID_MOD_LSHIFT)) {
            uni = '_';
        } else {
            uni = '\\';
        }
    }

    return uni;
}

// HID Thread
_Noreturn void hid_thread(void *args) {
    for (;;) {
        int cont;
        do {
            moe_hid_mouse_report_t mouse_report;
            moe_hid_keyboard_report_t keyreport;

            cont = 0;
            if (ps2_exists) {
                int state = ps2_parse_data(&keyreport, &mouse_report);

                switch(state) {
                    case 1:
                        if (keyreport.keydata[0]) {
                            if ((keyreport.keydata[0] & 0x7F) == SCAN_DELETE &&
                                (keyreport.modifier & (0x11)) != 0 && (keyreport.modifier & (0x44)) != 0) {
                                moe_ctrl_alt_del();
                            }
                            uint32_t uni = hid_scan_to_unicode(keyreport.keydata[0], keyreport.modifier);
                            if (uni != INVALID_UNICHAR) {
                                moe_fifo_write(hid_fifo, uni);
                            }
                        }
                        break;

                    case 2:
                        move_mouse(mouse_report.x, mouse_report.y);
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
    hid_fifo = moe_fifo_init(fifo_size);

    moe_create_thread(hid_thread, 0, "HID");
    ps2_exists = ps2_init();
}
