// Virtual Shell Interface
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include "moe.h"
#include "kernel.h"
#include "hid.h"


extern int putchar(int);


static moe_queue_t *cin;


int moe_send_key_event(moe_hid_kbd_report_t *keyreport) {
    uint32_t uni = hid_usage_to_unicode(keyreport->keydata[0], keyreport->modifier);
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

_Noreturn void test_thread(void *args) {
    int k = moe_get_current_thread_id();
    int padding = 2;
    int w = 10;
    moe_rect_t rect = {{k * w, padding}, {w - padding, w - padding}};
    uint32_t color = k * 0x010101;
    for (;;) {
        moe_fill_rect(NULL, &rect, color);
        color += k;
        moe_usleep(10000);
    }
}

void shell_init() {

    cin = moe_queue_create(256);

    for (int i = 0; i < 20; i++) {
        moe_usleep(100000);
        moe_create_thread(&test_thread, 0, NULL, "test");
        putchar('.');
    }

    // 💩
    printf("\n\nUNCommand Mode");
    for (;;) {
        printf("\n# ");
        int cont = 1;
        do {
            int c = key_in(1);
            switch (c) {
                case 10:
                case 13:
                    cont = 0;
                    break;
                default:
                    putchar(c);
                    break;
            }
        } while (cont);
    }
}
