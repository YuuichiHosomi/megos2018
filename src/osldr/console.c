// Graphical Console for EFI
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: MIT
#include "efi.h"
#include "osldr.h"

#define	DEFAULT_COLOR	0x07

#include "megh0816.h"
#define FONT_PROPERTY(x) MEGH0816_ ## x

#include "cp932.h"

void *memset(void *, int, size_t);

static void *malloc(size_t n) {
    void *result = 0;
    EFI_STATUS status = gST->BootServices->AllocatePool(EfiBootServicesData, n, &result);
    if(EFI_ERROR(status)){
        return 0;
    }
    return result;
}

void *blt_buffer = NULL;


typedef struct {
    SIMPLE_TEXT_OUTPUT_MODE mode;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* text;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
    uint32_t fgcolor, bgcolor;
    intptr_t cols, rows, padding_x, padding_y, portrait, gpdmicro_landscape;
    intptr_t font_w, font_h, font_w8, line_height, font_offset;
    const uint8_t *font_data;
    uint8_t mode_cols, mode_rows;
} ATOP_Context;

typedef struct {
    uint8_t cols, rows;
} coords;

static coords mode_templates[] = {
    { 80, 25 },
    { 80, 50 },
    { 100, 31 },
    { 0, 0 },
};


uint32_t palette[] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};


typedef struct {
    uint16_t begin, end;
    uint32_t off;
} fontx2_zn_table;

typedef struct {
    uint8_t *rawPtr;
    fontx2_zn_table *table;
    intptr_t font_w, font_h, font_w8, tbl_cnt;
} fontx2_zn_font;

static uint16_t *uni2ansi_tbl = NULL;
static fontx2_zn_font font_zn;

EFI_STATUS cp932_tbl_init(struct iovec table_bin_vec) {

    const size_t sizeof_unitable = 0x20000;
    uni2ansi_tbl = malloc(sizeof_unitable);
    if (!uni2ansi_tbl) return EFI_OUT_OF_RESOURCES;
    memset(uni2ansi_tbl, 0, sizeof_unitable);

    uint16_t cp932_code = 0x8140;
    uint16_t *p = table_bin_vec.iov_base;
    size_t length = table_bin_vec.iov_len / 2;
    for (int i = 0; i < length; i++, p++){
        uni2ansi_tbl[*p] = cp932_code;
        cp932_code++;
        if ((cp932_code & 0xFF) == 0x7F) {
            cp932_code++;
        } else if((cp932_code & 0xFF) >= 0xFD) {
            cp932_code += (0x140 - 0xFD);
            if( cp932_code == 0xA040) {
                cp932_code = 0xE040;
            }
        }
    }

    return EFI_SUCCESS;
}

EFI_STATUS cp932_font_init(struct iovec font_vec) {

    font_zn.rawPtr	= font_vec.iov_base;
    font_zn.font_w	= font_zn.rawPtr[0x0E];
    font_zn.font_h	= font_zn.rawPtr[0x0F];
    font_zn.font_w8 = (font_zn.font_w + 7) >> 3;
    font_zn.tbl_cnt	= font_zn.rawPtr[0x11];
    font_zn.table = malloc(sizeof(fontx2_zn_table) * font_zn.tbl_cnt);
    if(!font_zn.table) return EFI_OUT_OF_RESOURCES;

    int sizeof_font = font_zn.font_w8*font_zn.font_h;

    uint32_t base = 0x12 + font_zn.tbl_cnt * 4;
    for (int i = 0; i < font_zn.tbl_cnt; i++) {
        int a = i * 4 + 0x12;
        font_zn.table[i].begin	= font_zn.rawPtr[a+1] * 256 + font_zn.rawPtr[a + 0];
        font_zn.table[i].end	= font_zn.rawPtr[a+3] * 256 + font_zn.rawPtr[a + 2];
        font_zn.table[i].off	= base;
        base += sizeof_font * (font_zn.table[i].end - font_zn.table[i].begin + 1);
    }

    return EFI_SUCCESS;
}


moe_bitmap_t main_screen;
moe_bitmap_t back_buffer;

void moe_bitmap_init(moe_bitmap_t *self, uint32_t *bitmap, int width, int height, int delta, int flags) {
    self->bitmap = bitmap;
    self->width = width;
    self->height = height;
    self->delta = delta;
    self->flags = flags;
}

