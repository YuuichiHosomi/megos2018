// Minimal Graphics Subsystem
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"
#include "kernel.h"

#define DEFAULT_ATTRIBUTES 0x07

typedef void * moe_glyph_t;
typedef int (* MOE_FONT_TEST_GLYPH)(moe_font_t *self, uint32_t code);
typedef moe_glyph_t (* MOE_FONT_GET_GLYPH)(moe_font_t *self, uint32_t code);
typedef moe_size_t (* MOE_FONT_GET_GLYPH_SIZE)(moe_font_t *self, uint32_t code);

typedef struct moe_font_t {
    MOE_FONT_TEST_GLYPH vt_test_glyph;
    MOE_FONT_GET_GLYPH vt_get_glyph;
    MOE_FONT_GET_GLYPH_SIZE vt_get_glyph_size;
    void *context;
    int ex, em, line_height, font_offset, font_bytes;
    uint32_t flags;
} moe_font_t;

typedef struct moe_console_context_t {
    moe_window_t *window;
    moe_dib_t *dib;
    moe_font_t *font;
    moe_edge_insets_t edge_insets;
    int cols, rows, cursor_x, cursor_y;
    uint32_t bgcolor, fgcolor;
    uint32_t attributes;
    int cursor_visible;
} moe_console_context_t;


static const moe_rect_t rect_zero = {{0, 0}, {0, 0}};
static const moe_edge_insets_t edge_insets_zero = {0, 0, 0, 0};
const moe_point_t *moe_point_zero = &rect_zero.origin;
const moe_size_t *moe_size_zero = &rect_zero.size;
const moe_rect_t *moe_rect_zero = &rect_zero;
const moe_edge_insets_t *moe_edge_insets_zero = &edge_insets_zero;

moe_dib_t main_screen_dib;

static moe_edge_insets_t main_console_insets;
static moe_console_context_t main_console;
moe_console_context_t* current_console;

uint32_t palette[] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

#include "bootfont.h"
#include "msgrfont.h"
#include "smallfont.h"

moe_font_t system_font, smallfont, msgrfont;


int test_glyph(moe_font_t *self, uint32_t code) {
    if (self->vt_test_glyph) {
        return self->vt_test_glyph(self, code);
    } else {
        return (code >= 0x20) && (code < 0x80);
    }
}

moe_glyph_t get_glyph(moe_font_t *self, uint32_t code) {
    if (self->vt_get_glyph) {
        return self->vt_get_glyph(self, code);
    } else {
        uint8_t *p = self->context;
        return p + (code - 0x20) * self->font_bytes;
    }
}

moe_size_t get_glyph_size(moe_font_t *self, uint32_t code) {
    if (self->vt_get_glyph_size) {
        return self->vt_get_glyph_size(self, code);
    } else {
        moe_size_t size = { self->ex, self->em };
        return size;
    }
}

moe_size_t draw_glyph(moe_font_t *self, moe_dib_t *dib, moe_point_t *origin, uint32_t code, uint32_t color) {
    moe_size_t size = {0,0};
    return size;
}

void init_simple_font(moe_font_t *self, int width, int height, int line_height, uint8_t *data, uint32_t flags) {
    int font_w8 = (width + 7) >> 3;
    self->vt_test_glyph = NULL;
    self->vt_get_glyph = NULL;
    self->vt_get_glyph_size = NULL;
    self->context = data;
    self->ex = width;
    self->em = height;
    self->font_bytes = font_w8 * height;
    if (line_height) {
        self->line_height = line_height;
    } else {
        self->line_height = height + ((height * 3) >> 4);
    }
    self->font_offset = (self->line_height - self->em) / 2;
    self->flags = flags;
}

moe_font_t *moe_get_system_font(int type) {
    switch (type) {
        case 1:
            return &smallfont;

        case 2:
            return &msgrfont;

        default:
            return &system_font;
    }
}


