// Minimal Window Manager
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "hid.h"

#define MAX_WINDOWS     256
#define WINDOW_TITLE_SIZE    32
#define MAX_EVENT_QUEUE 256

#define MAX(a, b)   ((a > b) ? (a) : (b))
#define MIN(a, b)   ((a < b) ? (a) : (b))

extern char *strncpy(char *s1, const char *s2, size_t n);
extern int snprintf(char* buffer, size_t n, const char* format, ...);
int getchar();
uint32_t hid_scan_to_unicode(uint8_t scan, uint8_t modifier);


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

typedef struct moe_view_t {
    moe_view_t *next;
    moe_dib_t *dib;
    moe_fifo_t *event_queue;
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
} moe_view_t;

#define WINDOW_FLAG_SAFE_MASK 0x00FFFFFF

enum {
    view_state_needs_redraw = 29,
    view_state_visible = 30,
    view_state_used = 31,
};


extern moe_dib_t main_screen_dib;

struct {
    moe_view_t *view_pool;
    moe_view_t *root;
    moe_dib_t *off_screen;
    moe_view_t *popup_barrier;
    moe_view_t *popup_window;
    moe_view_t *active_window;
    moe_view_t *captured_window;
    moe_dib_t *close_button_dib;
    moe_rect_t screen_bounds;
    moe_edge_insets_t screen_insets;
    _Atomic int next_window_id;
    _Atomic uint32_t global_flags;
    atomic_flag hierarchy_lock;
} wm_state;

moe_point_t captured_at;


enum {
    global_flag_view_redraw,
    global_flag_mouse_redraw,
};


#define MOUSE_CURSOR_WIDTH  12
#define MOUSE_CURSOR_HEIGHT  20
uint32_t mouse_cursor_palette[] = { 0x00FF00FF, 0xFF000000, 0xFFFFFFFF };
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
    while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
        io_pause();
    }
}

void unlock_hierarchy() {
    atomic_flag_clear(&wm_state.hierarchy_lock);
}


void draw_border(moe_view_t *view) {
    if (!view->dib) return;
    if ((view->flags & MOE_WS_CAPTION)) {
        moe_rect_t rect = {{0, 0}, {view->frame.size.width, title_height}};
        moe_point_t point = {20, 3};
        moe_point_t point2 = {21, 4};
        if (wm_state.active_window == view) {
            moe_fill_rect(view->dib, &rect, active_title_bgcolor);
            moe_draw_string(view->dib, &point2, &rect, view->title, active_title_shadow_color);
            moe_draw_string(view->dib, &point, &rect, view->title, active_title_fgcolor);
        } else {
            moe_fill_rect(view->dib, &rect, inactive_title_bgcolor);
            moe_draw_string(view->dib, &point, &rect, view->title, inactive_title_fgcolor);
        }

        moe_point_t origin_close = {view->frame.size.width - CLOSE_BUTTON_SIZE - 12, (title_height - CLOSE_BUTTON_SIZE) / 2 + 1};
        moe_blt(view->dib, wm_state.close_button_dib, &origin_close, NULL, 0);
    }
    if ((view->flags & MOE_WS_BORDER)) {
        moe_rect_t rect1 = {{0, 0}, {view->frame.size.width, border_width_top}};
        moe_fill_rect(view->dib, &rect1, border_color);
        moe_rect_t rect2 = {{0, 0}, {border_width_left, view->frame.size.height}};
        moe_fill_rect(view->dib, &rect2, border_color);
        moe_rect_t rect3 = {{0, view->frame.size.height - border_width_bottom}, {view->frame.size.width, border_width_bottom}};
        moe_fill_rect(view->dib, &rect3, border_color);
        moe_rect_t rect4 = {{view->frame.size.width - border_width_right, 0}, {border_width_right, view->frame.size.height}};
        moe_fill_rect(view->dib, &rect4, border_color);
    }
}

void moe_view_set_title(moe_view_t *view, const char *title) {
    if (title) {
        strncpy(&view->title[0], title, WINDOW_TITLE_SIZE - 1);
    } else {
        memset(&view->title[0], 0, WINDOW_TITLE_SIZE);
    }
    if (view->flags & MOE_WS_CAPTION) {
        draw_border(view);
    }
}

