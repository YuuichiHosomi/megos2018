// Minimal Window Manager
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "hid.h"

#define MAX_WINDOWS         256
#define WINDOW_TITLE_SIZE   32
#define MAX_EVENT_QUEUE     64
#define DEFAULT_WAIT_EVENT_TIME 1000000

#define MOE_EVENT_CHAR_MIN  0x00000001
#define MOE_EVENT_CHAR_MAX  0x0000FFFF
#define MOE_EVENT_KEY_MIN   0x00010000
#define MOE_EVENT_KEY_MAX   0x0001FFFF
#define MOE_EVENT_TIMEOUT   0x000FFFFE
#define NULL_EVENT          0x000FFFFF

uint32_t hid_usage_to_unicode(uint8_t usage, uint8_t modifier);

#define IS_NOT_RESPONDING(window) atomic_bit_test(&window->state, window_state_not_responding)


const int32_t border_width_top = 1;
const int32_t border_width_left = 1;
const int32_t border_width_bottom = 1;
const int32_t border_width_right = 1;

uint32_t border_color               = 0xFF777777;
uint32_t desktop_color              = 0xFF55AAFF;
uint32_t default_bgcolor            = 0xFFFFFFFF;
uint32_t active_title_bgcolor       = 0xFFCCCCCC;
uint32_t active_title_shadow_color  = 0xFF999999;
uint32_t active_title_fgcolor       = 0xFF000000;
uint32_t inactive_title_bgcolor     = 0xFFEEEEEE;
uint32_t inactive_title_fgcolor     = 0xFF999999;
uint32_t popup_title_color          = 0xFF000000;
uint32_t popup_bgcolor              = 0xC0FFFF77;
uint32_t popup_message_color        = 0xFF555555;
uint32_t active_button_bgcolor      = 0xFF3366FF;
uint32_t active_button_fgcolor      = 0xFFFFFFFF;
uint32_t destructive_button_bgcolor = 0xFFFF3366;
uint32_t destructive_button_fgcolor = 0xFFFFFFFF;

int button_height = 24;
int button_radius = 4;
int title_height = 24;

typedef struct moe_window_t {
    moe_window_t *next;
    moe_dib_t *dib;
    moe_fifo_t *event_queue;
    moe_view_t *view;
    void *context;
    moe_rect_t frame;
    union {
        uint32_t flags;
        struct {
            uint8_t window_level;
        };
    };
    uint32_t state;
    uint32_t bgcolor;
    int window_id;
    char title[WINDOW_TITLE_SIZE];
} moe_window_t;

enum {
    window_state_needs_redraw = 28,
    window_state_not_responding = 29,
    window_state_visible = 30,
    window_state_used = 31,
};

typedef struct moe_view_t {
    moe_window_t *window;
    moe_view_t *parent;
    moe_rect_t frame;
    union {
        uint32_t flags;
        struct {
            int hoge;
        };
    };
    uint32_t bgcolor;
    int tag;
} moe_view_t;


extern moe_dib_t main_screen_dib;

struct {
    moe_window_t *window_pool;
    moe_window_t *root;
    moe_dib_t *off_screen;
    moe_window_t *popup_barrier;
    moe_window_t *popup_window;
    moe_window_t *active_window;
    moe_window_t *captured_window;
    moe_dib_t *close_button_dib;
    moe_rect_t screen_bounds;
    moe_edge_insets_t screen_insets;
    _Atomic int next_window_id;
    _Atomic uint32_t global_flags;
    _Atomic uintptr_t hierarchy_lock;
} wm_state;

moe_point_t captured_at;


enum {
    global_flag_redraw_request,
    global_flag_mouse_redraw,
};


#define MOUSE_CURSOR_WIDTH  12
#define MOUSE_CURSOR_HEIGHT  20
uint32_t mouse_cursor_palette[] = { 0x00FF00FF, 0xFFFFFFFF, 0x80000000 };
uint8_t mouse_cursor_source[MOUSE_CURSOR_HEIGHT][MOUSE_CURSOR_WIDTH] = {
    { 1, },
    { 1, 1, },
    { 1, 2, 1, },
    { 1, 2, 2, 1, },
    { 1, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, },
    { 1, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, },
    { 1, 2, 2, 2, 1, 2, 2, 1, },
    { 1, 2, 2, 1, 0, 1, 2, 2, 1, },
    { 1, 2, 1, 0, 0, 1, 2, 2, 1, },
    { 1, 1, 0, 0, 0, 0, 1, 2, 2, 1, },
    { 0, 0, 0, 0, 0, 0, 1, 2, 2, 1, },
    { 0, 0, 0, 0, 0, 0, 0, 1, 1, },
};


