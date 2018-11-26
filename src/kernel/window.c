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

char *strncpy(char *s1, const char *s2, size_t n);
int getchar();

const int32_t border_width_top = 1;
const int32_t border_width_left = 1;
const int32_t border_width_bottom = 2;
const int32_t border_width_right = 2;
uint32_t border_color           = 0x000000;
uint32_t desktop_color          = 0x55AAFF;
uint32_t default_bgcolor        = 0xFFFFFF;
uint32_t active_title_bgcolor   = 0x10CCCCCC;
uint32_t active_title_shadow_color  = 0x20AAAAAA;
uint32_t active_title_fgcolor   = 0x00000000;
uint32_t popup_bgcolor          = 0x20FFFFFF;
uint32_t popup_message_color    = 0x555555;
uint32_t active_button_bgcolor  = 0x003366FF;
uint32_t active_button_fgcolor  = 0x00FFFFFF;

int title_height = 22;

typedef struct moe_view_t {
    moe_view_t *next;
    moe_dib_t *dib;
    void *context;
    moe_rect_t frame;
    union {
        _Atomic uint32_t flags;
        struct {
            uint8_t window_level;
        };
    };
    uint32_t bgcolor;
    int index;
    char title[WINDOW_TITLE_SIZE];
} moe_view_t;

#define WINDOW_FLAG_SAFE_MASK 0x00FFFFFF

enum {
    view_flag_active_window = 28,
    view_flag_needs_redraw = 29,
    view_flag_visible = 30,
    view_flag_used = 31,
};


struct {
    moe_view_t *view_pool;
    moe_view_t *screen;
    moe_view_t *root;
    moe_view_t *popup_window;
    uint32_t global_flags;
    atomic_flag hierarchy_lock;
} wm_state;


enum {
    screen_redraw_flag,
    view_redraw_flag,
    mouse_redraw_flag,
};

static moe_point_t mouse_point;
moe_view_t *mouse_cursor;

#define MOUSE_CURSOR_WIDTH  12
#define MOUSE_CURSOR_HEIGHT  20
uint32_t mouse_cursor_palette[] = { 0xFF00FF, 0x000000, 0xFFFFFF };
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


void draw_border(moe_view_t *view) {
    if (!view->dib) return;
    if ((view->flags & WINDOW_CAPTION)) {
        moe_rect_t rect = {{0, 0}, {view->frame.size.width, title_height}};
        moe_point_t point = {20, 2};
        moe_point_t point2 = {21, 3};
        moe_fill_rect(view->dib, &rect, active_title_bgcolor);
        moe_draw_string(view->dib, &point2, &rect, view->title, active_title_shadow_color);
        moe_draw_string(view->dib, &point, &rect, view->title, active_title_fgcolor);
    }
    if ((view->flags & BORDER_TOP)) {
        moe_rect_t rect = {{0, 0}, {view->frame.size.width, border_width_top}};
        moe_fill_rect(view->dib, &rect, border_color);
    }
    if ((view->flags & BORDER_LEFT)) {
        moe_rect_t rect = {{0, 0}, {border_width_left, view->frame.size.height}};
        moe_fill_rect(view->dib, &rect, border_color);
    }
    if ((view->flags & BORDER_BOTTOM)) {
        moe_rect_t rect = {{0, view->frame.size.height - border_width_bottom}, {view->frame.size.width, border_width_bottom}};
        moe_fill_rect(view->dib, &rect, border_color);
    }
    if ((view->flags & BORDER_RIGHT)) {
        moe_rect_t rect = {{view->frame.size.width - border_width_right, 0}, {border_width_right, view->frame.size.height}};
        moe_fill_rect(view->dib, &rect, border_color);
    }
}

void moe_view_set_title(moe_view_t *view, const char *title) {
    if (title) {
        strncpy(&view->title[0], title, WINDOW_TITLE_SIZE - 1);
    } else {
        memset(&view->title[0], 0, WINDOW_TITLE_SIZE);
    }
    draw_border(view);
}

moe_edge_insets_t moe_get_client_insets(moe_view_t *view) {
    const int thin_insets = 2;
    const int thick_insets = 4;
    uint32_t flags = view->flags;
    moe_edge_insets_t insets;
    if (flags & WINDOW_CAPTION) {
        insets.top = title_height + thin_insets;
    } else {
        insets.top = (flags & BORDER_TOP) ? thick_insets : thin_insets;
    }
    insets.left = (flags & BORDER_LEFT) ? thick_insets : thin_insets;
    insets.right = (flags & BORDER_RIGHT) ? thick_insets : thin_insets;
    insets.bottom = (flags & BORDER_BOTTOM) ? thick_insets : thin_insets;
    return insets;
}

