// Minimal Graphics Service
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include "moe.h"

#include "megh0816.h"

moe_bitmap_t main_screen;

void moe_blt(moe_bitmap_t* dest, moe_bitmap_t* src, moe_point_t *origin, moe_rect_t *rect, uint32_t options) {

    intptr_t dx, dy, w, h, sx, sy;
    if (origin) {
        dx = origin->x;
        dy = origin->y;
    } else {
        dx = dy = 0;
    }
    if (rect) {
        sx = rect->origin.x;
        sy = rect->origin.y;
        w = rect->size.width;
        h = rect->size.height;
    } else {
        sx = sy = 0;
        w = src->width;
        h = src->height;
    }

    // Clipping
    {
        if (dx < 0) {
            sx -= dx;
            w += dx;
            dx = 0;
        }
        if (dy < 0) {
            sy -= dy;
            h += dy;
            dy = 0;
        }
        if (w > src->width) w = src->width;
        if (h > src->height) h = src->height;
        intptr_t r = dx + w;
        intptr_t b = dy + h;
        if (r >= dest->width) w = dest->width - dx;
        if (b >= dest->height) h = dest->height - dy;
        if (w <= 0 || h <= 0) return;
    }

    // Transfer

    if (dest->flags & MOE_BMP_ROTATE) { // Rotation
        intptr_t temp = dx;
        dx = dest->height - dy;
        dy = temp;
        uint32_t *p = dest->bitmap;
        p += dx + dy * dest->delta - h;
        uint32_t *q = src->bitmap;
        q += sx + (sy + h - 1) * src->delta;
        uintptr_t sdy = src->delta, ddy = dest->delta - h;
        // #pragma clang loop vectorize(enable) interleave(enable)
        for (uintptr_t i = 0; i < w; i++) {
            uint32_t *q0 = q + i;
            for (uintptr_t j = 0; j < h; j++) {
                *p++ = *q0;
                q0 -= sdy;
            }
            p += ddy;
        }
        return;
    }

    uint32_t *p = dest->bitmap;
    p += dx + dy * dest->delta;
    uint32_t *q = src->bitmap;
    q += sx + sy * src->delta;
    uintptr_t sd = src->delta - w;
    uintptr_t dd = dest->delta - w;

    if (src->flags & MOE_BMP_ALPHA) { // ARGB Transparency
        for (uintptr_t i = 0; i < h; i++) {
            // #pragma clang loop vectorize(enable) interleave(enable)
            for (uintptr_t j = 0; j < w; j++) {
                uint8_t *p0 = (uint8_t*)p;
                uint8_t *q0 = (uint8_t*)q;
                uint8_t alpha = q0[3];
                uint8_t alpha_n = 255 - alpha;
                p0[0] = (q0[0] * alpha + p0[0] * alpha_n) / 256;
                p0[1] = (q0[1] * alpha + p0[1] * alpha_n) / 256;
                p0[2] = (q0[2] * alpha + p0[2] * alpha_n) / 256;
                p0[3] = (q0[3] * alpha + p0[3] * alpha_n) / 256;
                // p0[3] = 0;
                p++, q++;
            }
            p += dd;
            q += sd;
        }
    } else {
        if (dd == 0 && sd == 0) {
            uintptr_t limit = w * h;
            // #pragma clang loop vectorize(enable) interleave(enable)
            for (uintptr_t i = 0; i < limit; i++) {
                *p++ = *q++;
            }
        } else if (dd == 0) {
            for (uintptr_t i = 0; i < h; i++) {
                // #pragma clang loop vectorize(enable) interleave(enable)
                for (uintptr_t j = 0; j < w; j++) {
                    *p++ = *q++;
                }
                q += sd;
            }
        } else if (sd == 0) {
            for (uintptr_t i = 0; i < h; i++) {
                // #pragma clang loop vectorize(enable) interleave(enable)
                for (uintptr_t j = 0; j < w; j++) {
                    *p++ = *q++;
                }
                p += dd;
            }
        } else {
            for (uintptr_t i = 0; i < h; i++) {
                // #pragma clang loop vectorize(enable) interleave(enable)
                for (uintptr_t j = 0; j < w; j++) {
                    *p++ = *q++;
                }
                p += dd;
                q += sd;
            }
        }
    }
}