#define CLOSE_BUTTON_SIZE 10
uint32_t close_button_palette[] = { 0x00000000, 0x30000000, 0x60000000, 0x90000000, };
uint8_t close_button_source[CLOSE_BUTTON_SIZE][CLOSE_BUTTON_SIZE] = {
    { 0, 1, 0, 0, 0, 0, 0, 0, 1, },
    { 1, 3, 2, 0, 0, 0, 0, 2, 3, 1, },
    { 0, 2, 3, 2, 0, 0, 2, 3, 2, },
    { 0, 0, 2, 3, 2, 2, 3, 2, },
    { 0, 0, 0, 2, 3, 3, 2, },
    { 0, 0, 0, 2, 3, 3, 2, },
    { 0, 0, 2, 3, 2, 2, 3, 2, },
    { 0, 2, 3, 2, 0, 0, 2, 3, 2, },
    { 1, 3, 2, 0, 0, 0, 0, 2, 3, 1, },
    { 0, 1, 0, 0, 0, 0, 0, 0, 1, },
};


void lock_hierarchy() {
    while (atomic_bit_test_and_set(&wm_state.hierarchy_lock, 0)) {
        io_pause();
    }
}

void unlock_hierarchy() {
    atomic_bit_test_and_clear(&wm_state.hierarchy_lock, 0);
}


void draw_border(moe_window_t *window) {
    if (!window->dib) return;
    if ((window->flags & MOE_WS_CAPTION)) {
        moe_rect_t rect = {{0, 0}, {window->frame.size.width, title_height}};
        moe_point_t point = {20, 3};
        moe_point_t point2 = {21, 4};
        moe_point_t point3;
        if (wm_state.active_window == window) {
            moe_fill_rect(window->dib, &rect, active_title_bgcolor);
            moe_draw_string(window->dib, NULL, &point2, &rect, window->title, active_title_shadow_color);
            point3 = moe_draw_string(window->dib, NULL, &point, &rect, window->title, active_title_fgcolor);
        } else {
            moe_fill_rect(window->dib, &rect, inactive_title_bgcolor);
            point3 = moe_draw_string(window->dib, NULL, &point, &rect, window->title, inactive_title_fgcolor);
            if (IS_NOT_RESPONDING(window)) {
                moe_draw_string(window->dib, NULL, &point3, &rect, " (Not Responding)", inactive_title_fgcolor);
            }
        }

        moe_point_t origin_close = {window->frame.size.width - CLOSE_BUTTON_SIZE - 12, (title_height - CLOSE_BUTTON_SIZE) / 2 + 1};
        moe_blt(window->dib, wm_state.close_button_dib, &origin_close, NULL, 0);
    }
    if ((window->flags & MOE_WS_BORDER)) {
        moe_rect_t rect1 = {{0, 0}, {window->frame.size.width, border_width_top}};
        moe_fill_rect(window->dib, &rect1, border_color);
        moe_rect_t rect2 = {{0, 0}, {border_width_left, window->frame.size.height}};
        moe_fill_rect(window->dib, &rect2, border_color);
        moe_rect_t rect3 = {{0, window->frame.size.height - border_width_bottom}, {window->frame.size.width, border_width_bottom}};
        moe_fill_rect(window->dib, &rect3, border_color);
        moe_rect_t rect4 = {{window->frame.size.width - border_width_right, 0}, {border_width_right, window->frame.size.height}};
        moe_fill_rect(window->dib, &rect4, border_color);
    }
}

void moe_set_window_title(moe_window_t *window, const char *title) {
    if (title) {
        strncpy(&window->title[0], title, WINDOW_TITLE_SIZE - 1);
    } else {
        memset(&window->title[0], 0, WINDOW_TITLE_SIZE);
    }
    if (window->flags & MOE_WS_CAPTION) {
        draw_border(window);
    }
}

