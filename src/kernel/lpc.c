// Legacy PC's Low Pin Count Devices
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

#include "moe.h"
#include "kernel.h"
#include "hid.h"


typedef enum {
    ps2_state_nodata,
    ps2_state_key,
    ps2_state_mouse,
    ps2_state_continued,
} ps2_state_t;


/*********************************************************************/
//  PS/2 Keyboard and Mouse

#define PS2_DATA_PORT       0x0060
#define PS2_STATUS_PORT     0x0064
#define PS2_COMMAND_PORT    0x0064

#define PS2_STATE_EXTEND    0x4000

#define PS2_SCAN_BREAK      0x80
#define PS2_SCAN_EXTEND     0xE0
#define PS2_SCAN_EXT16      0x80

#define PS2_WRITE_TIMEOUT   INT64_C(10000)
#define PS2_READ_TIMEOUT    INT64_C(100000)

#define PS2_FIFO_KEY_MIN    0x100
#define PS2_FIFO_KEY_MAX    0x1FF
#define PS2_FIFO_MOUSE_MIN  0x200
#define PS2_FIFO_MOUSE_MAX  0x2FF

static moe_queue_t *ps2_event_queue;

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
//  ---0  ---1  ---2  ---3  ---4  ---5  ---6  ---7  ---8  ---9  ---A  ---B  ---C  ---D  ---E  ---F
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, // E0 0
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, 0x58, 0xE4,    0,    0, // E0 1
    0x7F,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, 0x81,    0, // E0 2
    0x80,    0,    0,    0,    0, 0x54,    0,    0, 0xE6,    0,    0,    0,    0,    0,    0,    0, // E0 3
       0,    0,    0,    0,    0,    0,    0, 0x4A, 0x52, 0x4B,    0, 0x50,    0, 0x4F,    0, 0x4D, // E0 4
    0x51, 0x4E, 0x49, 0x4C,    0,    0,    0,    0,    0,    0,    0, 0xE3, 0xE7, 0x65, 0x66,    0, // E0 5
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, // E0 6
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0, // E0 7
};


static uint8_t ps2_read_data(void) {
    uint8_t al;
    __asm__ volatile("inb %1, %b0": "=a"(al): "i"(PS2_DATA_PORT));
    return al;
}

static void ps2_write_data(uint8_t val) {
    __asm__ volatile("outb %b1, %0": : "i"(PS2_DATA_PORT), "a"(val));
}

static uint8_t ps2_read_status(void) {
    uint8_t al;
    __asm__ volatile("inb %1, %b0": "=a"(al): "i"(PS2_STATUS_PORT));
    return al;
}

static void ps2_write_command(uint8_t val) {
    __asm__ volatile("outb %b1, %0": : "i"(PS2_COMMAND_PORT), "a"(val));
}

static void ps2_clear_interrupt(void) {
    // const int ps2_controll_port = 0x61;
    // uint8_t al;
    // __asm__ volatile(
    //     "inb %1, %b0;"
    //     "andb $0xF, %b0;"
    //     "orb $0x80, %b0;"
    //     "outb %b0, %1;"
    //     "pause;"
    //     "andb $0xF, %b0;"
    //     "outb %b0, %1;"
    // : "=a"(al): "i"(ps2_controll_port));
}


static inline int ps2_wait_for_write(int timeout) {
    moe_measure_t deadline = moe_create_measure(PS2_WRITE_TIMEOUT * timeout);
    while (moe_measure_until(deadline)) {
        if ((ps2_read_status() & 0x02) == 0x00) {
            return 0;
        } else {
            cpu_relax();
        }
    }
    return -1;
}

static inline int ps2_wait_for_read(int timeout) {
    moe_measure_t deadline = moe_create_measure(PS2_READ_TIMEOUT * timeout);
    while (moe_measure_until(deadline)) {
        if ((ps2_read_status() & 0x01) != 0x00) {
            return 0;
        } else {
            cpu_relax();
        }
    }
    return -1;
}