moe_rect_t moe_edge_insets_inset_rect(moe_rect_t *_rect, moe_edge_insets_t *insets) {
    moe_rect_t rect = *_rect;
    rect.origin.x += insets->left;
    rect.origin.y += insets->top;
    rect.size.width -= (insets->left + insets->right);
    rect.size.height -= (insets->top + insets->bottom);
    return rect;
}


static int col_to_x(moe_console_context_t *self, int x) {
    return self->edge_insets.left + self->font->ex * x;
}

static int row_to_y(moe_console_context_t *self, int y) {
    return self->edge_insets.top + self->font->line_height * y;
}


moe_dib_t *moe_create_dib(moe_size_t *size, uint32_t flags, uint32_t color) {

    moe_dib_t *self = mm_alloc_static(sizeof(moe_dib_t));

    if ((flags & MOE_DIB_UNMANAGED) == 0) {
        size_t dibsz = size->width * size->height * sizeof(uint32_t);
        uint32_t *bitmap = mm_alloc_static(dibsz);
        self->dib = bitmap;
        memset32(bitmap, color, size->width * size->height);
    } else {
        self->dib = 0;
    }
    self->width = size->width;
    self->height = size->height;
    self->delta = size->width;
    self->flags = flags;
    if (flags & MOE_DIB_COLOR_KEY) {
        self->color_key = color;
    }
    return self;
}


// void blt_test(moe_dib_t* dest, moe_dib_t* src, moe_point_t *origin, moe_rect_t *rect) {
//     unsigned dx = origin->x, dy = origin->y;
//     unsigned sx = rect->origin.x, sy = rect->origin.y, w = rect->size.width, h = rect->size.height;

//     uint32_t *p = dest->dib;
//     p += dx + dy * dest->delta;
//     uint32_t *q = src->dib;
//     q += sx + sy * src->delta;
//     uintptr_t dd = dest->delta - w, sd = src->delta - w;

//     if (1) {

//         for (uintptr_t i = 0; i < h; i++) {
//             #pragma clang loop vectorize(enable) interleave(enable)
//             for (uintptr_t j = 0; j < w; j++) {
//                 uint8_t *p0 = (uint8_t*)p;
//                 uint8_t *q0 = (uint8_t*)q;
//                 uint8_t alpha = q0[3];
//                 uint8_t alpha_n = 255 - alpha;
//                 p0[0] = (q0[0] * alpha + p0[0] * alpha_n) / 256;
//                 p0[1] = (q0[1] * alpha + p0[1] * alpha_n) / 256;
//                 p0[2] = (q0[2] * alpha + p0[2] * alpha_n) / 256;
//                 p0[3] = (q0[3] * alpha + p0[3] * alpha_n) / 256;
//                 // p0[3] = 0;
//                 p++, q++;
//             }
//             p += dd;
//             q += sd;
//         }