moe_edge_insets_t moe_get_client_insets(moe_window_t *window) {
    int thin_insets = 4;
    int thick_insets = 8;
    uint32_t flags = window->flags;
    moe_edge_insets_t insets;
    if (flags & MOE_WS_CLIENT_RECT) {
        if (flags & MOE_WS_BORDER) {
            moe_edge_insets_t insets1 = {border_width_top, border_width_left, border_width_bottom, border_width_right};
            insets = insets1;
        } else {
            moe_edge_insets_t insets2 = {0, 0, 0, 0};
            insets = insets2;
        }
        if (flags & MOE_WS_CAPTION) {
            insets.top = title_height;
        }
    } else {
        if (flags & MOE_WS_BORDER) {
            moe_edge_insets_t insets1 = {thick_insets, thick_insets, thick_insets, thick_insets};
            insets = insets1;
        } else {
            moe_edge_insets_t insets2 = {thin_insets, thin_insets, thin_insets, thin_insets};
            insets = insets2;
        }
        if (flags & MOE_WS_CAPTION) {
            insets.top = title_height + thin_insets;
        }
    }

    return insets;
}

moe_rect_t moe_get_window_bounds(moe_window_t *window) {
    moe_rect_t rect;
    rect.origin = *moe_point_zero;
    rect.size = window->frame.size;
    return rect;
}

moe_rect_t moe_get_client_rect(moe_window_t *window) {
    moe_rect_t rect = moe_get_window_bounds(window);
    moe_edge_insets_t insets = moe_get_client_insets(window);
    return moe_edge_insets_inset_rect(&rect, &insets);
}

void moe_blt_to_window(moe_window_t *window, moe_dib_t *dib) {
    if (!window->dib) return;
    moe_rect_t rect = moe_get_client_rect(window);
    moe_rect_t rect2 = {{0, 0}, {rect.size.width, rect.size.height}};
    moe_blt(window->dib, dib, &rect.origin, &rect2, 0);
    moe_invalidate_rect(window, &rect);
}


moe_edge_insets_t moe_add_screen_insets(moe_edge_insets_t *insets) {
    wm_state.screen_insets.top      += insets->top;
    wm_state.screen_insets.left     += insets->left;
    wm_state.screen_insets.bottom   += insets->bottom;
    wm_state.screen_insets.right    += insets->right;
    return wm_state.screen_insets;
}


moe_window_t *create_window(moe_rect_t *frame, moe_dib_t* dib, uint32_t flags, const char *title) {

    moe_window_t *self = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!atomic_bit_test_and_set(&wm_state.window_pool[i].state, window_state_used)) {
            self = &wm_state.window_pool[i];
            break;
        }
    }
    if (!self) return NULL;

    moe_window_t window;
    memset(&window, 0, sizeof(moe_window_t));

    window.window_id = atomic_fetch_add(&wm_state.next_window_id, 1);
    window.state = 1 << window_state_used;
    window.flags = flags;
    if (self->event_queue) {
        window.event_queue = self->event_queue;
    } else {
        window.event_queue = moe_fifo_init(MAX_EVENT_QUEUE);
    }

    if (title) {
        strncpy(&window.title[0], title, WINDOW_TITLE_SIZE - 1);
    } else {
        memset(&window.title[0], 0, WINDOW_TITLE_SIZE);
    }

    if (dib) {
        window.dib = dib;
        if (frame) {
            window.frame = *frame;
        } else {
            moe_size_t size = { dib->width, dib->height };
            window.frame.size = size;
        }
        draw_border(&window);
    } else {
        window.dib = NULL;
        if (frame) window.frame = *frame;
    }

    *self = window;
    return self;
}


moe_window_t *moe_create_window(moe_rect_t *rect, uint32_t style, moe_window_level_t window_level, const char *title) {
    uint32_t dib_flags = 0;
    uint32_t bgcolor = default_bgcolor;

    moe_rect_t frame = *rect;
    int estimated_window_id = wm_state.next_window_id;
    if (frame.origin.x < 0) frame.origin.x = estimated_window_id * 10;
    if (frame.origin.y < 0) frame.origin.y = estimated_window_id * 10;
    frame.origin.x = MAX(frame.origin.x, wm_state.screen_insets.left);
    frame.origin.y = MAX(frame.origin.y, wm_state.screen_insets.top);

    style &= 0xFFFFFF00;
    if (window_level > window_level_desktop && window_level < window_level_pointer) {
        style |= window_level;
    } else {
        style |= window_level_normal;
    }
    if (style & MOE_WS_CAPTION) {
        style |= MOE_WS_BORDER;
    }

    if (style & MOE_WS_CLIENT_RECT) {
        if (style & MOE_WS_BORDER) {
            frame.size.width += border_width_left + border_width_right;
            frame.size.height += border_width_top + border_width_bottom;
            if (style & MOE_WS_CAPTION) {
                frame.size.height += title_height - border_width_top;
            }
        }
    }

    if (style & MOE_WS_TRANSPARENT) {
        dib_flags |= MOE_DIB_ALPHA;
        bgcolor = MOE_COLOR_TRANSPARENT;
    }

    moe_dib_t *dib = moe_create_dib(&frame.size, dib_flags, bgcolor);
    moe_window_t *window = create_window(&frame, dib, style, title);
    window->bgcolor = bgcolor;

    return window;
}


