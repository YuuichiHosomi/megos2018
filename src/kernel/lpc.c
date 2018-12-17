// Legacy PC Devices
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"
#include "kernel.h"
#include "hid.h"


static uint8_t io_in8(uint16_t const port) {
    uint8_t al;
    if (port < 0x100) {
        __asm__ volatile("inb %1, %%al": "=a"(al): "n"(port));
    } else {
        __asm__ volatile("inb %%dx, %%al": "=a"(al): "d"(port));
    }
    return al;
}

static void io_out8(uint16_t const port, uint8_t val) {
    if (port < 0x100) {
        __asm__ volatile("outb %%al, %0": : "n"(port), "a"(val));
    } else {
        __asm__ volatile("outb %%al, %%dx": : "d"(port), "a"(val));
    }
}


/*********************************************************************/
//  PS/2 Keyboard and Mouse

#define PS2_DATA_PORT       0x0060
#define PS2_STATUS_PORT     0x0064
#define PS2_COMMAND_PORT    0x0064

#define PS2_STATE_EXTEND    0x4000

#define PS2_SCAN_BREAK      0x80
#define PS2_SCAN_EXTEND     0xE0
#define PS2_SCAN_EXT16      0x80

#define PS2_TIMEOUT         1000

#define PS2_FIFO_KEY_MIN    0x10000
#define PS2_FIFO_KEY_MAX    0x1FFFF
#define PS2_FIFO_MOUSE_MIN  0x20000
#define PS2_FIFO_MOUSE_MAX  0x2FFFF

static moe_fifo_t *ps2_fifo;

static volatile uintptr_t ps2k_state = 0;

typedef enum {
    ps2m_phase_ack = 0,
    ps2m_phase_head,
    ps2m_phase_x,
    ps2m_phase_y,
} ps2m_packet_phase;

ps2m_packet_phase ps2m_phase;
uint8_t ps2m_packet[4];

uint8_t ps2_to_hid_usage_table[] = {
    0x00, 0x29, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2A, 0x2B, // 0
    0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13, 0x2F, 0x30, 0x28, 0xE0, 0x04, 0x16, // 1
    0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33, 0x34, 0x35, 0xE1, 0x31, 0x1D, 0x1B, 0x06, 0x19, // 2
    0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xE5, 0x55, 0xE2, 0x2C, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, // 3
    0x3F, 0x40, 0x41, 0x42, 0x43, 0x53, 0x47, 0x5F, 0x60, 0x61, 0x56, 0x5C, 0x5D, 0x5E, 0x57, 0x59, // 4
    0x5A, 0x5B, 0x62, 0x63,    0,    0,    0, 0x44, 0x45,    0,    0,    0,    0,    0,    0,    0, // 5
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, // 6
    0x88,    0,    0, 0x87,    0,    0,    0,    0,    0, 0x8A,    0, 0x8B,    0, 0x89,    0,    0, // 7
//     0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, // E0 0
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, 0x58, 0xE4,    0,    0, // E0 1
    0x7F,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, 0x81,    0, // E0 2
    0x80,    0,    0,    0,    0, 0x54,    0,    0, 0xE6,    0,    0,    0,    0,    0,    0,    0, // E0 3
       0,    0,    0,    0,    0,    0,    0, 0x4A, 0x52, 0x4B,    0, 0x50,    0, 0x4F,    0, 0x4D, // E0 4
    0x51, 0x4E, 0x49, 0x4C,    0,    0,    0,    0,    0,    0,    0, 0xE3, 0xE7, 0x65, 0x66,    0, // E0 5
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, // E0 6
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, // E0 7
};


int ps2_wait_for_write(uint64_t timeout) {
    moe_timer_t timer = moe_create_interval_timer(timeout);
    while (moe_check_timer(&timer)) {
        if ((io_in8(PS2_STATUS_PORT) & 0x02) == 0x00) {
            return 1;
        }
        io_pause();
    }
    return 0;
}


int ps2k_irq_handler(int irq) {
    moe_fifo_write(ps2_fifo, PS2_FIFO_KEY_MIN + io_in8(PS2_DATA_PORT));
    return 0;
}


int ps2m_irq_handler(int irq) {
    moe_fifo_write(ps2_fifo, PS2_FIFO_MOUSE_MIN + io_in8(PS2_DATA_PORT));
    return 0;
}


