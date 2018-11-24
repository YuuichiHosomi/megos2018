// Minimal Graphics Subsystem
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"
#include "kernel.h"

#define DEFAULT_ATTRIBUTES 0x70
// #define DEFAULT_ATTRIBUTES 0xF8

typedef struct moe_console_context_t {
    moe_dib_t *dib;
    moe_edge_insets_t edge_insets;
    int cols, rows, cursor_x, cursor_y;
    uint32_t bgcolor, fgcolor;
    uint32_t attributes;
} moe_console_context_t;

static const moe_rect_t rect_zero = {{0, 0}, {0, 0}};
static const moe_edge_insets_t edge_insets_zero = {0, 0, 0, 0};
const moe_point_t *moe_point_zero = &rect_zero.origin;
const moe_size_t *moe_size_zero = &rect_zero.size;
const moe_rect_t *moe_rect_zero = &rect_zero;
const moe_edge_insets_t *moe_edge_insets_zero = &edge_insets_zero;

extern moe_dib_t *desktop_dib;
moe_dib_t main_screen_dib;

static moe_edge_insets_t main_console_insets;
static moe_console_context_t main_console;
moe_console_context_t* current_console;

uint32_t palette[] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

#include "bootfont.h"
int font_w8, line_height, font_offset;

static int col_to_x(moe_console_context_t *self, int x) {
    return self->edge_insets.left + font_w * x;
}

static int row_to_y(moe_console_context_t *self, int y) {
    return self->edge_insets.top + line_height * y;
}

static void blt_main(moe_dib_t* dest, moe_dib_t* src, moe_point_t *origin, moe_rect_t *rect);


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
        int r = dx + w;
        int b = dy + h;
        if (r >= dest->width) w = dest->width - dx;
        if (b >= dest->height) h = dest->height - dy;
        // printf("\r{%5d %5d %5d %5d %5d %5d %5d %5d}", dx, dy, sx, sy, w, h, r, b);
        if (w <= 0 || h <= 0) return;
    }

    moe_point_t p = { dx, dy };
    moe_rect_t r = { { sx, sy }, { w, h } };
    blt_main(dest, src, &p, &r);
}

void blt_main(moe_dib_t* dest, moe_dib_t* src, moe_point_t *origin, moe_rect_t *rect) {
    int dx = origin->x, dy = origin->y;
    int sx = rect->origin.x, sy = rect->origin.y, w = rect->size.width, h = rect->size.height;

    // Transfer
    uint32_t *p = dest->dib;
    p += dx + dy * dest->delta;
    uint32_t *q = src->dib;
    q += sx + sy * src->delta;
    uintptr_t dd = dest->delta - w, sd = src->delta - w;

    if (src->flags & MOE_DIB_COLOR_KEY) { // Blt with color key
        uint32_t k = src->color_key;
        for (uintptr_t i = 0; i < h; i++) {
            #pragma clang loop vectorize(enable) interleave(enable)
            for (uintptr_t j = 0; j < w; j++) {
                uint32_t c = *q++;
                if (c != k) {
                    *p = c;
                }
                p++;
            }
            p += dd;
            q += sd;
        }
    } else {
        if (dd == 0 && sd == 0) {
            uintptr_t limit = w * h;
            #pragma clang loop vectorize(enable) interleave(enable)
            for (uintptr_t i = 0; i < limit; i++) {
                *p++ = *q++;
            }
        } else if (dd == 0) {
            for (uintptr_t i = 0; i < h; i++) {
                #pragma clang loop vectorize(enable) interleave(enable)
                for (uintptr_t j = 0; j < w; j++) {
                    *p++ = *q++;
                }
                q += sd;
            }
        } else if (sd == 0) {
            for (uintptr_t i = 0; i < h; i++) {
                #pragma clang loop vectorize(enable) interleave(enable)
                for (uintptr_t j = 0; j < w; j++) {
                    *p++ = *q++;
                }
                p += dd;
            }
        } else {
            for (uintptr_t i = 0; i < h; i++) {
                #pragma clang loop vectorize(enable) interleave(enable)
                for (uintptr_t j = 0; j < w; j++) {
                    *p++ = *q++;
                }
                p += dd;
                q += sd;
            }
        }
    }
}


void moe_fill_rect(moe_dib_t* dest, moe_rect_t *rect, uint32_t color) {

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
    uintptr_t dd = dest->delta - w;

    if (dd == 0) {
        uintptr_t limit = w * h;
        #pragma clang loop vectorize(enable) interleave(enable)
        for (int i = 0; i < limit; i++) {
            *p++ = color;
        }
    } else {
        for (int i = 0; i < h; i++) {
            #pragma clang loop vectorize(enable) interleave(enable)
            for (int j = 0; j < w; j++) {
                *p++ = color;
            }
            p += dd;
        }
    }

}


