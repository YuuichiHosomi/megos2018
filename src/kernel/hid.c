//  Human Interface Device Service
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"
#include "hid.h"


struct {
    moe_hid_mouse_report_t mouse;
    int mouse_changed;
} hid_state;

static moe_fifo_t *ps2_fifo = NULL;

extern int ps2_init(moe_fifo_t **fifo);
int ps2_exists = 0;
extern int ps2_parse_data(intptr_t data, moe_hid_keyboard_report_t* keyreport, moe_hid_mouse_report_t* mouse_report);
extern void move_mouse(moe_hid_mouse_report_t* mouse_report);


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
            intptr_t ps2_data;
            if (moe_fifo_read_and_wait(ps2_fifo, &ps2_data, 1000000)) {
                int state = ps2_parse_data(ps2_data, &keyreport, &mouse_report);

                switch(state) {
                    case hid_ps2_key_report_enabled:
                        if (keyreport.keydata[0]) {
                            if ((keyreport.keydata[0] & 0x7F) == SCAN_DELETE &&
                                (keyreport.modifier & (0x11)) != 0 && (keyreport.modifier & (0x44)) != 0) {
                                moe_reboot();
                            }
                            moe_send_key_event(&keyreport);
                        }
                        break;

                    case hid_ps2_mouse_report_enabled:
                    {
                        hid_state.mouse.buttons |= mouse_report.buttons;
                        hid_state.mouse.x += mouse_report.x;
                        hid_state.mouse.y += mouse_report.y;
                        hid_state.mouse_changed = 1;
                    }
                        break;

                    case hid_ps2_continued:
                        cont = 1;
                }
            }
        } while (cont);

        if (hid_state.mouse_changed) {
            uint8_t buttons_changed = hid_state.mouse.buttons ^ hid_state.mouse.old_buttons;
            hid_state.mouse.pressed = buttons_changed & hid_state.mouse.buttons;
            hid_state.mouse.released = buttons_changed & hid_state.mouse.old_buttons;
            if (hid_state.mouse.x || hid_state.mouse.y || buttons_changed) {
                move_mouse(&hid_state.mouse);
            }
            hid_state.mouse_changed = 0;
            hid_state.mouse.old_buttons = hid_state.mouse.buttons;
            hid_state.mouse.buttons = 0;
            hid_state.mouse.x = 0;
            hid_state.mouse.y = 0;
        }

    }
}

void hid_init() {
    ps2_init(&ps2_fifo);
    if (ps2_fifo) {
        moe_create_thread(hid_thread, priority_realtime, 0, "hid-ps2");
    }
}
