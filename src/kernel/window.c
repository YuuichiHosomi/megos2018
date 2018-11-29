// Minimal Window Manager
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "hid.h"

#define MAX_WINDOWS     256
#define WINDOW_TITLE_SIZE    32

#define MAX(a, b)   ((a > b) ? (a) : (b))
#define MIN(a, b)   ((a < b) ? (a) : (b))

extern char *strncpy(char *s1, const char *s2, size_t n);
extern int snprintf(char* buffer, size_t n, const char* format, ...);
int getchar();

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
    int index;
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
    moe_edge_insets_t global_insets;
    _Atomic uint32_t global_flags;
    atomic_flag hierarchy_lock;
} wm_state;

moe_point_t captured_at;


enum {
    global_flag_screen_redraw,
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


void draw_border(moe_view_t *view) {
    if (!view->dib) return;
    if ((view->flags & WINDOW_CAPTION)) {
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
    if ((view->flags & WINDOW_BORDER)) {
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
    if (view->flags & WINDOW_CAPTION) {
        draw_border(view);
    }
}

moe_edge_insets_t moe_get_client_insets(moe_view_t *view) {
    const int thin_insets = 4;
    const int thick_insets = 8;
    uint32_t flags = view->flags;
    moe_edge_insets_t insets;
    if (flags & WINDOW_BORDER) {
        moe_edge_insets_t insets1 = {thick_insets, thick_insets, thick_insets, thick_insets};
        insets = insets1;
    } else {
        moe_edge_insets_t insets2 = {thin_insets, thin_insets, thin_insets, thin_insets};
        insets = insets2;
    }
    if (flags & WINDOW_CAPTION) {
        insets.top = title_height + thin_insets;
    }

    return insets;
}

moe_rect_t moe_get_view_bounds(moe_view_t *view) {
    moe_rect_t rect;
    rect.origin = *moe_point_zero;
    rect.size = view->frame.size;
    return rect;
}

moe_rect_t moe_get_client_rect(moe_view_t *view) {
    moe_rect_t rect = moe_get_view_bounds(view);
    moe_edge_insets_t insets = moe_get_client_insets(view);
    return moe_edge_insets_inset_rect(&rect, &insets);
}

moe_edge_insets_t moe_add_global_insets(moe_edge_insets_t *insets) {
    wm_state.global_insets.top      += insets->top;
    wm_state.global_insets.left     += insets->left;
    wm_state.global_insets.bottom   += insets->bottom;
    wm_state.global_insets.right    += insets->right;
    return wm_state.global_insets;
}


moe_view_t *moe_create_view(moe_rect_t *frame, moe_dib_t* dib, uint32_t flags, const char *title) {

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

    view.state = 1 << view_state_used;
    view.flags = (flags & WINDOW_FLAG_SAFE_MASK);
    if (!view.window_level) {
        view.window_level = window_level_normal;
    }
    view.bgcolor = default_bgcolor;
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
    if (view.flags & WINDOW_CENTER) {
        view.frame.origin.x = (wm_state.screen_bounds.size.width - view.frame.size.width) / 2;
        view.frame.origin.y = (wm_state.screen_bounds.size.height - view.frame.size.height) / 2;
    }
    view.index = self->index;

    *self = view;
    return self;
}

moe_point_t moe_convert_view_point_to_screen(moe_view_t *view, moe_point_t *point) {
    moe_point_t result = { view->frame.origin.x + point->x, view->frame.origin.y + point->y };
    return result;
}

void remove_view_hierarchy_nb(moe_view_t *view) {
    moe_view_t *p = wm_state.root;
    for (; p; p = p->next) {
        moe_view_t *next = p->next;
        if (view == next) {
            p->next = view->next;
            view->next = NULL;
            break;
        }
    }
    atomic_bit_test_and_clear(&view->state, view_state_visible);
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
    while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
        moe_yield();
    }

    remove_view_hierarchy_nb(view);
    add_view_hierarchy_nb(view);

    atomic_flag_clear(&wm_state.hierarchy_lock);

    moe_invalidate_view(view, NULL);
}

void moe_hide_window(moe_view_t *view) {
    if (!view->window_level) return;
    while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
        moe_yield();
    }

    moe_rect_t rect = view->frame;

    remove_view_hierarchy_nb(view);

    atomic_flag_clear(&wm_state.hierarchy_lock);

    moe_invalidate_screen(&rect);
}


void moe_set_active_window(moe_view_t *new_window) {
    moe_view_t *old_active = wm_state.active_window;
    wm_state.active_window = new_window;
    if (old_active) {
        draw_border(old_active);
        moe_invalidate_view(old_active, NULL);
    }
    if (new_window) {
        draw_border(wm_state.active_window);
        moe_show_window(wm_state.active_window);
    }
}


void move_window(moe_view_t *view, moe_point_t *new_point) {
    moe_rect_t old_frame = view->frame;
    if (old_frame.origin.x != new_point->x || old_frame.origin.y != new_point->y) {
        view->frame.origin = *new_point;
        moe_invalidate_screen(&old_frame);
        moe_invalidate_view(view, NULL);
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
                if (p->flags & WINDOW_TRANSPARENT) {
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
    while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
        moe_yield();
    }
    draw_window_nb(view, rect, off_screen);
    atomic_flag_clear(&wm_state.hierarchy_lock);
}

void moe_invalidate_view(moe_view_t *view, moe_rect_t *_rect) {
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
    if (view->flags & WINDOW_TRANSPARENT) {
        needs_recursive = 1;
    } else {
        while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
            moe_yield();
        }
        moe_view_t *p = view;
        for (; p; p = p->next) {
            if (moe_hit_test_rect(&rect, &p->frame)) {
                needs_recursive = 1;
                break;
            }
        }
        atomic_flag_clear(&wm_state.hierarchy_lock);
    }

    if (needs_recursive) {
        draw_window(wm_state.root, &rect, 1);
    } else {
        draw_window(view, &rect, 0);
    }
}


void moe_invalidate_screen(moe_rect_t *rect) {
    moe_invalidate_view(wm_state.root, rect);
    // if (rect) {
    //     draw_window(wm_state.root, rect, 1);
    // } else {
    //     atomic_bit_test_and_set(&wm_state.global_flags, global_flag_screen_redraw);
    // }
}

moe_size_t moe_get_screen_size() {
    return wm_state.root->frame.size;
}

int moe_alert(const char *title, const char *message, uint32_t flags) {

    moe_dib_t *dib = wm_state.popup_window->dib;
    int width = dib->width;
    int height = dib->height;
    int padding_x = 12;

    // moe_fill_rect(dib, NULL, popup_bgcolor);
    moe_fill_rect(dib, NULL, COLOR_TRANSPARENT);
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
    moe_point_t origin = {rect_button.origin.x + 32, rect_button.origin.y + 2};
    moe_draw_string(dib, &origin, &rect_button, "OK", active_button_fgcolor);

    moe_view_set_title(wm_state.popup_window, title);

    moe_show_window(wm_state.popup_barrier);
    // moe_show_window(wm_state.popup_window);
    moe_set_active_window(wm_state.popup_window);
    getchar();
    moe_hide_window(wm_state.popup_barrier);
    moe_hide_window(wm_state.popup_window);
    return 0;
}


int getchar() {
    for(;;) {
        int c = hid_getchar();
        if (c >= 0) {
            return c;
        }
        moe_yield();
    }
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

    while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
        moe_yield();
    }