moe_edge_insets_t moe_get_client_insets(moe_view_t *view) {
    int thin_insets = 4;
    int thick_insets = 8;
    uint32_t flags = view->flags;
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

moe_rect_t moe_get_window_bounds(moe_view_t *view) {
    moe_rect_t rect;
    rect.origin = *moe_point_zero;
    rect.size = view->frame.size;
    return rect;
}

moe_rect_t moe_get_client_rect(moe_view_t *view) {
    moe_rect_t rect = moe_get_window_bounds(view);
    moe_edge_insets_t insets = moe_get_client_insets(view);
    return moe_edge_insets_inset_rect(&rect, &insets);
}

void moe_blt_to_window(moe_view_t *window, moe_dib_t *dib) {
    if (!window->dib) return;
    moe_rect_t rect = moe_get_client_rect(window);
    moe_rect_t rect2 = {{0, 0}, {rect.size.width, rect.size.height}};
    moe_blt(window->dib, dib, &rect.origin, &rect2, 0);
    moe_invalidate_rect(window, NULL);
}


moe_edge_insets_t moe_add_screen_insets(moe_edge_insets_t *insets) {
    wm_state.screen_insets.top      += insets->top;
    wm_state.screen_insets.left     += insets->left;
    wm_state.screen_insets.bottom   += insets->bottom;
    wm_state.screen_insets.right    += insets->right;
    return wm_state.screen_insets;
}


moe_view_t *create_window(moe_rect_t *frame, moe_dib_t* dib, uint32_t flags, const char *title) {

    moe_view_t *self = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!atomic_bit_test_and_set(&wm_state.view_pool[i].state, view_state_used)) {
            self = &wm_state.view_pool[i];
            break;
        }
    }
    if (!self) return NULL;

    moe_view_t view;
    memset(&view, 0, sizeof(moe_view_t));

    view.window_id = atomic_fetch_add(&wm_state.next_window_id, 1);
    view.state = 1 << view_state_used;
    view.flags = flags;

    if (title) {
        strncpy(&view.title[0], title, WINDOW_TITLE_SIZE - 1);
    } else {
        memset(&view.title[0], 0, WINDOW_TITLE_SIZE);
    }

    if (dib) {
        view.dib = dib;
        if (frame) {
            view.frame = *frame;
        } else {
            moe_size_t size = { dib->width, dib->height };
            view.frame.size = size;
        }
        draw_border(&view);
    } else {
        view.dib = NULL;
        if (frame) view.frame = *frame;
    }

    *self = view;
    return self;
}


moe_view_t *moe_create_window(moe_rect_t *rect, uint32_t style, moe_window_level_t window_level, const char *title) {
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
    moe_view_t *view = create_window(&frame, dib, style, title);
    view->bgcolor = bgcolor;

    return view;
}


int moe_destroy_window(moe_view_t *self) {

    moe_hide_window(self);

    // TODO: remove resources

    atomic_bit_test_and_clear(&self->state, view_state_used);

    return 0;
}


moe_dib_t *moe_get_window_bitmap(moe_view_t *self) {
    return self->dib;
}


moe_point_t moe_convert_view_point_to_screen(moe_view_t *view, moe_point_t *point) {
    moe_point_t result = { view->frame.origin.x + point->x, view->frame.origin.y + point->y };
    return result;
}

void remove_view_hierarchy_nb(moe_view_t *view) {
    atomic_bit_test_and_clear(&view->state, view_state_visible);
    moe_view_t *p = wm_state.root;
    for (; p; p = p->next) {
        moe_view_t *next = p->next;
        if (view == next) {
            p->next = view->next;
            view->next = NULL;
            break;
        }
    }
}

void add_view_hierarchy_nb(moe_view_t *view) {
    moe_view_t *p = wm_state.root;
    moe_window_level_t lv = view->window_level;

    for (; p; p = p->next) {
        moe_view_t *next = p->next;
        if (!next || lv < next->window_level) {
            view->next = next;
            p->next = view;
            break;
        }
    }
    atomic_bit_test_and_set(&view->state, view_state_visible);
}

void moe_show_window(moe_view_t* view) {
    if (!view->window_level) return;
    lock_hierarchy();

    remove_view_hierarchy_nb(view);
    add_view_hierarchy_nb(view);

    unlock_hierarchy();

    moe_invalidate_rect(view, NULL);
}

void moe_hide_window(moe_view_t *view) {
    if (!view->window_level) return;

    if (wm_state.active_window == view) {
        wm_state.active_window = NULL;
    }
    if (wm_state.captured_window == view) {
        wm_state.captured_window = NULL;
    }

    lock_hierarchy();

    moe_rect_t rect = view->frame;

    remove_view_hierarchy_nb(view);

    unlock_hierarchy();

    moe_invalidate_rect(NULL, &rect);
}