void moe_fill_rect(moe_bitmap_t* dest, moe_rect_t *rect, uint32_t color) {
    intptr_t dx, dy, w, h;
    if (rect) {
        dx = rect->origin.x;
        dy = rect->origin.y;
        w = rect->size.width;
        h = rect->size.height;
    } else {
        dx = dy = 0;
        w = dest->width;
        h = dest->height;
    }

    {
        if (dx < 0) {
            w += dx;
            dx = 0;
        }
        if (dy < 0) {
            h += dy;
            dy = 0;
        }
        intptr_t r = dx + w;
        intptr_t b = dy + h;
        if (r >= dest->width) w = dest->width - dx;
        if (b >= dest->height) h = dest->height - dy;
        if (w <= 0 || h <= 0) return;
    }

    if (dest->flags & MOE_BMP_ROTATE) {
        intptr_t temp = dx;
        dx = dest->height - dy - h;
        dy = temp;
        temp = w;
        w = h;
        h = temp;
    }

    uint32_t *p = dest->bitmap;
    p += dx + dy * dest->delta;
    uintptr_t dd = dest->delta - w;

    if (dd == 0) {
        uintptr_t limit = w * h;
        // #pragma clang loop vectorize(enable) interleave(enable)
        for (uintptr_t i = 0; i < limit; i++) {
            *p++ = color;
        }
    } else {
        for (uintptr_t i = 0; i < h; i++) {
            // #pragma clang loop vectorize(enable) interleave(enable)
            for (uintptr_t j = 0; j < w; j++) {
                *p++ = color;
            }
            p += dd;
        }
    }
}


void draw_pattern(moe_bitmap_t *dest, moe_rect_t* rect, const uint8_t* pattern, uint32_t fgcolor) {

    intptr_t x = rect->origin.x, y = rect->origin.y, w = rect->size.width, h = rect->size.height;
    uintptr_t delta = dest->delta;
    uintptr_t w8 = (w + 7) / 8;

    if ( x < 0 || y < 0) return;

    if (dest->flags & MOE_BMP_ROTATE) {
        y = dest->height - y - h;
        uintptr_t wl = delta - h;
        uint32_t* p = dest->bitmap + x * delta + y;

        uintptr_t l = w;
        for(uintptr_t k = 0; k < w8; k++, l -= 8) {
            uintptr_t m = (l > 8) ? 8 : l;
            // #pragma clang loop vectorize(enable) interleave(enable)
            for(uintptr_t i = 0; i < m; i++) {
                uint32_t mask = 0x80 >> i;
                // #pragma clang loop vectorize(enable) interleave(enable)
                for(intptr_t j = h - 1; j >= 0; j--, p++) {
                    if(pattern[j * w8 + k] & mask) {
                        *p = fgcolor;
                    }
                }
                p += wl;
            }
        }

    } else {
        uintptr_t wl = delta - w;
        uint32_t* p = dest->bitmap + y * delta + x;

        for(uintptr_t i = 0; i < h; i++) {
            uintptr_t l = w;
            // #pragma clang loop vectorize(enable) interleave(enable)
            for(uintptr_t k = 0; k < w8; k++, l -= 8) {
                uintptr_t m = (l > 8) ? 8 : l;
                uint8_t font_pattern = *pattern++;
                // #pragma clang loop vectorize(enable) interleave(enable)
                for(uintptr_t j = 0; j < m; j++, p++) {
                    if(font_pattern & (0x80 >> j)) {
                        *p = fgcolor;
                    }
                }
            }
            p += wl;
        }
    }

}


uint32_t main_console_bgcolor = 0xFFFFFF;
uint32_t main_console_fgcolor = 0x666666;
int main_console_cussor_x = 0;
int main_console_cussor_y = 0;

int putchar(int _c) {
    int c = _c & 0xFF;
    const int font_w = MEGH0816_width, font_h = MEGH0816_height;
    const int console_padding_h = 16, console_padding_v = 16;
    switch (c) {
        case '\n':
            main_console_cussor_x = 0;
            main_console_cussor_y ++;
            break;
        default:
        {
            moe_rect_t rect = {{console_padding_h + main_console_cussor_x * font_w, console_padding_v + main_console_cussor_y * font_h}, {font_w, font_h}};
            moe_fill_rect(&main_screen, &rect, main_console_bgcolor);
            if (c > 0x20 && c < 0x7F) {
                uint8_t *p = (void *)MEGH0816_fontdata;
                p += (c - 0x20) << 4;
                draw_pattern(&main_screen, &rect, p, main_console_fgcolor);
            }
            main_console_cussor_x ++;
        }
    }
    return 1;
}


void gs_init(moe_bitmap_t *screen) {

    main_screen = *screen;
    if (main_screen.width < main_screen.height) {
        int temp = main_screen.width;
        main_screen.width = main_screen.height;
        main_screen.height = temp;
        main_screen.flags |= MOE_BMP_ROTATE;
    }

    if (main_console_bgcolor) {
        moe_fill_rect(&main_screen, NULL, main_console_bgcolor);
    }

}