moe_rect_t moe_get_client_rect(moe_view_t *view) {
    moe_rect_t rect = {{0, 0}};
    rect.size = view->frame.size;
    moe_edge_insets_t insets = moe_get_client_insets(view);
    return moe_edge_insets_inset_rect(&rect, &insets);
}

moe_view_t *moe_create_view(moe_rect_t *frame, moe_dib_t* dib, uint32_t flags, const char *title) {

    moe_view_t *self = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!atomic_bit_test_and_set(&wm_state.view_pool[i].flags, view_flag_used)) {
            self = &wm_state.view_pool[i];
            break;
        }
    }
    if (!self) return NULL;

    moe_view_t view;
    memset(&view, 0, sizeof(moe_view_t));

    view.flags |= (1 << view_flag_used) | (flags & WINDOW_FLAG_SAFE_MASK);
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
        view.frame.origin.x = (wm_state.screen->frame.size.width - view.frame.size.width) / 2;
        view.frame.origin.y = (wm_state.screen->frame.size.height - view.frame.size.height) / 2;
    }
    view.index = self->index;

    *self = view;
    return self;
}

moe_point_t moe_convert_view_point_to_screen(moe_view_t *view) {
    return view->frame.origin;
}


void moe_add_view(moe_view_t* view) {
    while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
        moe_yield();
    }

    moe_view_t *p = wm_state.root;
    moe_window_level_t lv = view->window_level;

    for (;;) {
        moe_view_t *next = p->next;
        if (!next || lv < next->window_level) {
            view->next = next;
            p->next = view;
            break;
        }
        p = p->next;
    }
    atomic_bit_test_and_set(&view->flags, view_flag_visible);

    atomic_flag_clear(&wm_state.hierarchy_lock);

    moe_invalidate_view(view, NULL);
}

void moe_remove_view(moe_view_t *view) {
    while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
        moe_yield();
    }

    moe_rect_t rect = view->frame;
    rect.origin = moe_convert_view_point_to_screen(view);

    moe_view_t *p = wm_state.root;
    for (;p;) {
        moe_view_t *next = p->next;
        if (view == next) {
            p->next = view->next;
            view->next = NULL;
            break;
        }
        p = p->next;
    }
    atomic_bit_test_and_set(&view->flags, view_flag_visible);

    atomic_flag_clear(&wm_state.hierarchy_lock);

    moe_invalidate_screen(&rect);
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


void draw_view_nb(moe_view_t *view, moe_rect_t *rect) {
    moe_view_t *p = view;
    if (!rect) {
        rect = &view->frame;
    }
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
                moe_blt(wm_state.screen->dib, p->dib, &blt_origin, &blt_rect, 0);
            } else {
                blt_rect.origin = blt_origin;
                moe_fill_rect(wm_state.screen->dib, &blt_rect, p->bgcolor);
            }
        }
        p = p->next;
    } while (p);
}

void moe_draw_view(moe_view_t *view, moe_rect_t *rect) {
    while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
        moe_yield();
    }
    draw_view_nb(view, rect);
    atomic_flag_clear(&wm_state.hierarchy_lock);
}

void moe_invalidate_view(moe_view_t *view, moe_rect_t *_rect) {
    moe_rect_t rect;
    if (_rect) {
        rect = *_rect;
    } else {
        atomic_bit_test_and_set(&view->flags, view_flag_needs_redraw);
        atomic_bit_test_and_set(&wm_state.global_flags, view_redraw_flag);
        return;
        // rect.origin = *moe_point_zero;
        // rect.size = view->frame.size;
    }
    moe_point_t origin = moe_convert_view_point_to_screen(view);
    rect.origin.x += origin.x;
    rect.origin.y += origin.y;
    if (view->flags & WINDOW_TRANSPARENT) {
        moe_invalidate_screen(&rect);
    } else {
        moe_draw_view(view, &rect);
    }
}


void moe_invalidate_screen(moe_rect_t *rect) {
    if (rect) {
        moe_draw_view(wm_state.root, rect);
    } else {
        atomic_bit_test_and_set(&wm_state.global_flags, screen_redraw_flag);
    }
}

moe_size_t moe_get_screen_size() {
    return wm_state.root->frame.size;
}