void moe_set_active_window(moe_view_t *new_window) {
    moe_view_t *old_active = wm_state.active_window;
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


void moe_set_window_bgcolor(moe_view_t *self, uint32_t color) {
    self->bgcolor = color;
    if (self->dib) {
        moe_fill_rect(self->dib, NULL, color);
    }
    draw_border(self);
    moe_invalidate_rect(self, NULL);
}


void move_window(moe_view_t *view, moe_point_t *new_point) {
    moe_rect_t old_frame = view->frame;
    if (old_frame.origin.x != new_point->x || old_frame.origin.y != new_point->y) {
        view->frame.origin = *new_point;
        moe_invalidate_rect(NULL, &old_frame);
        moe_invalidate_rect(view, NULL);
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


void draw_window_nb(moe_view_t *view, moe_rect_t *rect, int off_screen) {
    // if (!atomic_bit_test(&view->state, view_state_visible)) return;
    moe_view_t *p = view;
    if (!rect) {
        rect = &view->frame;
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

void draw_window(moe_view_t *view, moe_rect_t *rect, int off_screen) {
    lock_hierarchy();
    draw_window_nb(view, rect, off_screen);
    unlock_hierarchy();
}

void moe_invalidate_rect(moe_view_t *view, moe_rect_t *_rect) {
    if (!view) {
        view = wm_state.root;
    }
    moe_rect_t rect;
    if (_rect) {
        rect = *_rect;
    } else {
        atomic_bit_test_and_set(&view->state, view_state_needs_redraw);
        atomic_bit_test_and_set(&wm_state.global_flags, global_flag_view_redraw);
        return;
    }
    rect.origin = moe_convert_view_point_to_screen(view, &rect.origin);

    int needs_recursive = 0;
    if (view->flags & MOE_WS_TRANSPARENT) {
        needs_recursive = 1;
    } else {
        lock_hierarchy();
        moe_view_t *p = view;
        for (; p; p = p->next) {
            if (moe_hit_test_rect(&rect, &p->frame)) {
                needs_recursive = 1;
                break;
            }
        }
        unlock_hierarchy();
    }

    if (needs_recursive) {
        draw_window(wm_state.root, &rect, 1);
    } else {
        draw_window(view, &rect, 0);
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
    moe_draw_string(dib, NULL, &rect_title, title, popup_title_color);

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
    moe_draw_string(dib, NULL, &rect_main, message, popup_message_color);

    // moe_fill_rect(dib, &rect_under, popup_under_bgcolor);
    moe_rect_t rect_button = {{0, 0}, {80, button_height}};
    rect_button.origin.x = rect_under.origin.x + (width - rect_button.size.width) / 2;
    rect_button.origin.y = rect_under.origin.y + 4;
    moe_fill_round_rect(dib, &rect_button, button_radius, active_button_bgcolor);
    // moe_draw_round_rect(dib, &rect_button, button_radius, 0);
    moe_point_t button_origin = {rect_button.origin.x + 32, rect_button.origin.y + 2};
    moe_draw_string(dib, &button_origin, &rect_button, "OK", active_button_fgcolor);

    moe_view_set_title(wm_state.popup_window, title);

    moe_show_window(wm_state.popup_barrier);
    // moe_show_window(wm_state.popup_window);
    moe_set_active_window(wm_state.popup_window);
    // getchar();
    moe_hide_window(wm_state.popup_barrier);
    moe_hide_window(wm_state.popup_window);
    return 0;
}


moe_view_t *event_test_window;

static moe_point_t mouse_point;
moe_view_t *mouse_cursor;
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


moe_view_t *moe_view_hit_test(moe_point_t *point) {

    lock_hierarchy();

    moe_view_t *found = NULL;

    moe_view_t *p = wm_state.root;
    for (; p; p = p->next) {
        if (p->window_level >= window_level_pointer) break;
        if (moe_hit_test(&p->frame, point)) found = p;
    }

    unlock_hierarchy();

    return found;
}


int moe_send_key_event(moe_hid_keyboard_report_t* report) {
    if (wm_state.active_window && wm_state.active_window->event_queue) {
        uintptr_t keyevent = report->keydata[0] | (report->modifier << 8);
        return moe_send_event(wm_state.active_window, keyevent);
    }
    return -1;
}

void init_event(moe_view_t *view) {
    if (view->event_queue) return;
    view->event_queue = moe_fifo_init(MAX_EVENT_QUEUE);
}

uintptr_t moe_get_event(moe_view_t *view) {
    init_event(view);

    for (;;) {
        uintptr_t event = moe_fifo_read(view->event_queue, 0);
        if (event) return event;

        if (atomic_bit_test_and_clear(&view->state, view_state_needs_redraw)) {
            if (view->flags & MOE_WS_TRANSPARENT) {
                draw_window_nb(wm_state.root, &view->frame, 1);
            } else {
                draw_window_nb(view, NULL, 1);
            }
        }

        moe_usleep(100);
    }

}

int moe_send_event(moe_view_t *view, uintptr_t event) {
    init_event(view);
    int result = moe_fifo_write(view->event_queue, event);
    return result;
}

uint32_t moe_translate_key_event(moe_view_t *window, uintptr_t event) {
    if (event < 0x100000) {
        uint8_t scan = event;
        uint8_t modifier = event >> 8;
        uint32_t c = hid_scan_to_unicode(scan, modifier);
        return c;
    } else {
        // TODO:
    }
    return INVALID_UNICHAR;
}


_Noreturn void window_thread(void* args) {

    moe_invalidate_rect(NULL, NULL);

    for (;;) {

        if (atomic_bit_test_and_clear(&wm_state.global_flags, global_flag_view_redraw)) {
            // Redraw Windows
            lock_hierarchy();
            moe_view_t *view = wm_state.root;
            for (; view; view = view->next) {
                if (!view->event_queue && atomic_bit_test_and_clear(&view->state, view_state_needs_redraw)) {
                    if (view->flags & MOE_WS_TRANSPARENT) {
                        draw_window_nb(wm_state.root, &view->frame, 1);
                    } else {
                        draw_window_nb(view, NULL, 1);
                    }
                }
            }
            unlock_hierarchy();
        }

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
                moe_view_t *mouse_at = moe_view_hit_test(&mouse_point);
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

                size_t size_buff = 32;
                char buff[32];
                uint32_t packed_button = (mouse.buttons & 7) + ((mouse.pressed & 7) << 4) + ((mouse.released & 7) << 8);
                snprintf(buff, size_buff, "%4d %4d %03x %d", mouse_point.x, mouse_point.y, packed_button,
                    mouse_at->window_id
                );
                moe_rect_t rect = moe_get_client_rect(event_test_window);
                moe_fill_rect(event_test_window->dib, &rect, 0x80000000);
                moe_draw_string(event_test_window->dib, NULL, &rect, buff, 0xFFFFFF77);
                moe_invalidate_rect(event_test_window, NULL);
            }

            move_window(mouse_cursor, &mouse_point);
        }
        moe_yield();
    }
}


void window_init() {
    wm_state.view_pool = mm_alloc_static(MAX_WINDOWS * sizeof(moe_view_t));
    memset(wm_state.view_pool, 0, MAX_WINDOWS * sizeof(moe_view_t));

    moe_rect_t screen_bounds = {{0, 0}, {main_screen_dib.width, main_screen_dib.height}};
    wm_state.screen_bounds = screen_bounds;
    wm_state.off_screen = moe_create_dib(&wm_state.screen_bounds.size, 0, 0);

    // Init root window
    {
        wm_state.root = create_window(&wm_state.screen_bounds, NULL, MOE_WS_CLIENT_RECT, "Desktop");
        wm_state.root->bgcolor = desktop_color;
        wm_state.root->window_level = window_level_desktop;
        atomic_bit_test_and_set(&wm_state.root->state, view_state_visible);
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

    // Event test window
    {
        int width = 160;
        moe_rect_t frame = {{wm_state.root->frame.size.width - width - 8, 32}, {width, 56}};
        event_test_window = moe_create_window(&frame, MOE_WS_CAPTION | MOE_WS_TRANSPARENT | MOE_WS_BORDER, window_level_higher, "Mouse Monitor");
        moe_set_window_bgcolor(event_test_window, 0x80000000);
        moe_show_window(event_test_window);
    }

    moe_create_thread(&window_thread, priority_high, NULL, "Window Manager");
}


int cmd_win(int argc, char **argv) {
    printf("ID context  attr     bitmap   event    frame              title\n");
    moe_view_t *view = wm_state.root;
    do {
        printf("%2d %08zx %08x %08zx %08zx %4d %4d %4d %4d %s\n", 
            view->window_id, (uintptr_t)view, view->state | view->flags,
            (uintptr_t)(view->dib ? view->dib->dib : 0), (uintptr_t)view->event_queue,
            view->frame.origin.x, view->frame.origin.y, view->frame.size.width, view->frame.size.height,
            view->title);
        view = view->next;
    } while(view);
    return 0;
}