int moe_destroy_window(moe_window_t *self) {

    moe_hide_window(self);

    atomic_bit_test_and_clear(&self->state, window_state_used);

    return 0;
}


moe_dib_t *moe_get_window_bitmap(moe_window_t *self) {
    return self->dib;
}


moe_point_t moe_convert_window_point_to_screen(moe_window_t *window, moe_point_t *point) {
    moe_point_t result = { window->frame.origin.x + point->x, window->frame.origin.y + point->y };
    return result;
}

void remove_window_hierarchy_nb(moe_window_t *window) {
    MOE_ASSERT(wm_state.hierarchy_lock, "WINDOW HIERARCHY VIOLATION");
    atomic_bit_test_and_clear(&window->state, window_state_visible);
    moe_window_t *p = wm_state.root;
    for (; p; p = p->next) {
        moe_window_t *next = p->next;
        if (window == next) {
            p->next = window->next;
            window->next = NULL;
            break;
        }
    }
}

void add_window_hierarchy_nb(moe_window_t *window) {
    MOE_ASSERT(wm_state.hierarchy_lock, "WINDOW HIERARCHY VIOLATION");
    moe_window_t *p = wm_state.root;
    moe_window_level_t lv = window->window_level;

    for (; p; p = p->next) {
        moe_window_t *next = p->next;
        if (!next || lv < next->window_level) {
            window->next = next;
            p->next = window;
            break;
        }
    }
    atomic_bit_test_and_set(&window->state, window_state_visible);
}

void moe_show_window(moe_window_t* window) {
    if (!window->window_level) return;
    lock_hierarchy();

    remove_window_hierarchy_nb(window);
    add_window_hierarchy_nb(window);

    unlock_hierarchy();

    moe_invalidate_rect(window, NULL);
}

void moe_hide_window(moe_window_t *window) {
    if (!window->window_level) return;

    if (wm_state.active_window == window) {
        wm_state.active_window = NULL;
    }
    if (wm_state.captured_window == window) {
        wm_state.captured_window = NULL;
    }

    lock_hierarchy();

    moe_rect_t rect = window->frame;

    remove_window_hierarchy_nb(window);

    unlock_hierarchy();

    moe_invalidate_rect(NULL, &rect);
}


void set_not_responding_window(moe_window_t *self) {
    if (!atomic_bit_test_and_set(&self->state, window_state_not_responding)) {
        if (wm_state.active_window == self) wm_state.active_window = NULL;
        draw_border(self);
        moe_invalidate_rect(self, NULL);
    }
}


void moe_set_active_window(moe_window_t *new_window) {
    if (IS_NOT_RESPONDING(new_window)) return;
    moe_window_t *old_active = wm_state.active_window;
    wm_state.active_window = new_window;
    if (old_active) {
        draw_border(old_active);
        moe_invalidate_rect(old_active, NULL);
    }
    if (new_window) {
        draw_border(wm_state.active_window);
        moe_show_window(wm_state.active_window);
    }
}


void moe_set_window_bgcolor(moe_window_t *self, uint32_t color) {
    self->bgcolor = color;
    if (self->dib) {
        moe_fill_rect(self->dib, NULL, color);
    }
    draw_border(self);
    // moe_invalidate_rect(self, NULL);
}


void move_window(moe_window_t *window, moe_point_t *new_point) {
    moe_rect_t old_frame = window->frame;
    if (old_frame.origin.x != new_point->x || old_frame.origin.y != new_point->y) {
        window->frame.origin = *new_point;
        moe_invalidate_rect(NULL, &old_frame);
        moe_invalidate_rect(window, NULL);
    }
}


int moe_hit_test(moe_rect_t *rect, moe_point_t *point) {
    int x = point->x, y = point->y, l = rect->origin.x, t = rect->origin.y;
    return (l <= x && x < (l + rect->size.width) 
        && t <= y && y < (t + rect->size.height));
}