void moe_blt(moe_bitmap_t* dest, moe_bitmap_t* src, moe_point_t *origin, moe_rect_t *rect, uint32_t options) {

    moe_bitmap_t temp_dest;
    if (!dest) dest = &main_screen;
    if (!src) src = &main_screen;

    int rotate = 0;
    if (dest->flags & MOE_BMP_ROTATE) {
        if (src->flags & MOE_BMP_IGNORE_ROTATE) {
            temp_dest = *dest;
            int temp = temp_dest.width;
            temp_dest.width = temp_dest.height;
            temp_dest.height = temp;
            temp_dest.flags &= ~MOE_BMP_ROTATE;
            dest = &temp_dest;
        } else {
            rotate = 1;
        }
    }

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

    if (rotate) {
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


void moe_fill_rect(moe_bitmap_t* dest, moe_rect_t *rect, uint32_t color) {

    if (!dest) dest = &main_screen;

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
        #pragma clang loop vectorize(enable) interleave(enable)
        for (uintptr_t i = 0; i < limit; i++) {
            *p++ = color;
        }
    } else {
        for (uintptr_t i = 0; i < h; i++) {
            #pragma clang loop vectorize(enable) interleave(enable)
            for (uintptr_t j = 0; j < w; j++) {
                *p++ = color;
            }
            p += dd;
        }
    }
}


void draw_pattern(moe_bitmap_t *dest, moe_rect_t* rect, const uint8_t* pattern, uint32_t fgcolor) {

    if (!dest) dest = &main_screen;

    intptr_t x = rect->origin.x, y = rect->origin.y, w = rect->size.width, h = rect->size.height;
    uintptr_t delta = dest->delta;
    uintptr_t w8 = (w + 7) / 8;

    intptr_t hl = dest->height - y;
    if (hl < h) {
        h = hl;
    }

    if (x < 0 || x >= dest->width || y < 0 || y >= dest->height || h == 0) return;

    if (dest->flags & MOE_BMP_ROTATE) {
        y = dest->height - y - h;
        uintptr_t wl = delta - h;
        uint32_t* p = dest->bitmap + x * delta + y;

        uintptr_t l = w;
        for(uintptr_t k = 0; k < w8; k++, l -= 8) {
            uintptr_t m = (l > 8) ? 8 : l;
            for(uintptr_t i = 0; i < m; i++) {
                uint32_t mask = 0x80 >> i;
                #pragma clang loop vectorize(enable) interleave(enable)
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
            for(uintptr_t k = 0; k < w8; k++, l -= 8) {
                uintptr_t m = (l > 8) ? 8 : l;
                uint8_t font_pattern = *pattern++;
                #pragma clang loop vectorize(enable) interleave(enable)
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


static void ATOP_sync_gop(ATOP_Context *self) {
    int sw = self->gop->Mode->Info->HorizontalResolution;
    int sh = self->gop->Mode->Info->VerticalResolution;
    int ppsl = self->gop->Mode->Info->PixelsPerScanLine;
    if (self->gpdmicro_landscape) {
        moe_bitmap_init(&main_screen, (void *)self->gop->Mode->FrameBufferBase, sw, sh, ppsl, MOE_BMP_ROTATE);
    } else if (self->portrait) {
        moe_bitmap_init(&main_screen, (void *)self->gop->Mode->FrameBufferBase, sh, sw, ppsl, MOE_BMP_ROTATE);
    } else {
        moe_bitmap_init(&main_screen, (void *)self->gop->Mode->FrameBufferBase, sw, sh, ppsl, 0);
    }
}


static int ATOP_col_to_x(ATOP_Context *self, int x) {
    return self->padding_x + self->font_w * x;
}

static int ATOP_row_to_y(ATOP_Context *self, int y) {
    return self->padding_y + self->line_height * y;
}

static ATOP_Context* ATOP_unboxing(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* text) {
    return (ATOP_Context *)(text->Mode);
}


static void ATOP_fill_rect(ATOP_Context *self, int x, int y, int w, int h, uint32_t color) {
    moe_rect_t rect = {{x, y}, {w, h}};
    moe_fill_rect(NULL, &rect, color);
}

static void ATOP_draw_pattern(ATOP_Context *self, int x, int y, int w, int h, const uint8_t *pattern, uint32_t color) {
    moe_rect_t rect = {{x, y}, {w, h}};
    draw_pattern(NULL, &rect, pattern, color);
}


static void ATOP_fill_block(ATOP_Context *self, int x, int y, int w, int h, uint32_t color) {
    ATOP_fill_rect(self, ATOP_col_to_x(self, x), ATOP_row_to_y(self, y), self->font_w * w, self->line_height * h, color);
}

static uint32_t ATOP_conv_glyph(uint32_t c) {
    if (c < 0x20) {
        switch (c) {
        case '\0':
        case '\b':
        case '\r':
        case '\n':
            return c;
        default:
            return INVALID_UNICHAR;
        }
    } else if (c < 0x80) {
        return c;
    } else if (c < 0x10000) {
        if (uni2ansi_tbl){
            uint32_t converted = uni2ansi_tbl[c];
            return converted ? converted : INVALID_UNICHAR;
        } else {
            return INVALID_UNICHAR;
        }
    } else {
        return INVALID_UNICHAR;
    }
}

static void ATOP_draw_char(ATOP_Context *self, int x, int y, wchar_t c, uint32_t color) {
    if (x < 0 || x >= self->cols || y < 0 || y >= self->rows) return;
    const uint8_t *font = self->font_data + (c - 0x20) * (self->font_w8 * self->font_h);
    ATOP_draw_pattern(self, ATOP_col_to_x(self, x), ATOP_row_to_y(self, y) + self->font_offset, self->font_w, self->font_h, font, color);
}

static void ATOP_draw_widechar(ATOP_Context *self, int x, int y, wchar_t c, uint32_t color){
    if (x < 0 || x >= self->cols - 1 || y < 0 || y >= self->rows) return;
    intptr_t font_size = font_zn.font_w8 * font_zn.font_h;
    for (int ti = 0; ti < font_zn.tbl_cnt; ti++) {
        if (c < font_zn.table[ti].begin) {
            break;
        } else if (c >= font_zn.table[ti].begin && c <= font_zn.table[ti].end) {
            uint8_t *p = font_zn.rawPtr + font_zn.table[ti].off + (c - font_zn.table[ti].begin) * font_size;
            ATOP_draw_pattern(self, ATOP_col_to_x(self, x), ATOP_row_to_y(self, y) + self->font_offset, font_zn.font_w, font_zn.font_h, p, color);
            return;
        }
    }
    ATOP_fill_block(self, x, y, 3, 1, color);
}

static int ATOP_set_cursor_visible(ATOP_Context *self, int visible) {
    int old_value = self->mode.CursorVisible;
    self->mode.CursorVisible = visible;
    if (self->mode.CursorColumn < self->cols && self->mode.CursorRow < self->rows) {
        int cursor_height = 2;
        int x = ATOP_col_to_x(self, self->mode.CursorColumn);
        int y = ATOP_row_to_y(self, self->mode.CursorRow) + self->line_height - cursor_height;
        int w = self->font_w;
        int h = cursor_height;
        if (visible) {
            ATOP_fill_rect(self, x, y, w, h, self->fgcolor);
        } else if (old_value) {
            ATOP_fill_rect(self, x, y, w, h, self->bgcolor);
        }
    }
    return old_value;
}


static int ATOP_check_scroll(ATOP_Context *self) {

    if (self->mode.CursorColumn >= self->cols) {
        self->mode.CursorColumn = 0;
        self->mode.CursorRow++;
    }
    if (self->mode.CursorRow >= self->rows) {
        self->mode.CursorRow = self->rows-1;

        int x0 = ATOP_col_to_x(self, 0);
        int y0 = ATOP_row_to_y(self, 0);
        int y1 = y0 + self->line_height;
        int w0 = self->cols * self->font_w;
        int h0 = (self->rows-1) * self->line_height;

        if (self->portrait) {
            int sw = self->gop->Mode->Info->HorizontalResolution;
            int x1 = sw - y1 - h0;
            int z0 = x0;
            x0 = sw - y0 - h0;
            y0 = z0;
            // self->gop->Blt(self->gop, NULL, EfiBltVideoToVideo, x1, y0, x0, y0, h0, w0, 0);
            self->gop->Blt(self->gop, blt_buffer, EfiBltVideoToBltBuffer, x1, y0, 0, 0, h0, w0, sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) * h0);
            self->gop->Blt(self->gop, blt_buffer, EfiBltBufferToVideo, 0, 0, x0, y0, h0, w0, sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) * h0);
        } else {
            self->gop->Blt(self->gop, NULL, EfiBltVideoToVideo, x0, y1, x0, y0, w0, h0, 0);
        }
        ATOP_fill_block(self, 0, self->rows-1, self->cols, 1, self->bgcolor);
    }

    return 0;
}


static EFI_STATUS ATOP_putchar(ATOP_Context *self, wchar_t c) {
    EFI_STATUS retval = EFI_SUCCESS;

    ATOP_check_scroll(self);

    uint32_t u =  ATOP_conv_glyph(c);
    if (u == INVALID_UNICHAR){
        retval = EFI_WARN_UNKOWN_GLYPH;
    }

    switch (u) {
    default:
        if (u < 0x100) {
            ATOP_fill_block(self, self->mode.CursorColumn, self->mode.CursorRow, 1, 1, self->bgcolor);
            if (c > 0x20) {
                ATOP_draw_char(self, self->mode.CursorColumn, self->mode.CursorRow, u, self->fgcolor);
            }
            self->mode.CursorColumn++;
        } else {
            ATOP_fill_block(self, self->mode.CursorColumn, self->mode.CursorRow, 2, 1, self->bgcolor);
            ATOP_draw_widechar(self, self->mode.CursorColumn, self->mode.CursorRow, u, self->fgcolor);
            self->mode.CursorColumn += 2;
        }
        break;
    case INVALID_UNICHAR:
        {
            ATOP_fill_block(self, self->mode.CursorColumn, self->mode.CursorRow, 1, 1, self->fgcolor);
            ATOP_draw_char(self, self->mode.CursorColumn, self->mode.CursorRow, '?', self->bgcolor);
            self->mode.CursorColumn++;
        }
        break;
    case '\b':
        if (self->mode.CursorColumn > 0) {
            self->mode.CursorColumn--;
        }
        break;
    case '\n':
        self->mode.CursorColumn = 0;
        self->mode.CursorRow++;
        break;
    case '\r':
        self->mode.CursorColumn = 0;
        break;
    case ZWNBSP:
        break;
    }

    return retval;
}


static EFI_STATUS EFIAPI ATOP_RESET (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN BOOLEAN ExtendedVerification
) {
    ATOP_Context *self = ATOP_unboxing(This);
    if(!self) return EFI_DEVICE_ERROR;

    ATOP_sync_gop(self);
    moe_fill_rect(NULL, NULL, 0);

    int scrW = main_screen.width;
    int scrH = main_screen.height;

    self->cols = self->mode_cols;
    if (!self->cols) self->cols = scrW / self->font_w;
    self->rows = self->mode_rows;
    if (!self->rows) self->rows = scrH / self->line_height;

    self->padding_x = ((scrW - (self->cols * self->font_w)) / 2) & ~3;
    self->padding_y = ((scrH - (self->rows * self->line_height)) / 2) & ~3;

    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI ATOP_OUTPUT_STRING (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN CONST CHAR16 *String
) {
    ATOP_Context *self = ATOP_unboxing(This);
    if(!self) return EFI_DEVICE_ERROR;

    int old_cursor_state = ATOP_set_cursor_visible(self, 0);
    EFI_STATUS retVal = 0;
    for (CONST CHAR16 *p = String; *p; p++) {
        retVal |= ATOP_putchar(self, *p);
    }
    ATOP_set_cursor_visible(self, old_cursor_state);

    return retVal;
}

static EFI_STATUS EFIAPI ATOP_TEST_STRING (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN CONST CHAR16 *String
) {
    for (CONST CHAR16* p = String; *p; p++) {
        if (ATOP_conv_glyph(*p) == INVALID_UNICHAR) {
            return EFI_UNSUPPORTED;
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI ATOP_QUERY_MODE (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN UINTN ModeNumber,
    OUT UINTN *Columns,
    OUT UINTN *Rows
) {
    ATOP_Context *self = ATOP_unboxing(This);
    if (!self) return EFI_DEVICE_ERROR;

    if (ModeNumber > self->mode.MaxMode) {
        return EFI_UNSUPPORTED;
    }

    if (ModeNumber == self->mode.Mode) {
        *Columns = self->cols;
        *Rows = self->rows;
    } else {
        coords mode = mode_templates[ModeNumber];
        *Columns = mode.cols;
        *Rows = mode.rows;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI ATOP_SET_MODE (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN UINTN ModeNumber
) {
    ATOP_Context *self = ATOP_unboxing(This);
    if (!self) return EFI_DEVICE_ERROR;

    if (ModeNumber > self->mode.MaxMode) {
        return EFI_UNSUPPORTED;
    }

    if (self->mode.Mode != ModeNumber) {
        ATOP_sync_gop(self);
        coords mode = mode_templates[ModeNumber];
        if (mode.cols > 0 && mode.rows > 0) {
            int scrW = main_screen.width;
            int scrH = main_screen.height;
            int fw = scrW / mode.cols, fh = scrH / mode.rows;
            if (fw < self->font_w || fh < self->font_h) {
                return EFI_UNSUPPORTED;
            }
        }
        self->mode.Mode = ModeNumber;
        self->mode_cols = mode.cols;
        self->mode_rows = mode.rows;
        This->Reset(This, FALSE);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI ATOP_SET_ATTRIBUTE (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN UINTN Attribute
) {
    ATOP_Context *self = ATOP_unboxing(This);
    if (!self) return EFI_DEVICE_ERROR;

    if (Attribute == 0) Attribute = DEFAULT_COLOR;

    int old_cursor_state = ATOP_set_cursor_visible(self, 0);
    This->Mode->Attribute = Attribute;
    self->bgcolor = palette[(Attribute >> 4) & 0xF];
    self->fgcolor = palette[Attribute & 0x0F];
    ATOP_set_cursor_visible(self, old_cursor_state);

    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI ATOP_CLEAR_SCREEN (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This
) {
    ATOP_Context *self = ATOP_unboxing(This);
    if (!self) return EFI_DEVICE_ERROR;

    This->SetCursorPosition(This, 0, 0);
    {
        ATOP_fill_block(self, 0, 0, self->cols, self->rows, self->bgcolor);
    }

    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI ATOP_SET_CURSOR_POSITION (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN UINTN Column,
    IN UINTN Row
) {
    ATOP_Context *self = ATOP_unboxing(This);
    if (!self) return EFI_DEVICE_ERROR;

    int old_cursor_state = ATOP_set_cursor_visible(self, 0);
    self->mode.CursorColumn = Column;
    self->mode.CursorRow = Row;
    ATOP_set_cursor_visible(self, old_cursor_state);

    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI ATOP_ENABLE_CURSOR (
    IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    IN BOOLEAN Visible
) {
    ATOP_Context *self = ATOP_unboxing(This);
    if (!self) return EFI_DEVICE_ERROR;

    ATOP_set_cursor_visible(self, Visible);

    return EFI_SUCCESS;
}


static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL static_stop;
static ATOP_Context static_context;


EFIAPI EFI_STATUS console_init(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL* gop,
    OUT EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL** result
) {

    if (!gop) return EFI_DEVICE_ERROR;

    struct iovec cp932tbl_vec = {cp932_tbl, sizeof(cp932_tbl)};
    cp932_tbl_init(cp932tbl_vec);

    ATOP_Context *ctx = &static_context;
    memset(ctx, 0, sizeof(ATOP_Context));
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *buffer = &static_stop;
    memset(buffer, 0, sizeof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL));

    ctx->text = buffer;
    ctx->gop = gop;
    ctx->mode.MaxMode = sizeof(mode_templates) / sizeof(mode_templates[0]);
    ctx->mode.Mode = -1;

    ctx->font_w = FONT_PROPERTY(width);
    ctx->font_h = FONT_PROPERTY(height);
    ctx->font_w8 = (FONT_PROPERTY(width) + 7) / 8;
    ctx->font_data = (const uint8_t *)FONT_PROPERTY(fontdata);
    ctx->line_height = FONT_PROPERTY(height) + ((FONT_PROPERTY(height) * 3) >> 4);
    ctx->font_offset = (ctx->line_height - FONT_PROPERTY(height)) / 2;

    buffer->Mode = (SIMPLE_TEXT_OUTPUT_MODE *)ctx;
    buffer->Reset = ATOP_RESET;
    buffer->OutputString = ATOP_OUTPUT_STRING;
    buffer->TestString = ATOP_TEST_STRING;
    buffer->QueryMode = ATOP_QUERY_MODE;
    buffer->SetMode = ATOP_SET_MODE;
    buffer->SetAttribute = ATOP_SET_ATTRIBUTE;
    buffer->ClearScreen = ATOP_CLEAR_SCREEN;
    buffer->SetCursorPosition = ATOP_SET_CURSOR_POSITION;
    buffer->EnableCursor = ATOP_ENABLE_CURSOR;

    //	Set rotation
    {
        UINTN sizeOfInfo;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
        gop->QueryMode(gop, 0, &sizeOfInfo, &info);
        if (info->HorizontalResolution < info->VerticalResolution) {
            ctx->portrait = 1;
        } else if (info->HorizontalResolution > info->PixelsPerScanLine) {
            // GPD Micro PC Pseudo Landscape Mode
            ctx->gpdmicro_landscape = 1;
        }
    }

    blt_buffer = malloc(sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) * gop->Mode->Info->HorizontalResolution * gop->Mode->Info->PixelsPerScanLine);

    buffer->SetAttribute(buffer, DEFAULT_COLOR);
    buffer->SetMode(buffer, 0);

    *result = buffer;
    return EFI_SUCCESS;
}