    moe_view_t *found = NULL;

    moe_view_t *p = wm_state.root;
    for (; p; p = p->next) {
        if (p->window_level >= window_level_pointer) break;
        if (moe_hit_test(&p->frame, point)) found = p;
    }

    atomic_flag_clear(&wm_state.hierarchy_lock);

    return found;
}


_Noreturn void window_thread(void* args) {

    // Init mouse
    {
        moe_size_t size = { MOUSE_CURSOR_WIDTH, MOUSE_CURSOR_HEIGHT };
        moe_dib_t *dib = moe_create_dib(&size, MOE_DIB_COLOR_KEY, mouse_cursor_palette[0]);
        for (int i = 0; i < MOUSE_CURSOR_WIDTH * MOUSE_CURSOR_HEIGHT; i++) {
            dib->dib[i] = mouse_cursor_palette[mouse_cursor_source[0][i]];
        }
        mouse_cursor = moe_create_view(NULL, dib, WINDOW_TRANSPARENT | WINDOW_CENTER | window_level_pointer, "Mouse");
        mouse_point = mouse_cursor->frame.origin;

        moe_show_window(mouse_cursor);
    }

    moe_invalidate_screen(NULL);

    for (;;) {

        // if (atomic_bit_test_and_clear(&wm_state.global_flags, global_flag_screen_redraw)) {
        //     // Redraw Screen
        //     // atomic_bit_test_and_clear(&wm_state.global_flags, global_flag_view_redraw);
        //     // draw_window(wm_state.root, &wm_state.root->frame, 1);
        //     // draw_window(wm_state.root, NULL, 1);
        // } else 

        if (atomic_bit_test_and_clear(&wm_state.global_flags, global_flag_view_redraw)) {
            // Redraw View
            while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
                moe_yield();
            }
            moe_view_t *view = wm_state.root;
            for (; view; view = view->next) {
                if (atomic_bit_test_and_clear(&view->state, view_state_needs_redraw)) {
                    if (view->flags & WINDOW_TRANSPARENT) {
                        draw_window_nb(wm_state.root, &view->frame, 1);
                    } else {
                        draw_window_nb(view, NULL, 1);
                    }
                }
            }
            atomic_flag_clear(&wm_state.hierarchy_lock);
        }

        // Update mouse
        if (atomic_bit_test_and_clear(&wm_state.global_flags, global_flag_mouse_redraw)) {

            if (wm_state.captured_window) {
                if (mouse.l_button) {
                    moe_point_t new_origin;
                    new_origin.x = mouse_point.x - captured_at.x;
                    int top = (wm_state.captured_window->window_level < window_level_higher) ? wm_state.global_insets.top : 0;
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
                    if ((mouse_at->flags & WINDOW_PINCHABLE) || ((mouse_at->flags & WINDOW_CAPTION) && relative_point.y < title_height)) {
                        wm_state.captured_window = mouse_at;
                        captured_at = relative_point;
                    }
                }

                size_t size_buff = 32;
                char buff[32];
                uint32_t packed_button = (mouse.buttons & 7) + ((mouse.pressed & 7) << 4) + ((mouse.released & 7) << 8);
                snprintf(buff, size_buff, "%4d %4d %03x %d %08zx", mouse_point.x, mouse_point.y, packed_button,
                    mouse_at->index, (uintptr_t)mouse_at
                );
                moe_rect_t rect = moe_get_client_rect(event_test_window);
                moe_fill_rect(event_test_window->dib, &rect, 0x80000000);
                moe_draw_string(event_test_window->dib, NULL, &rect, buff, 0xFFFFFF77);
                moe_invalidate_screen(&event_test_window->frame);

            }

            move_window(mouse_cursor, &mouse_point);
        }
        moe_yield();
    }
}