void draw_pattern(moe_dib_t *dib, moe_rect_t* rect, const uint8_t* pattern, uint32_t fgcolor) {

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


moe_point_t moe_draw_string(moe_dib_t *dib, moe_point_t *_cursor, moe_rect_t *_rect, const char *s, uint32_t color) {
    moe_rect_t cursor;
    moe_rect_t rect;
    if (_rect) {
        rect = *_rect;
    } else {
        moe_rect_t _rect = {{0, 0}, {dib->width, dib->height}};
        rect = _rect;
    }
    int right = rect.origin.x + rect.size.width;
    int bottom = rect.origin.y + rect.size.height;
    if (_cursor) {
        cursor.origin = *_cursor;
    } else {
        cursor.origin = rect.origin;
    }
    cursor.size.width = font_w;
    cursor.size.height = line_height;

    uint32_t uc = *s++;
    for (; uc; uc = *s++) {

        moe_rect_t font_rect = {{ cursor.origin.x, cursor.origin.y + font_offset}, {font_w, font_h}};
        const uint8_t* font_p = font_data + (uc - 0x20) * font_w8 * font_h;
        draw_pattern(dib, &font_rect, font_p, color);

        cursor.origin.x += cursor.size.width;

        if (cursor.origin.x >= right) {
            cursor.origin.x = rect.origin.x;
            cursor.origin.y += cursor.size.height;
        }
        if (cursor.origin.y >= bottom) break;

    }

    return cursor.origin;
}





void console_init(moe_console_context_t *self, moe_dib_t *dib, moe_edge_insets_t* insets) {
    self->dib = dib;
    if (insets) {
        self->edge_insets = *insets;
    } else {
        self->edge_insets = edge_insets_zero;
    }
    self->cols = (self->dib->width - (self->edge_insets.left + self->edge_insets.right)) / font_w;
    self->rows = (self->dib->height - (self->edge_insets.top + self->edge_insets.bottom)) / line_height;
    self->cursor_x = 0;
    self->cursor_y = 0;
}


void moe_set_console_attributes(moe_console_context_t *self, uint32_t attributes) {
    if (!self) self = current_console;
    self->attributes = attributes;
    self->fgcolor = palette[attributes & 0x0F];
    self->bgcolor = palette[(attributes >> 4) & 0x0F];
}


void putchar32(moe_console_context_t *self, uint32_t c) {
    while (self->cursor_y >= self->rows) {
        self->cursor_y--;
        moe_point_t origin = { col_to_x(self, 0), row_to_y(self, 0) };
        moe_rect_t rect = {{ col_to_x(self, 0), row_to_y(self, 1) }, { self->cols * font_w, (self->rows - 1) * line_height }};
        moe_blt(self->dib, self->dib, &origin, &rect, 0);
        moe_rect_t rect_last_line = { {col_to_x(self, 0), row_to_y(self, self->cursor_y) }, { self->cols * font_w, line_height } };
        moe_fill_rect(current_console->dib, &rect_last_line, self->bgcolor);
        moe_invalidate_screen(NULL);
    }
    switch (c) {
        default:
            if (c >= 0x20 && c < 0x80) {
                moe_rect_t rect = { { col_to_x(self, self->cursor_x), row_to_y(self, self->cursor_y) }, { font_w, line_height } };
                moe_rect_t rect_f = { { col_to_x(self, self->cursor_x), row_to_y(self, self->cursor_y) + font_offset }, { font_w, font_h } };
                const uint8_t* font_p = font_data + (c - 0x20) * font_w8 * font_h;
                moe_fill_rect(self->dib, &rect, self->bgcolor);
                draw_pattern(self->dib, &rect_f, font_p, self->fgcolor);
                moe_invalidate_screen(&rect);
            }
            self->cursor_x++;
            break;

        case '\b': // Backspace
            if (self->cursor_x > 0) {
                self->cursor_x--;
            }
            break;

        case '\r': // Carriage Return
            self->cursor_x = 0;
            break;

        case '\n': // New Line (Line Feed)
            self->cursor_x = 0;
            self->cursor_y++;
            break;
    }

    if (self->cursor_x >= self->cols) {
        self->cursor_x = 0;
        self->cursor_y++;
    }
}


// TODO:
int putchar(unsigned char c) {
    putchar32(current_console, c);
    return 1;
}

// TODO:
void mgs_cls() {
    if (desktop_dib) {
        console_init(current_console, desktop_dib, &main_console_insets);
    } else {
        console_init(current_console, &main_screen_dib, &main_console_insets);
    }

    moe_fill_rect(current_console->dib, NULL, current_console->bgcolor);
    moe_invalidate_screen(NULL);
}


void gs_init(moe_dib_t* screen) {

    main_screen_dib = *screen;

    font_w8 = (font_w + 7) / 8;
    line_height = font_h + ((font_h * 3) >> 4);
    font_offset = (line_height - font_h) / 2;

    current_console = &main_console;
    int padding_x = font_w * 2;
    int padding_y = line_height + 4;
    moe_edge_insets_t insets = { padding_y, padding_x, padding_y, padding_x };
    main_console_insets = insets;
    console_init(current_console, &main_screen_dib, &main_console_insets);
    moe_set_console_attributes(current_console, DEFAULT_ATTRIBUTES);
}