//  Human Interface Device Service
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdint.h>


#define INVALID_UNICHAR     0xFFFE
#define HID_MOD_LCTRL       0x0001
#define HID_MOD_LSHIFT      0x0002
#define HID_MOD_LALT        0x0004
#define HID_MOD_LGUI        0x0008
#define HID_MOD_RCTRL       0x0010
#define HID_MOD_RSHIFT      0x0020
#define HID_MOD_RALT        0x0040
#define HID_MOD_RGUI        0x0080

typedef struct {
    union {
        uint8_t buttons;
        struct {
            uint8_t l_button:1;
            uint8_t r_button:1;
            uint8_t m_button:1;
        };
        int32_t PADDING_1;
    };
    int16_t x, y;
} moe_hid_mouse_report_t;

typedef struct {
    uint8_t modifier;
    uint8_t RESERVED_1;
    uint8_t keydata[6];
} moe_hid_keyboard_report_t;

int hid_getchar();