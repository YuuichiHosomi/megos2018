// Human Interface Device Manager
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

#include "moe.h"
#include "kernel.h"
#include "hid.h"

#define MAX_KBD 16


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


#define MOUSE_CURSOR_WIDTH  12
#define MOUSE_CURSOR_HEIGHT  20
static uint32_t mouse_cursor_palette[] = { 0x00FF00FF, 0xFFFFFFFF, 0x80000000 };
static uint8_t mouse_cursor_source[MOUSE_CURSOR_HEIGHT][MOUSE_CURSOR_WIDTH] = {
    { 1, },
    { 1, 1, },
    { 1, 2, 1, },
    { 1, 2, 2, 1, },
    { 1, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, },
    { 1, 2, 2, 2, 1, 2, 2, 1, },
    { 1, 2, 2, 1, 0, 1, 2, 2, 1, },
    { 1, 2, 1, 0, 0, 1, 2, 2, 1, },
    { 1, 1, 0, 0, 0, 0, 1, 2, 2, 1, },
    { 0, 0, 0, 0, 0, 0, 1, 2, 2, 1, },
    { 0, 0, 0, 0, 0, 0, 0, 1, 1, },
};
moe_bitmap_t mouse_cursor;

int hid_process_key_report(moe_hid_kbd_state_t *kbd) {
    uint64_t *q = (uint64_t *)kbd;
    if (q[0] == q[1]) return 0;

    moe_hid_kbd_state_t state = {{0}};
    uint8_t pressed[8] = {0};
    int next = 0;
    for (int i = 0; i < 6; i++) {
        int found = 0;
        uint8_t u = kbd->current.keydata[i];
        if (!u) break;
        for (int j = 0; j < 6; j++) {
            uint8_t v = kbd->prev.keydata[j];
            if (!v) break;
            if (v == u) {
                found = 1;
            }
        }
        if (!found) {
            pressed[next++] = u;
        }
    }
    kbd->prev = kbd->current;
    if (next == 0) return 0;
    state.current.modifier = kbd->current.modifier;

    for (int i = 0; i < next; i++) {
        state.current.keydata[0] = pressed[i];
        if ((state.current.keydata[0] == HID_USAGE_DELETE) &&
            (state.current.modifier & (0x11)) != 0 && (state.current.modifier & (0x44)) != 0) {
            moe_reboot();
        }
        moe_send_key_event(&state);
    }

    return 1;
}


moe_hid_mos_state_t *hid_convert_mouse(moe_hid_mos_state_t *mos, hid_raw_mos_report_t *raw) {
    uint8_t current = raw->buttons;
    mos->x = raw->x;
    mos->y = raw->y;
    uint8_t changed = current ^ mos->old_buttons;
    mos->buttons = current;
    mos->pressed = changed & current;
    mos->released = changed & mos->old_buttons;
    mos->old_buttons = current;
    return mos;
}

static moe_hid_absolute_pointer_t global_pointer = {{{0}}};

static void update_global_pointer() {
    moe_point_t point = {global_pointer.x, global_pointer.y};
    moe_blt(NULL, &mouse_cursor, &point, NULL, 0);
}

int hid_process_mouse_report(moe_hid_mos_state_t *mos) {
    // TODO: everything

    global_pointer.pressed |= mos->pressed;
    global_pointer.released |= mos->released;
    global_pointer.x += mos->x;
    global_pointer.y += mos->y;

    update_global_pointer();

    return 1;
}

int hid_process_absolute_pointer(moe_hid_absolute_pointer_t *abs) {
    // TODO: everything

    update_global_pointer();

    return 1;
}


void hid_init() {
    const size_t words = MOUSE_CURSOR_WIDTH * MOUSE_CURSOR_HEIGHT;
    mouse_cursor.width = MOUSE_CURSOR_WIDTH;
    mouse_cursor.delta = MOUSE_CURSOR_WIDTH;
    mouse_cursor.height = MOUSE_CURSOR_HEIGHT;
    mouse_cursor.flags = MOE_BMP_ALPHA;
    mouse_cursor.bitmap = moe_alloc_object(words * 4, 1);
    uint8_t *p = (uint8_t *)mouse_cursor_source; 
    for (size_t i = 0; i < words; i++) {
        mouse_cursor.bitmap[i] = mouse_cursor_palette[p[i]];
    }
}

