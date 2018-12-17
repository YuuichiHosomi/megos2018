//  Human Interface Device Service
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"
#include "hid.h"


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
