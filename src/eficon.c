// EFI Console Library
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "efi.h"

#define	INVALID_UNICHAR	0xFFFE
#define	ZWNBSP	0xFEFF

extern void efi_print(CONST CHAR16 *s);
extern int vsnprintf(char *buffer, size_t limit, const char *format, va_list args);


int putchar(unsigned char c) {
    static uint8_t state[2] = {0};
    uint32_t ch = INVALID_UNICHAR;

    if (c < 0x80) { // ASCII
        if(state[0] == 0){
            ch = c;
        } else {
            state[0] = 0;
        }
    } else if(c < 0xC0) { // Trail Bytes
        if(state[0] < 0xE0) { // 2bytes (Cx 8x .. Dx Bx)
            ch = ((state[0] & 0x1F) << 6) | (c & 0x3F);
            state[0] = 0;
        } else if (state[0] < 0xF0) { // 3bytes (Ex 8x 8x)
            if(state[1] == 0){
                state[1] = c;
                ch = ZWNBSP;
            } else {
                ch = ((state[0] & 0x0F) << 12) | ((state[1] & 0x3F) << 6) | (c & 0x3F);
                state[0] = 0;
            }
        }
    } else if(c >= 0xC2 && c <= 0xEF) { // Leading Byte
        state[0] = c;
        state[1] = 0;
        ch = ZWNBSP;
    } else { // invalid sequence
        state[0] = 0;
    }

    if (ch == INVALID_UNICHAR) ch ='?';

    switch (ch) {
        default:
        {
            CHAR16 box[] = { ch, 0 };
            efi_print(box);
            return 1;
        }

        case '\n':
        {
            static CONST CHAR16 *crlf = L"\r\n";
            efi_print(crlf);
            return 1;
        }

        case ZWNBSP:
            return 0;
    }

}


#define PRINTF_BUFFER_SIZE 0x1000
static char printf_buffer[PRINTF_BUFFER_SIZE];

int vprintf(const char *format, va_list args) {
    int count = vsnprintf(printf_buffer, PRINTF_BUFFER_SIZE, format, args);
    const char *p = printf_buffer;
    for (int i = 0; i < count; i++) {
        putchar(p[i]);
    }
    return count;
}
