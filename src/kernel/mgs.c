/*

    Minimal Graphics Subsystem

    Copyright (c) 1998,2000,2018 MEG-OS project, All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

*/
#include "moe.h"

#define DEFAULT_BGCOLOR 0xFFFFFF
#define DEFAULT_FGCOLOR 0x777777

moe_video_info_t* video;
int font_w8, line_height, font_offset, padding_x, padding_y;
int cols, rows, cursor_x, cursor_y;
uint32_t bgcolor = DEFAULT_BGCOLOR;
uint32_t fgcolor = DEFAULT_FGCOLOR;

#include "bootfont.h"

static int col_to_x(int x) {
    return padding_x + font_w * x;
}

static int row_to_y(int y) {
    return padding_y + line_height * y;
}

void mgs_fill_rect(int x, int y, int width, int height, uint32_t color) {
    int left = x, right = x+width, top = y, bottom = y+height;

    if (width < 0 || height < 0) return;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > video->res_x) right = video->res_x;
    if (bottom > video->res_y) bottom = video->res_y;

    if (left > video->res_x || top > video->res_y || right < 0 || bottom < 0) return;

    int w = right - left;
    int ppl = video->pixel_per_scan_line;
    uint32_t* p = (uint32_t*)video->vram + top * ppl;
    if (w == ppl) {
        memset32(p, color, (bottom - top) * ppl);
    } else {
        p += left;
        for (int i = top; i < bottom; i++, p+=ppl) {
            memset32(p, color, w);
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
    int ppl = video->pixel_per_scan_line;
    int w8 = (w+7)/8;

    if(x<0 || y<0) return;

    int wl = ppl - w;
    uint32_t* p = (uint32_t*)video->vram + y*ppl + x;

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

void mgs_draw_font(int x, int y, uint32_t c, uint32_t color) {
    const uint8_t* p = font_data + (c - 0x20) * font_w8 * font_h;
    mgs_draw_pattern(x, y + font_offset, font_w, font_h, p, color);
}

void putchar32(uint32_t c) {
    switch (c) {
        default:
            mgs_fill_block(cursor_x, cursor_y, 1, 1, bgcolor);
            if (c > 0x20 && c < 0x80) {
                mgs_draw_font(col_to_x(cursor_x), row_to_y(cursor_y), c, fgcolor);
            }
            cursor_x++;
            break;

        case '\n':
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

void mgs_init(moe_video_info_t* _video) {
    video = _video;

    font_w8 = (font_w+7)/8;
    line_height = font_h+((font_h*3)>>4);
    font_offset = (line_height-font_h)/2;
    padding_x = font_w * 2;
    padding_y = line_height;
    cols = (video->res_x - padding_x * 2) / font_w;
    rows = (video->res_y - padding_y * 2) / line_height;

    mgs_cls();
}
