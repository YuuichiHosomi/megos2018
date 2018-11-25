// Minimal Window Manager
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "hid.h"

#define MAX_WINDOWS     256

#define MAX(a, b)   ((a > b) ? (a) : (b))
#define MIN(a, b)   ((a < b) ? (a) : (b))

const int32_t border_width_top = 1;
const int32_t border_width_left = 1;
const int32_t border_width_bottom = 2;
const int32_t border_width_right = 2;
uint32_t border_color = 0x000000;
uint32_t desktop_color = 0x55AAFF;
uint32_t default_bgcolor = 0xFFFFFF;

typedef struct moe_view_t {
    moe_view_t *next;
    moe_dib_t *dib;
    void *context;
    moe_rect_t frame;
    union {
        uint32_t flags;
        struct {
            moe_window_level_t window_level: 8;
        };
    };
    uint32_t bgcolor;
    int index;
} moe_view_t;

enum {
    view_flag_needs_redraw = 29,
    view_flag_show = 30,
    view_flag_used = 31,
};


struct {
    moe_view_t *view_pool;
    moe_view_t *screen;
    moe_view_t *root;
    atomic_flag hierarchy_lock;
    uint32_t global_flags;
} wm_state;


enum {
    screen_redraw_flag,
    view_redraw_flag,
    mouse_redraw_flag,
};

moe_dib_t *desktop_dib = NULL;

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

moe_view_t *moe_create_view(moe_rect_t *frame, moe_dib_t* dib, uint32_t flags) {

    moe_view_t *self = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!atomic_bit_test_and_set(&wm_state.view_pool[i].flags, view_flag_used)) {
            self = &wm_state.view_pool[i];
            break;
        }
    }
    if (!self) return NULL;

    self->next = NULL;
    self->context = NULL;
    self->flags |= flags;
    if (!self->window_level) {
        self->window_level = window_level_normal;
    }
    self->bgcolor = default_bgcolor;

    if (dib) {
        self->dib = dib;
        if (frame) {
            self->frame = *frame;
        } else {
            moe_size_t size = { dib->width, dib->height };
            self->frame.size = size;
        }
        draw_border(self);
    } else {
        self->dib = NULL;
        if (frame) self->frame = *frame;
    }
    return self;
}

moe_point_t moe_convert_view_point_to_screen(moe_view_t *view) {
    return view->frame.origin;
}


void moe_add_view(moe_view_t* self) {

    while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
        moe_yield();
    }

    moe_view_t *p = wm_state.root;
    moe_window_level_t lv = self->window_level;

    for (;;) {
        moe_view_t *next = p->next;
        if (!next || lv < next->window_level) {
            self->next = next;
            p->next = self;
            break;
        }
        p = p->next;
    }
    atomic_bit_test_and_set(&self->flags, view_flag_show);

    atomic_flag_clear(&wm_state.hierarchy_lock);

    moe_invalidate_view(self, NULL);
}

void moe_remove_view(moe_view_t *self) {
    while (atomic_flag_test_and_set(&wm_state.hierarchy_lock)) {
        moe_yield();
    }

    moe_rect_t rect = self->frame;
    rect.origin = moe_convert_view_point_to_screen(self);

    moe_view_t *p = wm_state.root;
    for (;p;) {
        moe_view_t *next = p->next;
        if (self == next) {
            p->next = self->next;
            self->next = NULL;
            break;
        }
        p = p->next;
    }
    atomic_bit_test_and_set(&self->flags, view_flag_show);

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

            if (wx > sx) {
                blt_origin.x = wx;
                blt_rect.origin.x = 0;
            } else {
                blt_origin.x = sx;
                blt_rect.origin.x = sx - wx;
            }
            if (wy > sy) {
                blt_origin.y = wy;
                blt_rect.origin.y = 0;
            } else {
                blt_origin.y = sy;
                blt_rect.origin.y = sy - wy;
            }
            blt_rect.size.width = MIN(wr, sr) - MAX(wx, sx);
            blt_rect.size.height = MIN(wb, sb) - MAX(wy, sy);
            moe_blt(wm_state.screen->dib, p->dib, &blt_origin, &blt_rect, 0);
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
    moe_draw_view(view, &rect);
}


void moe_invalidate_screen(moe_rect_t *rect) {
    if (rect) {
        moe_draw_view(wm_state.root, rect);
    } else {
        atomic_bit_test_and_set(&wm_state.global_flags, screen_redraw_flag);
    }
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
        mouse_cursor = moe_create_view(NULL, dib, window_level_highest);
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
            // moe_invalidate_view(mouse_cursor, NULL);
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

    wm_state.screen = moe_create_view(NULL, &main_screen_dib, 0);

    // Init root window
    {
        uint32_t flags = 0;
        moe_size_t size = wm_state.screen->frame.size;
        // if (size.width < size.height) {
        //     moe_size_t size2 = { size.height, size.width };
        //     size = size2;
        //     flags |= MOE_DIB_ROTATE;
        // }
        desktop_dib = moe_create_dib(&size, flags, desktop_color);
        wm_state.root = moe_create_view(NULL, desktop_dib, 0);
        wm_state.root->window_level = window_level_lowest;
        wm_state.root->bgcolor = desktop_color;
    }

    moe_create_thread(&window_thread, priority_highest, NULL, "Window Manager");
}


void cmd_win() {
    printf("ID context  attr     fb       bgcolor frame\n");
    moe_view_t *view = wm_state.root;
    do {
        printf("%2d %08zx %08x %08zx %06x %4d %4d %4d %4d\n", 
            view->index, (uintptr_t)view, view->flags,
            (uintptr_t)(view->dib ? view->dib->dib : 0), view->bgcolor,
            view->frame.origin.x, view->frame.origin.y, view->frame.size.width, view->frame.size.height);
        
        view = view->next;
    } while(view);
}