int ps2_parse_data(intptr_t data, moe_hid_keyboard_report_t* keyreport, moe_hid_mouse_report_t* mouse_report) {

    if (data >= PS2_FIFO_MOUSE_MIN) {
        int m = (data - PS2_FIFO_MOUSE_MIN);
        switch(ps2m_phase) {
            case ps2m_phase_ack:
                if (m == 0xFA) ps2m_phase++;
                return hid_ps2_nodata;

            case ps2m_phase_head:
                if ((m &0xC8) == 0x08) {
                    ps2m_packet[ps2m_phase++] = m;
                }
                return hid_ps2_continued;

            case ps2m_phase_x:
                ps2m_packet[ps2m_phase++] = m;
                return hid_ps2_continued;

            case ps2m_phase_y:
                ps2m_packet[ps2m_phase] = m;
                ps2m_phase = ps2m_phase_head;

                mouse_report->buttons = ps2m_packet[1] & 0x07;
                int32_t x, y;
                x = ps2m_packet[2];
                y = ps2m_packet[3];
                if (ps2m_packet[1] & 0x10) {
                    x |= 0xFFFFFF00;
                }
                if (ps2m_packet[1] & 0x20) {
                    y |= 0xFFFFFF00;
                }
                y = 0 - y;
                mouse_report->x = x;
                mouse_report->y = y;

                return hid_ps2_mouse_report_enabled;
        }
    } else {
        uint8_t k = (data - PS2_FIFO_KEY_MIN);
        if (k == PS2_SCAN_EXTEND) {
            ps2k_state |= PS2_STATE_EXTEND;
            return hid_ps2_continued;
        } else if (k) {
            int is_break = (k & PS2_SCAN_BREAK) != 0;
            uint32_t scan = k & 0x7F;
            if (ps2k_state & PS2_STATE_EXTEND) {
                ps2k_state &= ~PS2_STATE_EXTEND;
                scan |= PS2_SCAN_EXT16;
            }
            uint8_t usage = ps2_to_hid_usage_table[scan];
            if (usage >= HID_USAGE_MOD_MIN && usage <= HID_USAGE_MOD_MAX) {
                uint32_t modifier = 1 << (usage - HID_USAGE_MOD_MIN);
                if (is_break) {
                    ps2k_state &= ~modifier;
                } else {
                    ps2k_state |= modifier;
                }
            }
            if (!is_break) {
                keyreport->keydata[0] = usage;
            }
            keyreport->modifier = ps2k_state;
            return hid_ps2_key_report_enabled;
        }
    }

    return hid_ps2_nodata;
}


// PS2 to HID Thread
extern void move_mouse(moe_hid_mouse_report_t* mouse_report);
_Noreturn void ps2_hid_thread(void *args) {

    for (;;) {
        int mouse_changed = 0;
        int cont;
        moe_hid_mouse_report_t mouse_report;
        moe_hid_keyboard_report_t keyreport;
        do {
            memset(&mouse_report, 0, sizeof(mouse_report));
            memset(&keyreport, 0, sizeof(keyreport));

            cont = 0;
            intptr_t ps2_data;
            if (moe_fifo_read_and_wait(ps2_fifo, &ps2_data, 1000000)) {
                int state = ps2_parse_data(ps2_data, &keyreport, &mouse_report);

                switch(state) {
                    case hid_ps2_key_report_enabled:
                        if (keyreport.keydata[0]) {
                            if ((keyreport.keydata[0] & 0x7F) == HID_USAGE_DELETE &&
                                (keyreport.modifier & (0x11)) != 0 && (keyreport.modifier & (0x44)) != 0) {
                                moe_reboot();
                            }
                            moe_send_key_event(&keyreport);
                        }
                        break;

                    case hid_ps2_mouse_report_enabled:
                    {
                        mouse_changed = 1;
                    }
                        break;

                    case hid_ps2_continued:
                        cont = 1;
                }
            }
        } while (cont);

        if (mouse_changed) {
            uint8_t buttons_changed = mouse_report.buttons ^ mouse_report.old_buttons;
            mouse_report.pressed = buttons_changed & mouse_report.buttons;
            mouse_report.released = buttons_changed & mouse_report.old_buttons;
            if (mouse_report.x || mouse_report.y || buttons_changed) {
                move_mouse(&mouse_report);
            }
            mouse_changed = 0;
            mouse_report.old_buttons = mouse_report.buttons;
            mouse_report.buttons = 0;
            mouse_report.x = 0;
            mouse_report.y = 0;
        }

    }
}


int ps2_init() {

    if (!ps2_wait_for_write(100000)) return 0;

    io_out8(PS2_COMMAND_PORT, 0xAD);
    ps2_wait_for_write(PS2_TIMEOUT);
    io_out8(PS2_COMMAND_PORT, 0xA7);

    for (int i = 0; i< 16; i++) {
        io_in8(PS2_DATA_PORT);
    }

    uintptr_t size_of_buffer = 128;
    ps2_fifo = moe_fifo_init(size_of_buffer);

    moe_enable_irq(1, ps2k_irq_handler);
    moe_enable_irq(12, ps2m_irq_handler);

    ps2_wait_for_write(PS2_TIMEOUT);
    io_out8(PS2_COMMAND_PORT, 0x60);
    ps2_wait_for_write(PS2_TIMEOUT);
    io_out8(PS2_DATA_PORT, 0x47);

    ps2_wait_for_write(PS2_TIMEOUT);
    io_out8(PS2_COMMAND_PORT, 0xD4);
    ps2_wait_for_write(PS2_TIMEOUT);
    io_out8(PS2_DATA_PORT, 0xF4);

    moe_create_thread(ps2_hid_thread, priority_realtime, 0, "hid-ps2");

    return 1;
}


/*********************************************************************/

void lpc_init() {
    ps2_init();
}
