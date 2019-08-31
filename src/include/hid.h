//  Human Interface Device Service
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: MIT
#include <stdint.h>


#define INVALID_UNICHAR     0xFFFE

#define HID_USAGE_DELETE    0x004C
#define HID_USAGE_MOD_MIN   0x00E0
#define HID_USAGE_MOD_MAX   0x00E7
#define HID_MOD_LCTRL       0x0001
#define HID_MOD_LSHIFT      0x0002
#define HID_MOD_LALT        0x0004
#define HID_MOD_LGUI        0x0008
#define HID_MOD_RCTRL       0x0010
#define HID_MOD_RSHIFT      0x0020
#define HID_MOD_RALT        0x0040
#define HID_MOD_RGUI        0x0080

// Raw HID boot keyboard protocol structure
typedef struct hid_raw_kbd_report_t {
    uint8_t modifier;
    uint8_t RESERVED_1;
    uint8_t keydata[6];
} hid_raw_kbd_report_t;

typedef struct moe_hid_kbd_state_t {
    hid_raw_kbd_report_t current, prev;
} moe_hid_kbd_state_t;

// Raw HID boot mouse protocol structure
typedef struct {
    union {
        uint8_t buttons;
        struct {
            uint8_t l_button:1;
            uint8_t r_button:1;
            uint8_t m_button:1;
            uint8_t _padding:5;
        };
    };
    int8_t x, y;
} hid_raw_mos_report_t;

typedef struct {
    union {
        struct {
            uint8_t buttons;
            uint8_t pressed;
            uint8_t released;
            uint8_t old_buttons;
        };
        struct {
            uint8_t l_button:1;
            uint8_t r_button:1;
            uint8_t m_button:1;
        };
        int32_t packed_buttons;
    };
    int16_t x, y;
} moe_hid_mos_state_t;

typedef struct {
    union {
        struct {
            uint8_t buttons;
            uint8_t pressed;
            uint8_t released;
            uint8_t old_buttons;
        };
        struct {
            uint8_t l_button:1;
            uint8_t r_button:1;
            uint8_t m_button:1;
        };
        int32_t packed_buttons;
    };
    int16_t x, y;
} moe_hid_absolute_pointer_t;

int hid_process_key_report(moe_hid_kbd_state_t *kbd);
uint32_t hid_usage_to_unicode(uint8_t usage, uint8_t modifier);
moe_hid_mos_state_t *hid_convert_mouse(moe_hid_mos_state_t *mos, hid_raw_mos_report_t *raw);
int hid_process_mouse_report(moe_hid_mos_state_t *mos);
int hid_process_absolute_pointer(moe_hid_absolute_pointer_t *abs);

int moe_send_key_event(moe_hid_kbd_state_t *kbd);