int moe_hit_test_rect(moe_rect_t *rect1, moe_rect_t *rect2) {

    int l1 = rect1->origin.x;
    int r1 = l1 + rect1->size.width;
    int t1 = rect1->origin.y;
    int b1 = t1 + rect1->size.height;

    int l2 = rect2->origin.x;
    int r2 = l2 + rect2->size.width;
    int t2 = rect2->origin.y;
    int b2 = t2 + rect2->size.height;

    return (l1 < r2 && l2 < r1 && t1 < b2 && t2 < b1);
}


void draw_window_nb(moe_window_t *window, moe_rect_t *rect, int off_screen) {
    MOE_ASSERT(wm_state.hierarchy_lock, "WINDOW HIERARCHY VIOLATION");
    // if (!atomic_bit_test(&window->state, window_state_visible)) return;
    moe_window_t *p = window;
    if (!rect) {
        rect = &window->frame;
    }
    moe_dib_t *dib_to_draw = off_screen ? wm_state.off_screen : &main_screen_dib;
    do {
        int sx = rect->origin.x, sy = rect->origin.y;
        int sr = sx + rect->size.width, sb = sy + rect->size.height;
        if (moe_hit_test_rect(rect, &p->frame)) {
            moe_point_t blt_origin;
            moe_rect_t blt_rect;
            int wx = p->frame.origin.x, wy = p->frame.origin.y;
            int wr = wx + p->frame.size.width, wb = wy + p->frame.size.height;

            blt_origin.x = MAX(wx, sx);
            blt_origin.y = MAX(wy, sy);
            blt_rect.origin.x = (wx > sx) ? 0 : sx - wx;
            blt_rect.origin.y = (wy > sy) ? 0 : sy - wy;
            blt_rect.size.width = MIN(wr, sr) - MAX(wx, sx);
            blt_rect.size.height = MIN(wb, sb) - MAX(wy, sy);
            if (p->dib) {
                moe_blt(dib_to_draw, p->dib, &blt_origin, &blt_rect, 0);
            } else {
                blt_rect.origin = blt_origin;
                if (p->flags & MOE_WS_TRANSPARENT) {
                    moe_blend_rect(dib_to_draw, &blt_rect, p->bgcolor);
                } else {
                    moe_fill_rect(dib_to_draw, &blt_rect, p->bgcolor);
                }
            }
        }
        p = p->next;
    } while (p);
    if (off_screen) {
        moe_blt(&main_screen_dib, wm_state.off_screen, &rect->origin, rect, 0);
    }
}

void draw_window(moe_window_t *window, moe_rect_t *rect, int off_screen) {
    lock_hierarchy();
    draw_window_nb(window, rect, off_screen);
    unlock_hierarchy();
}

void moe_invalidate_rect(moe_window_t *window, moe_rect_t *_rect) {
    if (!window) {
        window = wm_state.root;
    }
    moe_rect_t rect;
    if (_rect) {
        rect = *_rect;
    } else {
        // rect = moe_get_window_bounds(window);
        atomic_bit_test_and_set(&window->state, window_state_needs_redraw);
        if (IS_NOT_RESPONDING(window)) {
            atomic_bit_test_and_set(&wm_state.global_flags, global_flag_redraw_request);
        }else{
            moe_send_event(window, NULL_EVENT);
        }
        return;
    }
    rect.origin = moe_convert_window_point_to_screen(window, &rect.origin);

    if (window->flags & MOE_WS_TRANSPARENT) {
        draw_window(wm_state.root, &rect, 1);
    } else {
        int needs_recursive = 0;
        lock_hierarchy();
        moe_window_t *p = window;
        for (; p; p = p->next) {
            if (moe_hit_test_rect(&rect, &p->frame)) {
                needs_recursive = 1;
                break;
            }
        }
        draw_window_nb(window, &rect, needs_recursive);
        unlock_hierarchy();
    }
}

moe_size_t moe_get_screen_size() {
    return wm_state.root->frame.size;
}

