// Minimal Graphics Subsystem
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"

#define DEFAULT_BGCOLOR 0xFFFFFF
#define DEFAULT_FGCOLOR 0x555555

extern moe_dib_t *desktop_dib;
moe_dib_t main_screen_dib;
moe_dib_t *current_screen_dib;

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


moe_dib_t *moe_create_dib(moe_size_t *size, uint32_t flags, uint32_t color) {
    size_t dibsz = size->width * size->height * sizeof(uint32_t);
    moe_dib_t *self = mm_alloc_static(sizeof(moe_dib_t));
    uint32_t *bitmap = mm_alloc_static(dibsz);
    self->dib = bitmap;
    self->width = size->width;
    self->height = size->height;
    self->delta = size->width;
    self->flags = flags;
    if (flags & MOE_DIB_COLOR_KEY) {
        self->color_key = color;
    }
    memset32(bitmap, color, self->width * self->delta);
    return self;
}


void moe_blt(moe_dib_t* dest, moe_dib_t* src, moe_point_t *origin, moe_rect_t *rect, uint32_t options) {

    int dx, dy, w, h, sx, sy;
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
        // if (dx >= dest->width || dy >= dest->height) return;
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
        // if (sx >= src->width || sy >= src->height) return;
        if (w > src->width) w = src->width;
        if (h > src->height) h = src->height;
        int r = dx + w;
        int b = dy + h;
        // if (r < 0 || b < 0) return;
        if (r >= dest->width) w = dest->width - dx;
        if (b >= dest->height) h = dest->height - dy;
        // printf("\r{%5d %5d %5d %5d %5d %5d %5d %5d}", dx, dy, sx, sy, w, h, r, b);
        if (w <= 0 || h <= 0) return;
    }

    // Transfer
    uint32_t *p = dest->dib;
    p += dx + dy * dest->delta;
    uint32_t *q = src->dib;
    q += sx + sy * src->delta;

    if (src->flags & MOE_DIB_COLOR_KEY) { // Blt with color key
        uint32_t k = src->color_key;
        for (int i = 0; i < h; i++) {
            #pragma clang loop vectorize(enable) interleave(enable)
            for (int j = 0; j < w; j++) {
                uint32_t c = q[j];
                if (c != k) {
                    p[j] = c;
                }
            }
            p += dest->delta;
            q += src->delta;
        }
    } else {
        for (int i = 0; i < h; i++) {
            #pragma clang loop vectorize(enable) interleave(enable)
            for (int j = 0; j < w; j++) {
                p[j] = q[j];
            }
            p += dest->delta;
            q += src->delta;
        }
    }

}

// TODO:
void mgs_fill_rect(int x, int y, int width, int height, uint32_t color) {
    moe_rect_t rect = {{x, y}, {width, height}};
    moe_blt_fill(current_screen_dib, &rect, color);
    moe_invalidate_screen(&rect);
}

void moe_blt_fill(moe_dib_t* dest, moe_rect_t *rect, uint32_t color) {

    int dx, dy, w, h;
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
        int r = dx + w;
        int b = dy + h;
        if (r >= dest->width) w = dest->width - dx;
        if (b >= dest->height) h = dest->height - dy;
        if (w <= 0 || h <= 0) return;
    }

    uint32_t *p = dest->dib;
    p += dx + dy * dest->delta;

    for (int i = 0; i < h; i++) {
        #pragma clang loop vectorize(enable) interleave(enable)
        for (int j = 0; j < w; j++) {
            p[j] = color;
        }
        p += dest->delta;
    }

}


void mgs_draw_pattern(moe_dib_t* dib, moe_rect_t* rect, const uint8_t* pattern, uint32_t fgcolor) {

    int x = rect->origin.x, y = rect->origin.y, w = rect->size.width, h = rect->size.height;
    int delta = dib->delta;
    int w8 = (w+7)/8;

    if ( x < 0 || y < 0) return;

    int wl = delta - w;
    uint32_t* p = dib->dib + y * delta + x;

    for(int i=0; i<h; i++) {
        int l=w;
        #pragma clang loop vectorize(enable) interleave(enable)
        for(int k=0; k<w8; k++, l-=8) {
            int m = (l>8) ? 8 : l;
            uint8_t font_pattern = *pattern++;
            #pragma clang loop vectorize(enable) interleave(enable)
            for(int j=0; j<m; j++, p++) {
                if(font_pattern & (0x80>>j)) {
                    *p = fgcolor;
                }
            }
        }
        p += wl;
    }

}

void mgs_cls() {

    if (desktop_dib) {
        current_screen_dib = desktop_dib;
    }

    cols = (current_screen_dib->width - padding_x * 2) / font_w;
    rows = (current_screen_dib->height - padding_y * 2) / line_height;
    cursor_x = 0;
    cursor_y = 0;

    moe_blt_fill(current_screen_dib, NULL, bgcolor);
    moe_invalidate_screen(NULL);
}

void putchar32(uint32_t c) {
    if (cursor_y >= rows) {
        cursor_y--;
        moe_point_t origin = { col_to_x(0), row_to_y(0) };
        moe_rect_t rect = {{ col_to_x(0), row_to_y(1) }, { cols * font_w, (rows - 1) * line_height }};
        moe_blt(current_screen_dib, current_screen_dib, &origin, &rect, 0);
        moe_rect_t rect_last_line = { {col_to_x(0), row_to_y(cursor_y) }, { cols * font_w, line_height } };
        moe_blt_fill(current_screen_dib, &rect_last_line, bgcolor);
        moe_invalidate_screen(NULL);
    }
    switch (c) {
        default:
            if (c >= 0x20 && c < 0x80) {
                moe_rect_t rect = { { col_to_x(cursor_x), row_to_y(cursor_y) }, { font_w, line_height } };
                moe_rect_t rect_f = { { col_to_x(cursor_x), row_to_y(cursor_y) + font_offset }, { font_w, font_h } };
                const uint8_t* font_p = font_data + (c - 0x20) * font_w8 * font_h;
                moe_blt_fill(current_screen_dib, &rect, bgcolor);
                mgs_draw_pattern(current_screen_dib, &rect_f, font_p, fgcolor);
                moe_invalidate_screen(&rect);
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

void mgs_init(moe_video_info_t* _video) {

    main_screen_dib.dib = _video->vram;
    main_screen_dib.width = _video->res_x;
    main_screen_dib.height = _video->res_y;
    main_screen_dib.delta = _video->pixel_per_scan_line;
    current_screen_dib = &main_screen_dib;

    font_w8 = (font_w+7)/8;
    line_height = font_h+((font_h*3)>>4);
    font_offset = (line_height-font_h)/2;
    padding_x = font_w * 2;
    padding_y = line_height;

    mgs_cls();
}