void ps2k_irq_handler(int irq) {
    uint32_t ps2k = ps2_read_data();
    // printf("K(%02x)", ps2k);
    moe_queue_write(ps2_event_queue, PS2_FIFO_KEY_MIN + ps2k);
    ps2_clear_interrupt();
}


void ps2m_irq_handler(int irq) {
    moe_queue_write(ps2_event_queue, PS2_FIFO_MOUSE_MIN + ps2_read_data());
    ps2_clear_interrupt();
}


static ps2_state_t ps2_parse_data(intptr_t data, hid_raw_kbd_report_t *keyreport, moe_hid_mos_state_t *mouse_report) {

    if (data >= PS2_FIFO_MOUSE_MIN) {
        int m = (data - PS2_FIFO_MOUSE_MIN);
        switch(ps2m_phase) {
            case ps2m_phase_ack:
                if (m == 0xFA) ps2m_phase++;
                return ps2_state_nodata;

            case ps2m_phase_head:
                if ((m &0xC8) == 0x08) {
                    ps2m_packet[ps2m_phase++] = m;
                }
                return ps2_state_continued;

            case ps2m_phase_x:
                ps2m_packet[ps2m_phase++] = m;
                return ps2_state_continued;

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

                return ps2_state_mouse;
        }
    } else {
        uint8_t k = (data - PS2_FIFO_KEY_MIN);
        if (k == PS2_SCAN_EXTEND) {
            ps2k_state |= PS2_STATE_EXTEND;
            return ps2_state_continued;
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
            return ps2_state_key;
        }
    }

    return ps2_state_nodata;
}


// PS2 to HID Thread
_Noreturn void ps2_hid_thread(void *args) {

    moe_hid_kbd_state_t keystate = {{0}};
    moe_hid_mos_state_t mouse = {{{0}}};
    for (;;) {
        int cont;
        do {
            memset(&keystate.current, 0, sizeof(hid_raw_kbd_report_t));
            cont = 0;
            intptr_t ps2_data;
            if (moe_queue_wait(ps2_event_queue, &ps2_data, MOE_FOREVER)) {
                ps2_state_t state = ps2_parse_data(ps2_data, &keystate.current, &mouse);
                switch(state) {
                    case ps2_state_nodata:
                        break;
                    case ps2_state_key:
                        hid_process_key_report(&keystate);
                        break;
                    case ps2_state_mouse:
                        hid_process_mouse_report(&mouse);
                        break;
                    case ps2_state_continued:
                        cont = 1;
                        break;
                }
            }
        } while (cont);
    }
}


static int ps2_init() {

    if (ps2_wait_for_write(10) != 0) return 0;
    ps2_write_command(0xAD);
    if (ps2_wait_for_write(1) != 0) goto ps2_timeout_error;
    ps2_write_command(0xA7);

    for (int i = 0; i < 16; i++) {
        ps2_read_data();
    }

    uintptr_t size_of_buffer = 128;
    ps2_event_queue = moe_queue_create(size_of_buffer);

    if (ps2_wait_for_write(1) != 0) goto ps2_timeout_error;
    ps2_write_command(0x60);
    if (ps2_wait_for_write(1) != 0) goto ps2_timeout_error;
    ps2_write_data(0x47);

    if (ps2_wait_for_write(1) != 0) goto ps2_timeout_error;
    ps2_write_data(0xFF);
    moe_usleep(100000);
    if (ps2_wait_for_write(1) != 0) goto ps2_timeout_error;
    ps2_write_data(0xF4);

    if (ps2_wait_for_write(1) != 0) goto ps2_timeout_error;
    ps2_write_command(0xD4);
    if (ps2_wait_for_write(1) != 0) goto ps2_timeout_error;
    ps2_write_data(0xF4);

    moe_install_irq(1, &ps2k_irq_handler);
    moe_install_irq(12, &ps2m_irq_handler);
    moe_create_thread(&ps2_hid_thread, priority_realtime, 0, "ps2.hid");

    return 1;

ps2_timeout_error:
    moe_panic("PS/2 Timeout");
}


/*********************************************************************/

void lpc_init() {
    ps2_init();
}