void window_init() {
    wm_state.view_pool = mm_alloc_static(MAX_WINDOWS * sizeof(moe_view_t));
    memset(wm_state.view_pool, 0, MAX_WINDOWS * sizeof(moe_view_t));
    for (int i = 0; i < MAX_WINDOWS; i++) {
        wm_state.view_pool[i].index = i;
    }

    moe_rect_t screen_bounds = {{0, 0}, {main_screen_dib.width, main_screen_dib.height}};
    wm_state.screen_bounds = screen_bounds;
    wm_state.off_screen = moe_create_dib(&wm_state.screen_bounds.size, 0, 0);

    // Init root window
    {
        wm_state.root = moe_create_view(&wm_state.screen_bounds, NULL, 0, "Desktop");
        wm_state.root->bgcolor = desktop_color;
        wm_state.root->window_level = window_level_desktop;
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

    // Event test window
    {
        moe_rect_t frame = {{100, 100}, {256, 56}};
        moe_dib_t *dib = moe_create_dib(&frame.size, MOE_DIB_ALPHA, 0x80000000);
        event_test_window = moe_create_view(&frame, dib, WINDOW_CAPTION | WINDOW_TRANSPARENT | WINDOW_BORDER | window_level_popup, "Event Test");

        moe_show_window(event_test_window);
    }

    // Prepare Popup Placeholder
    {
        moe_rect_t frame = {{0, 0}, {480, 160}};
        moe_dib_t *dib = moe_create_dib(&frame.size, MOE_DIB_ALPHA, 0);
        wm_state.popup_window = moe_create_view(&frame, dib, WINDOW_PINCHABLE | WINDOW_CENTER | WINDOW_TRANSPARENT | window_level_popup, "POPUP");
    }

    // Popup barrier
    {
        moe_rect_t frame = wm_state.root->frame;
        wm_state.popup_barrier = moe_create_view(&frame, NULL, WINDOW_TRANSPARENT | window_level_popup_barrier, "popup barrier");
        wm_state.popup_barrier->bgcolor = 0x80000000;
    }


    moe_create_thread(&window_thread, priority_highest, NULL, "Window Manager");
}


void cmd_win() {
    printf("ID context  attr     bitmap   bgcolor frame              title\n");
    moe_view_t *view = wm_state.root;
    do {
        printf("%2d %08zx %08x %08zx %06x %4d %4d %4d %4d %s\n", 
            view->index, (uintptr_t)view, view->state | view->flags,
            (uintptr_t)(view->dib ? view->dib->dib : 0), view->bgcolor,
            view->frame.origin.x, view->frame.origin.y, view->frame.size.width, view->frame.size.height,
            view->title);
        view = view->next;
    } while(view);
}