//     }
// }


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

    // Transfer
    uint32_t *p = dest->dib;
    p += dx + dy * dest->delta;
    uint32_t *q = src->dib;
    q += sx + sy * src->delta;
    uintptr_t dd = dest->delta - w, sd = src->delta - w;

    if (src->flags & MOE_DIB_ALPHA) { // ARGB Transparency
        for (uintptr_t i = 0; i < h; i++) {
            #pragma clang loop vectorize(enable) interleave(enable)
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
    } else if (src->flags & MOE_DIB_COLOR_KEY) { // Blt with color key
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

void moe_blend_rect(moe_dib_t *dest, moe_rect_t *rect, uint32_t color) {
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

    unsigned alpha = (color & 0xFF000000) >> 24;
    unsigned alpha_n = 255 - alpha;
    uint8_t _b = color & 0xFF;
    uint8_t _g = (color >> 8) & 0xFF;
    uint8_t _r = (color >> 16) & 0xFF;

        for (int i = 0; i < h; i++) {
            #pragma clang loop vectorize(enable) interleave(enable)
            for (int j = 0; j < w; j++) {
                uint8_t *p0 = (uint8_t*)p;
                p0[0] = (_b * alpha + p0[0] * alpha_n) / 256;
                p0[1] = (_g * alpha + p0[1] * alpha_n) / 256;
                p0[2] = (_r * alpha + p0[2] * alpha_n) / 256;
                p0[3] = (alpha * alpha + p0[3] * alpha_n) / 256;
                p++;
            }
            p += dd;
        }

}


void moe_draw_pixel(moe_dib_t* dest, int dx, int dy, uint32_t color) {
    moe_point_t point = {dx, dy};
    moe_draw_multi_pixels(dest, 1, &point, color);
}


void moe_draw_multi_pixels(moe_dib_t *dest, size_t count, moe_point_t *points, uint32_t color) {
    for (int i = 0; i < count; i++) {
        int dx = points[i].x, dy = points[i].y;
        if (dx >= 0 && dy >= 0 && dx < dest->width && dest->height) {
            uint32_t *p = dest->dib + dx + dy * dest->delta;
            *p = color;
        }
    }
}


void draw_hline(moe_dib_t *dest, int x, int y, int width, uint32_t color) {
    int dx = x, dy = y, w = width;

    {
        if (dy < 0 || dy >= dest->height) return;
        if (dx < 0) {
            w += dx;
            dx = 0;
        }
        int r = dx + w;
        if (r >= dest->width) w = dest->width - dx;
        if (w <= 0) return;
    }

    uint32_t *p = dest->dib;
    p += dx + dy * dest->delta;

    #pragma clang loop vectorize(enable) interleave(enable)
    for (int j = 0; j < w; j++) {
        *p++ = color;
    }
}


void draw_vline(moe_dib_t *dest, int x, int y, int height, uint32_t color) {
    int dx = x, dy = y, h = height, dd = dest->delta;

    {
        if (dx < 0 || dx >= dest->width) return;
        if (dy < 0) {
            h += dy;
            dy = 0;
        }
        int b = dy + h;
        if (b >= dest->height) h = dest->height - dy;
        if (h <= 0) return;
    }

    uint32_t *p = dest->dib;
    p += dx + dy * dd;

    for (int j = 0; j < h; j++) {
        *p = color;
        p += dd;
    }
}


void moe_fill_round_rect(moe_dib_t* dest, moe_rect_t *rect, int radius, uint32_t color) {

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

    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;

    int lh = h - radius * 2;
    if (lh > 0) {
        moe_rect_t rect_line = {{dx, dy + radius}, {w, lh}};
        moe_fill_rect(dest, &rect_line, color);
    }

    int d = 1 - radius, dh = 3, dd = 5 - 2 * radius;
    int cx = 0, cy = radius;
    int bx, by, dw, qh = h - 1;

    for (; cx <= cy; cx++) {
        if (d < 0) {
            d += dh;
            dd += 2;
        } else {
            d += dd;
            dh += 2;
            dd += 4;
            cy--;
        }

        bx = radius - cy, by = radius - cx;
        {
            dw = w - bx * 2;
            draw_hline(dest, dx + bx, dy + by, dw, color);
            draw_hline(dest, dx + bx, dy + qh - by, dw, color);
        }

        bx = radius - cx, by = radius - cy;
        {
            dw = w - bx * 2;
            draw_hline(dest, dx + bx, dy + by, dw, color);
            draw_hline(dest, dx + bx, dy + qh - by, dw, color);
        }
    }
}

void moe_draw_round_rect(moe_dib_t* dest, moe_rect_t *rect, int radius, uint32_t color) {

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

    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;

    int lh = h - radius * 2;
    if (lh > 0) {
        draw_vline(dest, dx, dy + radius, lh, color);
        draw_vline(dest, dx + w - 1, dy + radius, lh, color);
    }
    int lw = w - radius * 2;
    if (lw > 0) {
        draw_hline(dest, dx + radius, dy, lw, color);
        draw_hline(dest, dx + radius, dy + h - 1, lw, color);
    }

    int d = 1 - radius, dh = 3, dd = 5 - 2 * radius;
    int cx = 0, cy = radius;
    int bx, by, dw, qh = h - 1;

    for (; cx <= cy; cx++) {
        if (d < 0) {
            d += dh;
            dd += 2;
        } else {
            d += dd;
            dh += 2;
            dd += 4;
            cy--;
        }

        bx = radius - cy, by = radius - cx;
        {
            dw = w - bx * 2 - 1;
            moe_point_t points[] = {
                {dx + bx, dy + by },
                {dx + bx, dy + qh - by },
                {dx + bx + dw, dy + by },
                {dx + bx + dw, dy + qh - by },
            };
            moe_draw_multi_pixels(dest, 4, points, color);
        }

        bx = radius - cx, by = radius - cy;
        {
            dw = w - bx * 2 - 1;
            moe_point_t points[] = {
                {dx + bx, dy + by },
                {dx + bx, dy + qh - by },
                {dx + bx + dw, dy + by },
                {dx + bx + dw, dy + qh - by },
            };
            moe_draw_multi_pixels(dest, 4, points, color);
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


moe_point_t moe_draw_string(moe_dib_t *dib, moe_font_t *font, moe_point_t *_cursor, moe_rect_t *_rect, const char *s, uint32_t color) {
    if (!font) {
        font = &system_font;
    }
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
    cursor.origin = rect.origin;
    if (_cursor) {
        cursor.origin.x += _cursor->x;
        cursor.origin.y += _cursor->y;
    }
    cursor.size.width = font->ex;
    cursor.size.height = font->line_height;

    uint32_t uc = *s++;
    for (; uc; uc = *s++) {
        if (uc == '\n') {
            cursor.origin.x = rect.origin.x;
            cursor.origin.y += cursor.size.height;
        }

        moe_rect_t font_rect;
        font_rect.origin = cursor.origin;
        font_rect.size = get_glyph_size(font, uc);

        if (cursor.origin.x + font_rect.size.width > right) {
            cursor.origin.x = rect.origin.x;
            cursor.origin.y += cursor.size.height;
        }
        if (cursor.origin.y + cursor.size.height > bottom) break;

        if (test_glyph(font, uc)) {
            draw_pattern(dib, &font_rect, get_glyph(font, uc), color);
            cursor.origin.x += font_rect.size.width;
        }
    }

    moe_point_t retval = cursor.origin;
    retval.x -= rect.origin.x;
    retval.y -= rect.origin.y;
    return retval;
}


void console_init(moe_console_context_t *self, moe_window_t* window, moe_dib_t *dib, moe_edge_insets_t* insets) {
    if (!self) self = current_console;
    self->window = window;
    self->dib = dib;
    if (insets) {
        self->edge_insets = *insets;
    } else {
        self->edge_insets = edge_insets_zero;
    }
    self->cols = (self->dib->width - (self->edge_insets.left + self->edge_insets.right)) / self->font->ex;
    self->rows = (self->dib->height - (self->edge_insets.top + self->edge_insets.bottom)) / self->font->line_height;
    self->cursor_x = 0;
    self->cursor_y = 0;
}


void moe_set_console_attributes(moe_console_context_t *self, uint32_t attributes) {
    if (!self) self = current_console;
    if (!attributes) attributes = DEFAULT_ATTRIBUTES;
    self->attributes = attributes;
    self->fgcolor = palette[attributes & 0x0F];
    self->bgcolor = palette[(attributes >> 4) & 0x0F];
}

int moe_set_console_cursor_visible(moe_console_context_t *self, int visible) {
    if (!self) self = current_console;
    int old_value = self->cursor_visible;
    self->cursor_visible = visible;
    if (self->cursor_x < self->cols && self->cursor_y < self->rows) {
        moe_rect_t rect = {{col_to_x(self, self->cursor_x), row_to_y(self, self->cursor_y) + self->font->font_offset}, {self->font->ex, self->font->em}};
        if (visible) {
            moe_fill_rect(self->dib, &rect, self->fgcolor);
            if (self->window) {
                moe_invalidate_rect(self->window, &rect);
            }
        } else if (old_value) {
            moe_fill_rect(self->dib, &rect, self->bgcolor);
            if (self->window) {
                moe_invalidate_rect(self->window, &rect);
            }
        }
    }
    return old_value;
}


void putchar32(moe_console_context_t *self, uint32_t c) {
    if (!self) self = current_console;
    moe_font_t *font = self->font;
    int old_cursor_state = moe_set_console_cursor_visible(self, 0);
    while (self->cursor_y >= self->rows) {
        self->cursor_y--;
        moe_point_t origin = {col_to_x(self, 0), row_to_y(self, 0) };
        moe_rect_t rect = {{col_to_x(self, 0), row_to_y(self, 1) }, { self->cols * font->ex, (self->rows - 1) * font->line_height }};
        moe_blt(self->dib, self->dib, &origin, &rect, 0);
        moe_rect_t rect_last_line = { {col_to_x(self, 0), row_to_y(self, self->cursor_y) }, { self->cols * font->ex, font->line_height } };
        moe_fill_rect(current_console->dib, &rect_last_line, self->bgcolor);
        if (self->window) {
            moe_invalidate_rect(self->window, NULL);
        }
    }
    switch (c) {
        default:
            if (c >= 0x20 && c < 0x80) {
                moe_rect_t rect = {{col_to_x(self, self->cursor_x), row_to_y(self, self->cursor_y)}, {font->ex, font->line_height}};
                moe_rect_t rect_f = {{col_to_x(self, self->cursor_x), row_to_y(self, self->cursor_y) + font->font_offset}, {font->ex, font->em}};
                const uint8_t* font_p = get_glyph(font, c);
                moe_fill_rect(self->dib, &rect, self->bgcolor);
                draw_pattern(self->dib, &rect_f, font_p, self->fgcolor);
                if (self->window) {
                    moe_invalidate_rect(self->window, &rect);
                }
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
        
        case '\t': // Horizontal Tab
            self->cursor_x = (self->cursor_x + 7) & ~7;
            break;
    }

    if (self->cursor_x >= self->cols) {
        self->cursor_x = 0;
        self->cursor_y++;
    }
    moe_set_console_cursor_visible(self, old_cursor_state);
}


int putchar(unsigned char c) {
    putchar32(current_console, c);
    return 1;
}

void mgs_cls() {
    moe_rect_t rect = {{0, 0}, {current_console->dib->width, current_console->dib->height}};
    rect = moe_edge_insets_inset_rect(&rect, &current_console->edge_insets);
    moe_fill_rect(current_console->dib, &rect, current_console->bgcolor);
    if (current_console->window) {
        moe_invalidate_rect(current_console->window, NULL);
    }
    current_console->cursor_x = 0;
    current_console->cursor_y = 0;
}

void mgs_bsod(const char *s) {

    console_init(current_console, NULL, &main_screen_dib, &main_console_insets);
    moe_set_console_attributes(current_console, 0x1F);

    for(; *s; s++) {
        putchar(*s);
    }
}


void gs_init(moe_dib_t* screen) {

    main_screen_dib = *screen;

    init_simple_font(&system_font, bootfont_w, bootfont_h, 0, (void*)bootfont_data, 0);
    init_simple_font(&msgrfont, msgrfont_w, msgrfont_h, 0, (void*)msgrfont_data, 0);
    init_simple_font(&smallfont, smallfont_w, smallfont_h, 0, (void*)smallfont_data, 0);
    main_console.font = &msgrfont;

    current_console = &main_console;
    int padding_x = system_font.ex * 2;
    int padding_y = system_font.line_height;
    moe_edge_insets_t insets = { padding_y, padding_x, padding_y, padding_x };
    main_console_insets = insets;
    console_init(current_console, NULL, &main_screen_dib, &main_console_insets);
    moe_set_console_attributes(current_console, 0);
}
