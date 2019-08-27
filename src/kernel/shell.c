// Pseudo Shell Interface
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include "moe.h"
#include "kernel.h"
#include "hid.h"


extern int putchar(int);


static moe_queue_t *cin;


int moe_send_key_event(moe_hid_kbd_state_t *state) {
    if (!cin) return 0;
    hid_raw_kbd_report_t report = state->current;
    uint32_t uni = hid_usage_to_unicode(report.keydata[0], report.modifier);
    if (uni != INVALID_UNICHAR) {
        return moe_queue_write(cin, uni);
    } else {
        return 0;
    }
}

uint32_t key_in(int wait) {
    intptr_t result = 0;
    if (wait) {
        while (!moe_queue_wait(cin, &result, UINT32_MAX));
    } else {
        result = moe_queue_read(cin, 0);
    }
    return result;
}

_Noreturn void fiber_test_1(void *args) {
    int k = moe_get_current_fiber_id();
    int padding = 2;
    int w = 10;
    moe_rect_t rect = {{k * w, padding}, {w - padding, w - padding}};
    uint32_t color = k * 0x010101;
    for (;;) {
        moe_fill_rect(NULL, &rect, color);
        color += k;
        moe_yield();
    }
}

_Noreturn void fiber_test_thread(void *args) {
    for (int i = 0; i < 20; i++) {
        char name[32];
        snprintf(name, 32, "test_#%d", 1 + i);
        moe_create_fiber(&fiber_test_1, NULL, 0, name);
        putchar('.');
        moe_yield();
        moe_usleep(10000);
    }
    for (;;) {
        moe_usleep(1);
        moe_yield();
    }
}


void shell_init() {

    cin = moe_queue_create(256);

    moe_create_thread(&fiber_test_thread, 0, NULL, "fiber_test");
    moe_usleep(1000000);

    // 💩 mode
    printf("\n\nUNCommand Mode");
    for (;;) {
        printf("\n# ");
        int cont = 1;
        do {
            int c = key_in(1);
            switch (c) {
                case 8:
                    printf("\b \b");
                    break;
                case 10:
                case 13:
                    cont = 0;
                    break;
                default:
                    if (c < 0x7F && c >= 0x20) {
                        putchar(c);
                    } else {
                        putchar('?');
                    }
                    break;
            }
        } while (cont);
    }
}
