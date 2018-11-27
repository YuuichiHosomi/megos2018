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
const int32_t border_width_bottom = 1;
const int32_t border_width_right = 1;

uint32_t border_color               = 0x00777777;
uint32_t desktop_color              = 0x0055AAFF;
uint32_t default_bgcolor            = 0x00FFFFFF;
uint32_t active_title_bgcolor       = 0x10CCCCCC;
uint32_t active_title_shadow_color  = 0x20AAAAAA;
uint32_t active_title_fgcolor       = 0x00000000;
uint32_t popup_title_color          = 0x00000000;
uint32_t popup_bgcolor              = 0x18FFFF77;
uint32_t popup_message_color        = 0x00555555;
uint32_t active_button_bgcolor      = 0x003366FF;
uint32_t active_button_fgcolor      = 0x00FFFFFF;
uint32_t destructive_button_bgcolor = 0x00FF3366;
uint32_t destructive_button_fgcolor = 0x00FFFFFF;

int button_height = 24;
int button_radius = 4;
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


extern moe_dib_t main_screen_dib;

struct {
    moe_view_t *view_pool;
    moe_view_t *root;
    moe_view_t *popup_window;
    moe_dib_t *close_button_dib;
    moe_view_t *off_screen;
    moe_rect_t screen_bounds;
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
uint32_t mouse_cursor_palette[] = { 0xFFFF00FF, 0x00000000, 0x00FFFFFF };
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
uint32_t close_button_palette[] = { 0xFF000000, 0xE0000000, 0xC0000000, 0xA0000000, 0x80000000, 0x60000000 };
uint8_t close_button_source[CLOSE_BUTTON_SIZE][CLOSE_BUTTON_SIZE] = {
    { 0, 1, 0, 0, 0, 0, 0, 1, },
    { 1, 4, 2, 0, 0, 0, 2, 4, 1, },
    { 0, 2, 4, 2, 0, 2, 4, 2, },
    { 0, 0, 2, 4, 3, 4, 2, },
    { 0, 0, 0, 3, 5, 3, },
    { 0, 0, 2, 4, 3, 4, 2, },
    { 0, 2, 4, 2, 0, 2, 4, 2, },
    { 1, 4, 2, 0, 0, 0, 2, 4, 1, },
    { 0, 1, 0, 0, 0, 0, 0, 1, },
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

        // int control_radius = 6;
        // moe_rect_t rect_close = {{0, 0}, {control_radius * 2, control_radius * 2}};
        // rect_close.origin.x = view->frame.size.width - rect_close.size.width - 12;
        // rect_close.origin.y = (title_height - rect_close.size.height) / 2;
        // moe_fill_round_rect(view->dib, &rect_close, control_radius, destructive_button_bgcolor);
        // moe_draw_round_rect(view->dib, &rect_close, control_radius, border_color);

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
        view.frame.origin.x = (wm_state.screen_bounds.size.width - view.frame.size.width) / 2;
        view.frame.origin.y = (wm_state.screen_bounds.size.height - view.frame.size.height) / 2;
    }
    view.index = self->index;

    *self = view;
    return self;
}

moe_point_t moe_convert_view_point_to_screen(moe_view_t *view) {
    return view->frame.origin;
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
    atomic_bit_test_and_clear(&view->flags, view_flag_visible);
}

void moe_show_window(moe_view_t* view) {
    while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
        moe_yield();
    }

    remove_view_hierarchy_nb(view);

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
    atomic_bit_test_and_set(&view->flags, view_flag_visible);

    atomic_flag_clear(&wm_state.hierarchy_lock);

    moe_invalidate_view(view, NULL);
}

void moe_hide_window(moe_view_t *view) {
    while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
        moe_yield();
    }

    moe_rect_t rect = view->frame;
    rect.origin = moe_convert_view_point_to_screen(view);

    remove_view_hierarchy_nb(view);

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
                moe_blt(&main_screen_dib, p->dib, &blt_origin, &blt_rect, 0);
            } else {
                blt_rect.origin = blt_origin;
                moe_fill_rect(&main_screen_dib, &blt_rect, p->bgcolor);
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

    // moe_view_set_title(wm_state.popup_window, title);

    moe_show_window(wm_state.popup_window);
    getchar();
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


void move_mouse(int x, int y) {
    int new_mouse_x = mouse_point.x + x;
    int new_mouse_y = mouse_point.y + y;
    if (new_mouse_x < 0) new_mouse_x = 0;
    if (new_mouse_y < 0) new_mouse_y = 0;
    if (new_mouse_x >= wm_state.screen_bounds.size.width) new_mouse_x = wm_state.screen_bounds.size.width - 1;
    if (new_mouse_y >= wm_state.screen_bounds.size.height) new_mouse_y = wm_state.screen_bounds.size.height - 1;
    mouse_point.x = new_mouse_x;
    mouse_point.y = new_mouse_y;
    atomic_bit_test_and_set(&wm_state.global_flags, mouse_redraw_flag);
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

    moe_rect_t screen_bounds = {{0, 0}, {main_screen_dib.width, main_screen_dib.height}};
    wm_state.screen_bounds = screen_bounds;

    // Prepare CLOSE BUTTON
    {
        moe_size_t size = {CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE };
        moe_dib_t *dib = moe_create_dib(&size, MOE_DIB_ALPHA, close_button_palette[0]);
        for (int i = 0; i < CLOSE_BUTTON_SIZE * CLOSE_BUTTON_SIZE; i++) {
            dib->dib[i] = close_button_palette[close_button_source[0][i]];
        }
        wm_state.close_button_dib = dib;
    }

    // Off screen buffer
    if (0) {
        moe_dib_t *dib = moe_create_dib(&wm_state.screen_bounds.size, 0, 0);
        wm_state.off_screen = moe_create_view(&wm_state.screen_bounds, dib, 0, "Off Screen");
    }

    // Init root window
    {
        wm_state.root = moe_create_view(&wm_state.screen_bounds, NULL, 0, "Desktop");
        wm_state.root->bgcolor = desktop_color;
        wm_state.root->window_level = window_level_desktop;
    }

    // Prepare Popup Placeholder
    {
        moe_rect_t frame = {{0, 0}, {480, 160}};
        moe_dib_t *dib = moe_create_dib(&frame.size, MOE_DIB_ALPHA, 0);
        wm_state.popup_window = moe_create_view(&frame, dib, WINDOW_CENTER | WINDOW_TRANSPARENT | window_level_popup, "POPUP");
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