int moe_alert(const char *title, const char *message, uint32_t flags) {

    moe_dib_t *dib = wm_state.popup_window->dib;
    int width = dib->width;
    int height = dib->height;
    int padding_x = 12;
    moe_point_t origin = { (wm_state.root->frame.size.width - width) / 2, (wm_state.root->frame.size.height - height) / 2};
    wm_state.popup_window->frame.origin = origin;

    // moe_fill_rect(dib, NULL, popup_bgcolor);
    moe_fill_rect(dib, NULL, MOE_COLOR_TRANSPARENT);
    moe_fill_round_rect(dib, NULL, 12, popup_bgcolor);
    moe_draw_round_rect(dib, NULL, 12, border_color);

    moe_rect_t rect_title = {{padding_x, 4}, {width - padding_x * 2, title_height}};
    moe_draw_string(dib, NULL, NULL, &rect_title, title, popup_title_color);

    moe_rect_t rect_hr = {{padding_x / 2, title_height}, {width - padding_x, 1}};
    moe_fill_rect(dib, &rect_hr, border_color);

    moe_rect_t rect_under;
    rect_under.size.width = width;
    rect_under.size.height = button_height + 8;
    rect_under.origin.x = 0;
    rect_under.origin.y = height - rect_under.size.height;

    moe_rect_t rect_main = {{0, 0}, {width, height}};
    moe_edge_insets_t insets_main = { title_height + 4, padding_x, rect_under.size.height, padding_x };
    rect_main = moe_edge_insets_inset_rect(&rect_main, &insets_main);

    // moe_fill_rect(dib, &rect_main, 0x800000FF);
    moe_draw_string(dib, NULL, NULL, &rect_main, message, popup_message_color);

    // moe_fill_rect(dib, &rect_under, popup_under_bgcolor);
    moe_rect_t rect_button = {{0, 0}, {80, button_height}};
    rect_button.origin.x = rect_under.origin.x + (width - rect_button.size.width) / 2;
    rect_button.origin.y = rect_under.origin.y + 4;
    moe_fill_round_rect(dib, &rect_button, button_radius, active_button_bgcolor);
    // moe_draw_round_rect(dib, &rect_button, button_radius, 0);
    moe_point_t button_origin = {32, 2};
    moe_draw_string(dib, NULL, &button_origin, &rect_button, "OK", active_button_fgcolor);

    moe_set_window_title(wm_state.popup_window, title);

    moe_show_window(wm_state.popup_barrier);
    // moe_show_window(wm_state.popup_window);
    moe_set_active_window(wm_state.popup_window);

    //

    moe_hide_window(wm_state.popup_barrier);
    moe_hide_window(wm_state.popup_window);
    return 0;
}


static moe_point_t mouse_point;
moe_window_t *mouse_cursor;
moe_hid_mouse_report_t mouse;
void move_mouse(moe_hid_mouse_report_t* mouse_report) {
    mouse = *mouse_report;

    int new_mouse_x = mouse_point.x + mouse.x;
    int new_mouse_y = mouse_point.y + mouse.y;
    if (new_mouse_x < 0) new_mouse_x = 0;
    if (new_mouse_y < 0) new_mouse_y = 0;
    if (new_mouse_x >= wm_state.screen_bounds.size.width) new_mouse_x = wm_state.screen_bounds.size.width - 1;
    if (new_mouse_y >= wm_state.screen_bounds.size.height) new_mouse_y = wm_state.screen_bounds.size.height - 1;
    mouse_point.x = new_mouse_x;
    mouse_point.y = new_mouse_y;
    atomic_bit_test_and_set(&wm_state.global_flags, global_flag_mouse_redraw);
}


moe_window_t *moe_window_hit_test(moe_point_t *point) {

    lock_hierarchy();

    moe_window_t *found = NULL;

    moe_window_t *p = wm_state.root;
    for (; p; p = p->next) {
        if (p->window_level >= window_level_pointer) break;
        if (moe_hit_test(&p->frame, point)) found = p;
    }

    unlock_hierarchy();

    return found;
}


int moe_send_key_event(moe_hid_keyboard_report_t *report) {
    if (wm_state.active_window) {
        uintptr_t keyevent = MOE_EVENT_KEY_MIN + (report->keydata[0] | (report->modifier << 8));
        return moe_send_event(wm_state.active_window, keyevent);
    }
    return -1;
}

int moe_send_char_event(moe_window_t *window, uint32_t code) {
    return moe_send_event(window, code);
}

int moe_send_event(moe_window_t *window, uintptr_t event) {
    int result = moe_fifo_write(window->event_queue, event);
    if (result) set_not_responding_window(window);
    return result;
}

void redraw_if_needed(moe_window_t *window) {
    if (atomic_bit_test_and_clear(&window->state, window_state_needs_redraw)) {
        if (window->flags & MOE_WS_TRANSPARENT) {
            draw_window(wm_state.root, &window->frame, 1);
        } else {
            draw_window(window, NULL, 1);
        }
    }
}

