// Minimal Graphics Subsystem
// Copyright (c) 1998,2018 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"

#define DEFAULT_BGCOLOR 0xFFFFFF
#define DEFAULT_FGCOLOR 0x555555

moe_video_info_t* video;
int font_w8, line_height, font_offset, padding_x, padding_y;
int cols, rows, cursor_x, cursor_y, rotate;
uint32_t bgcolor = DEFAULT_BGCOLOR;
uint32_t fgcolor = DEFAULT_FGCOLOR;

#include "bootfont.h"

static int col_to_x(int x) {
    return padding_x + font_w * x;
}

static int row_to_y(int y) {
    return padding_y + line_height * y;
}

void mgs_fill_rect(int x, int y, int w, int h, uint32_t color) {

    int sw = video->res_x, sh = video->res_y;

    if (rotate) {
        int z = x;
        x = sw -y -h;
        y = z;
        z = w;
        w = h;
        h = z;
    }

    int left = x, right = x+w, top = y, bottom = y+h;

    if (w < 0 || h < 0) return;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > sw) right = sw;
    if (bottom > sh) bottom = sh;

    if (left > sw || top > sh || right < 0 || bottom < 0) return;

    int width = right - left;
    int delta = video->pixel_per_scan_line;
    uint32_t* p = (uint32_t*)video->vram + top * delta;
    if (width == delta) {
        memset32(p, color, (bottom - top) * delta);
    } else {
        p += left;
        for (int i = top; i < bottom; i++, p += delta) {
            memset32(p, color, width);
        }
    }
}

void mgs_fill_block(int x, int y, int width, int height, uint32_t color) {
    mgs_fill_rect(col_to_x(x), row_to_y(y), width * font_w, height * line_height, color);
}

void mgs_cls() {
    mgs_fill_rect(0, 0, INT32_MAX, INT32_MAX, bgcolor);
    cursor_x = 0;
    cursor_y = 0;
}

void mgs_draw_pattern(int x, int y, int w, int h, const uint8_t* pattern, uint32_t color) {
    int sw = video->res_x;
    int delta = video->pixel_per_scan_line;
    int w8 = (w+7)/8;

    if ( x < 0 || y < 0) return;

    if (rotate) {
        y = sw -y -h;
        int wl = delta - h;
        uint32_t* p = (uint32_t*)video->vram + x * delta + y;

        int l=w;
        for(int k=0; k<w8; k++, l-=8) {
            int m = (l>8) ? 8 : l;
            for(int i=0; i<m; i++) {
                uint32_t mask = 0x80>>i;
                for(int j=h-1; j>=0; j--,p++) {
                    if(pattern[j*w8+k] & mask) {
                        *p = color;
                    }
                }
                p += wl;
            }
        }

    } else {
        int wl = delta - w;
        uint32_t* p = (uint32_t*)video->vram + y * delta + x;

        for(int i=0; i<h; i++) {
            int l=w;
            for(int k=0; k<w8; k++, l-=8) {
                int m = (l>8) ? 8 : l;
                uint8_t font_pattern = *pattern++;
                for(int j=0; j<m; j++, p++) {
                    if(font_pattern & (0x80>>j)) {
                        *p = color;
                    }
                }
            }
            p += wl;
        }
    }

}

void mgs_draw_font(int x, int y, uint32_t c, uint32_t color) {
    const uint8_t* p = font_data + (c - 0x20) * font_w8 * font_h;
    mgs_draw_pattern(x, y + font_offset, font_w, font_h, p, color);
}

void putchar32(uint32_t c) {
    if (cursor_y >= rows) {
        if (rotate) {
            cursor_y = 0;
        } else {
            cursor_y = rows - 1;
            uintptr_t lh = line_height * video->pixel_per_scan_line;
            int left = col_to_x(0), top = row_to_y(0);
            uint32_t* p = video->vram;
            uint32_t* p0 = p + left + top * video->pixel_per_scan_line;
            uintptr_t th = (rows - 1) * lh;
            memcpy(p0, p0 + lh, th * sizeof(uint32_t));
            memset32(p0 + th, bgcolor, lh);
        }
    }
    switch (c) {
        default:
            mgs_fill_block(cursor_x, cursor_y, 1, 1, bgcolor);
            if (c > 0x20 && c < 0x80) {
                mgs_draw_font(col_to_x(cursor_x), row_to_y(cursor_y), c, fgcolor);
            }
            cursor_x++;
            break;

        case '\b': // Backspace
            if (cursor_x > 0) {
                cursor_x--;
            }
            break;

        case '\r': // Carriage Return
            cursor_x = 0;
            break;

        case '\n': // New Line (Line Feed)
            cursor_x = 0;
            cursor_y++;
            break;
    }

    if (cursor_x >= cols) {
        cursor_x = 0;
        cursor_y++;
    }
}

int putchar(unsigned char c) {
    putchar32(c);
    return 1;
}

void mgs_bsod() {
    bgcolor = 0x000077;
    fgcolor = 0xFFFFFF;
    mgs_cls();
}

void mgs_init(moe_video_info_t* _video) {
    video = _video;

    font_w8 = (font_w+7)/8;
    line_height = font_h+((font_h*3)>>4);
    font_offset = (line_height-font_h)/2;
    padding_x = font_w * 2;
    padding_y = line_height;

    rotate = (video->res_x < video->res_y);
    // rotate = 1;

    if (rotate) {
        cols = (video->res_y - padding_x * 2) / font_w;
        rows = (video->res_x - padding_y * 2) / line_height;
    } else {
        cols = (video->res_x - padding_x * 2) / font_w;
        rows = (video->res_y - padding_y * 2) / line_height;
    }

    mgs_cls();
}