int moe_alert(const char *title, const char *message, uint32_t flags) {
    moe_fill_rect(wm_state.popup_window->dib, NULL, popup_bgcolor);
    moe_view_set_title(wm_state.popup_window, title);

    moe_rect_t rect = moe_get_client_rect(wm_state.popup_window);
    rect.size.height -= 32;
    moe_draw_string(wm_state.popup_window->dib, NULL, &rect, message, popup_message_color);

    moe_rect_t rect_button = {{0, 0}, {80, 24}};
    rect_button.origin.x = (wm_state.popup_window->frame.size.width - rect_button.size.width) / 2;
    rect_button.origin.y = rect.origin.y + rect.size.height + 6;
    // moe_fill_rect
    moe_round_rect
    (wm_state.popup_window->dib, &rect_button, 10, active_button_bgcolor);
    moe_point_t origin = {rect_button.origin.x + 32, rect_button.origin.y + 3};
    moe_draw_string(wm_state.popup_window->dib, &origin, &rect_button, "OK", active_button_fgcolor);

    moe_add_view(wm_state.popup_window);
    getchar();
    moe_remove_view(wm_state.popup_window);
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


void move_mouse(int x, int y) {
    int new_mouse_x = mouse_point.x + x;
    int new_mouse_y = mouse_point.y + y;
    if (new_mouse_x < 0) new_mouse_x = 0;
    if (new_mouse_y < 0) new_mouse_y = 0;
    if (new_mouse_x >= wm_state.screen->frame.size.width) new_mouse_x = wm_state.screen->frame.size.width - 1;
    if (new_mouse_y >= wm_state.screen->frame.size.height) new_mouse_y = wm_state.screen->frame.size.height - 1;
    mouse_point.x = new_mouse_x;
    mouse_point.y = new_mouse_y;
    atomic_bit_test_and_set(&wm_state.global_flags, mouse_redraw_flag);
}


extern moe_dib_t main_screen_dib;
_Noreturn void window_thread(void* args) {

    // Init mouse
    {
        moe_size_t size = { MOUSE_CURSOR_WIDTH, MOUSE_CURSOR_HEIGHT };
        moe_dib_t *dib = moe_create_dib(&size, MOE_DIB_COLOR_KEY, mouse_cursor_palette[0]);
        for (int i = 0; i < MOUSE_CURSOR_WIDTH * MOUSE_CURSOR_HEIGHT; i++) {
            dib->dib[i] = mouse_cursor_palette[mouse_cursor_source[0][i]];
        }
        mouse_cursor = moe_create_view(NULL, dib, WINDOW_TRANSPARENT | window_level_pointer, "Mouse");
        mouse_cursor->frame.origin.x = wm_state.screen->frame.size.width / 2;
        mouse_cursor->frame.origin.y = wm_state.screen->frame.size.height / 2;
        mouse_point = mouse_cursor->frame.origin;

        moe_add_view(mouse_cursor);
    }

    moe_invalidate_screen(NULL);

    //  Main loop
    for (;;) {
        if (atomic_bit_test_and_clear(&wm_state.global_flags, screen_redraw_flag)) {
            // Redraw Screen
            atomic_bit_test_and_clear(&wm_state.global_flags, view_redraw_flag);
            moe_draw_view(wm_state.root, &wm_state.root->frame);
        } else if (atomic_bit_test_and_clear(&wm_state.global_flags, view_redraw_flag)) {
            // Redraw View
            while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
                moe_yield();
            }
            moe_view_t *view = wm_state.root;
            do {
                if (atomic_bit_test_and_clear(&view->flags, view_flag_needs_redraw)) {
                    draw_view_nb(view, NULL);
                }
                view = view->next;
            } while (view);
            atomic_flag_clear(&wm_state.hierarchy_lock);
        }
        // Update mouse state
        if (atomic_bit_test_and_clear(&wm_state.global_flags, mouse_redraw_flag)) {
            moe_rect_t oldrect = mouse_cursor->frame;
            mouse_cursor->frame.origin = mouse_point;
            moe_invalidate_screen(&oldrect);
            moe_invalidate_screen(&mouse_cursor->frame);
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

    wm_state.screen = moe_create_view(NULL, &main_screen_dib, 0, "Screen");

    // Init root window
    {
        moe_rect_t frame = wm_state.screen->frame;
        wm_state.root = moe_create_view(&frame, NULL, 0, "Desktop");
        wm_state.root->bgcolor = desktop_color;
        wm_state.root->window_level = window_level_desktop;
    }

    // Prepare Popup window
    {
        moe_rect_t frame = {{0, 0}, {320, 160}};
        moe_dib_t *dib = moe_create_dib(&frame.size, MOE_DIB_ALPHA, 0);
        wm_state.popup_window = moe_create_view(&frame, dib, WINDOW_CENTER | WINDOW_TRANSPARENT | WINDOW_CAPTION | BORDER_ALL | window_level_popup, "POPUP");
    }

    moe_create_thread(&window_thread, priority_highest, NULL, "Window Manager");
}


void cmd_win() {
    printf("ID context  attr     bitmap   bgcolor frame              title\n");
    moe_view_t *view = wm_state.root;
    do {
        printf("%2d %08zx %08x %08zx %06x %4d %4d %4d %4d %s\n", 
            view->index, (uintptr_t)view, view->flags,
            (uintptr_t)(view->dib ? view->dib->dib : 0), view->bgcolor,
            view->frame.origin.x, view->frame.origin.y, view->frame.size.width, view->frame.size.height,
            view->title);
        view = view->next;
    } while(view);
}
