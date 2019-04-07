// Human Interface Device Manager
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"
#include "kernel.h"
#include <stdatomic.h>
#include "hid.h"

#define MAX_KBD 16


extern void move_mouse(moe_hid_mouse_report_t* mouse_report);


int usage_to_uni_table_1E[] = { // Non Alphabet
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0x0D, 0x1B, 0x08, 0x09, ' ', '-',
    '^', '@', '[', ']', INVALID_UNICHAR, ';', ':', '`', ',', '.', '/',
};

int usage_to_uni_table_4F[] = { // Arrows & Numpads
    0x2191, 0x2190, 0x2193, 0x2192, INVALID_UNICHAR,
    '/', '*', '-', '+', 0x0D, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '.',
};

//  JP 109
uint32_t hid_usage_to_unicode(uint8_t usage, uint8_t modifier) {
    uint32_t uni = INVALID_UNICHAR;

    if (usage >= 4 && usage <= 0x1D) { // Alphabet
        uni = usage - 4 + 'a';
    } else if (usage >= 0x1E && usage <= 0x38) { // Non Alphabet
        uni = usage_to_uni_table_1E[usage - 0x1E];
        if (uni > 0x20 && uni < 0x40 && uni != 0x30 && (modifier & (HID_MOD_LSHIFT | HID_MOD_LSHIFT))) {
            uni ^= 0x10;
        }
    } else if (usage == HID_USAGE_DELETE) { // Delete
        uni = 0x7F;
    } else if (usage >= 0x4F && usage <= 0x64) { // Arrows & Numpads
        uni = usage_to_uni_table_4F[usage - 0x4F];
    } else if (usage == 0x89) { // '\|'
        uni = '\\';
    }
    if (uni >= 0x40 && uni < 0x7F) {
        if (modifier & (HID_MOD_LCTRL | HID_MOD_RCTRL)) {
            uni &= 0x1F;
        } else if (modifier & (HID_MOD_LSHIFT | HID_MOD_LSHIFT)) {
            uni ^= 0x20;
        }
    }
    if (usage == 0x87) { // '_'
        if (modifier & (HID_MOD_LSHIFT | HID_MOD_LSHIFT)) {
            uni = '_';
        } else {
            uni = '\\';
        }
    }

    return uni;
}


int hid_report_key(moe_hid_kbd_report_t *keyreport) {
    uint64_t *q = (uint64_t *)keyreport;
    if (q[0] == q[1]) return 0;

    moe_hid_kbd_report_t current = {0};
    uint8_t pressed[6];
    int next = 0;
    for (int i = 0; i < 6; i++) {
        int found = 0;
        uint8_t u = keyreport[0].keydata[i];
        if (!u) break;
        for (int j = 0; j < 6; j++) {
            uint8_t v = keyreport[1].keydata[j];
            if (!v) break;
            if (v == u) {
                found = 1;
            }
        }
        if (!found) {
            pressed[next++] = u;
        }
    }
    memcpy(keyreport + 1, keyreport, sizeof(moe_hid_kbd_report_t));
    if (next == 0) return 0;
    current.modifier = keyreport[0].modifier;

    for (int i = 0; i < next; i++) {
        current.keydata[0] = pressed[i];
        if ((current.keydata[0] == HID_USAGE_DELETE) &&
            (current.modifier & (0x11)) != 0 && (current.modifier & (0x44)) != 0) {
            moe_reboot();
        }
        moe_send_key_event(&current);
    }

    return 1;
}

void hid_report_mouse(moe_hid_mouse_report_t mouse_report) {
}


void hid_init() {

}