uintptr_t moe_get_event(moe_window_t *window, int wait) {
    uintptr_t event;

    uint64_t wait_timer = 0;

    if (moe_fifo_wait(window->event_queue, (intptr_t*)&event, 0)) {
        if (event != NULL_EVENT) return event;
    }

    redraw_if_needed(window);

    wait_timer = (wait < 0) ? DEFAULT_WAIT_EVENT_TIME : wait;
    if (moe_fifo_wait(window->event_queue, (intptr_t*)&event, wait_timer)) {
        if (event != NULL_EVENT) return event;
    }

    redraw_if_needed(window);

    return MOE_EVENT_TIMEOUT;
}

uint32_t moe_translate_key_event(moe_window_t *window, uintptr_t event) {
    if (event > MOE_EVENT_KEY_MIN && event < MOE_EVENT_KEY_MAX) {
        uint8_t usage = event;
        uint8_t modifier = event >> 8;
        uint32_t c = hid_usage_to_unicode(usage, modifier);
        return c;
    }
    return INVALID_UNICHAR;
}


_Noreturn void window_thread(void* args) {

    for (;;) {
        // Update mouse
        if (atomic_bit_test_and_clear(&wm_state.global_flags, global_flag_mouse_redraw)) {
            if (wm_state.captured_window) {
                if (mouse.l_button) {
                    moe_point_t new_origin;
                    new_origin.x = mouse_point.x - captured_at.x;
                    int top = (wm_state.captured_window->window_level < window_level_higher) ? wm_state.screen_insets.top : 0;
                    new_origin.y = MAX(mouse_point.y - captured_at.y, top);
                    move_window(wm_state.captured_window, &new_origin);
                } else {
                    wm_state.captured_window = 0;
                }
            } else {
                moe_window_t *mouse_at = moe_window_hit_test(&mouse_point);
                moe_point_t relative_point;
                relative_point.x = mouse_point.x - mouse_at->frame.origin.x;
                relative_point.y = mouse_point.y - mouse_at->frame.origin.y;

                if (mouse.pressed & 1) {
                    if (wm_state.active_window != mouse_at) {
                        moe_set_active_window(mouse_at);
                    }
                    if ((mouse_at->flags & MOE_WS_PINCHABLE) || ((mouse_at->flags & MOE_WS_CAPTION) && relative_point.y < title_height)) {
                        wm_state.captured_window = mouse_at;
                        captured_at = relative_point;
                    }
                }
            }
            move_window(mouse_cursor, &mouse_point);
        }

        uintptr_t event;
        event = moe_get_event(wm_state.root, 0);
        event = moe_get_event(mouse_cursor, 0);

        if (atomic_bit_test_and_clear(&wm_state.global_flags, global_flag_redraw_request)) {
            int sl = -1;
            int st = -1;
            int sr = -1;
            int sb = -1;
            lock_hierarchy();
            moe_window_t *window = wm_state.root;
            for (; window; window = window->next) {
                if (IS_NOT_RESPONDING(window) && atomic_bit_test_and_clear(&window->state, window_state_needs_redraw)) {
                    moe_rect_t frame = window->frame;
                    if (frame.origin.x < 0) {
                        frame.size.width += frame.origin.x;
                        frame.origin.x = 0;
                    }
                    if (frame.origin.y < 0) {
                        frame.size.height += frame.origin.y;
                        frame.origin.y = 0;
                    }
                    if (frame.origin.x < wm_state.screen_bounds.size.width && frame.origin.y < wm_state.screen_bounds.size.height &&
                    frame.size.width > 0 && frame.size.height > 0) {
                        int right = MIN(wm_state.screen_bounds.size.width, frame.origin.x + frame.size.width);
                        int bottom = MIN(wm_state.screen_bounds.size.height, frame.origin.y + frame.size.height);
                        if (sl < 0) {
                            sl = frame.origin.x;
                        } else {
                            sl = MIN(sl, frame.origin.x);
                        }
                        if (st < 0) {
                            st = frame.origin.y;
                        } else {
                            st = MIN(st, frame.origin.y);
                        }
                        sr = MAX(sr, right);
                        sb = MAX(sb, bottom);
                    }
                }
            }
            moe_rect_t rect;
            rect.origin.x = sl;
            rect.origin.y = st;
            rect.size.width = sr - sl;
            rect.size.height = sb - st;
            if (sl >= 0 && st >= 0 && sr >= 0 && sb >= 0) {
                draw_window_nb(wm_state.root, &rect, 1);
            }
            if (0) {
                char buff[32];
                snprintf(buff, 32, "R%4d %4d %4d %4d ", rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
                moe_rect_t rect2 = {{0, 0}, {200, 20}};
                // moe_fill_rect(&main_screen_dib, &rect2, 0x00000000);
                moe_blend_rect(&main_screen_dib, &rect2, 0x40000000);
                moe_draw_string(&main_screen_dib, NULL, NULL, &rect2, buff, 0x00FFFF00);
            }
            unlock_hierarchy();
        }
        moe_usleep(1000);
    }
}


void window_init() {
    wm_state.window_pool = mm_alloc_static(MAX_WINDOWS * sizeof(moe_window_t));
    memset(wm_state.window_pool, 0, MAX_WINDOWS * sizeof(moe_window_t));

    moe_rect_t screen_bounds = {{0, 0}, {main_screen_dib.width, main_screen_dib.height}};
    wm_state.screen_bounds = screen_bounds;
    wm_state.off_screen = moe_create_dib(&wm_state.screen_bounds.size, 0, 0);

    // Init root window
    {
        wm_state.root = create_window(&wm_state.screen_bounds, NULL, MOE_WS_CLIENT_RECT, "Desktop");
        wm_state.root->bgcolor = desktop_color;
        wm_state.root->window_level = window_level_desktop;
        atomic_bit_test_and_set(&wm_state.root->state, window_state_visible);
    }

    // Init mouse
    {
        moe_rect_t frame = {{wm_state.root->frame.size.width / 2, wm_state.root->frame.size.height / 2}, { MOUSE_CURSOR_WIDTH, MOUSE_CURSOR_HEIGHT }};
        moe_dib_t *dib = moe_create_dib(&frame.size, MOE_DIB_COLOR_KEY, mouse_cursor_palette[0]);
        for (int i = 0; i < MOUSE_CURSOR_WIDTH * MOUSE_CURSOR_HEIGHT; i++) {
            dib->dib[i] = mouse_cursor_palette[mouse_cursor_source[0][i]];
        }
        mouse_cursor = create_window(&frame, dib, MOE_WS_CLIENT_RECT | MOE_WS_TRANSPARENT | window_level_pointer, "Mouse");
        mouse_point = mouse_cursor->frame.origin;

        moe_show_window(mouse_cursor);
    }

    // Prepare CLOSE BUTTON
    {
        moe_size_t size = {CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE };
        moe_dib_t *dib = moe_create_dib(&size, MOE_DIB_ALPHA, close_button_palette[0]);
        for (int i = 0; i < CLOSE_BUTTON_SIZE * CLOSE_BUTTON_SIZE; i++) {
            dib->dib[i] = close_button_palette[close_button_source[0][i]];
        }
        wm_state.close_button_dib = dib;
    }

    // Popup Placeholder
    {
        moe_rect_t frame = {{0, 0}, {480, 160}};
        wm_state.popup_window = moe_create_window(&frame, MOE_WS_PINCHABLE | MOE_WS_TRANSPARENT, window_level_popup, "POPUP");
    }

    // Popup barrier
    {
        moe_rect_t frame = wm_state.root->frame;
        wm_state.popup_barrier = create_window(&frame, NULL, MOE_WS_CLIENT_RECT | MOE_WS_TRANSPARENT | window_level_popup_barrier, "popup barrier");
        wm_state.popup_barrier->bgcolor = 0x80000000;
    }

    moe_invalidate_rect(NULL, NULL);
    moe_create_thread(&window_thread, priority_high, NULL, "Window Manager");
    // moe_create_thread(&window_delegate_thread, 0, NULL, "Window Delegate");
}


int cmd_win(int argc, char **argv) {
    printf("ID context  attr     bitmap   event    frame              title\n");
    moe_window_t *window = wm_state.root;
    do {
        printf("%2d %08zx %08x %08zx %08zx %4d %4d %4d %4d %s\n", 
            window->window_id, (uintptr_t)window, window->state | window->flags,
            (uintptr_t)(window->dib ? window->dib->dib : 0), (uintptr_t)window->event_queue,
            window->frame.origin.x, window->frame.origin.y, window->frame.size.width, window->frame.size.height,
            window->title);
        window = window->next;
    } while(window);
    return 0;
}
