// Minimal Window Manager
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"
#include "hid.h"

#define MAX_WINDOWS     256

#define MAX(a, b)   ((a > b) ? (a) : (b))
#define MIN(a, b)   ((a < b) ? (a) : (b))


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
    uint32_t bgColor;

} moe_view_t;

enum {
    view_flag_used = 31,
};


struct {
    moe_view_t *view_pool;
    // moe_view_t **view_hierarchy;
    moe_view_t *screen;
    moe_view_t *root;
} wm_state;


enum {
    screen_redraw_flag,
    mouse_redraw_flag,
};
static uint32_t global_flags;

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


moe_view_t *moe_create_view(moe_rect_t *frame, moe_dib_t* dib, uint32_t flags) {

    moe_view_t *self = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!atomic_bit_test_and_set(&wm_state.view_pool[i].flags, view_flag_used)) {
            self = &wm_state.view_pool[i];
            break;
        }
    }
    if (!self) return NULL;

    self->flags |= flags;
    if (!self->window_level) {
        self->window_level = window_level_normal;
    }

    if (dib) {
        self->dib = dib;
        if (frame) {
            self->frame = *frame;
        } else {
            moe_size_t size = { dib->width, dib->height };
            self->frame.size = size;
        }
    } else {
        if (frame) self->frame = *frame;
    }
    return self;
}


void moe_add_next_view(moe_view_t *self, moe_view_t* child) {

    moe_view_t *p = self;
    if (!p) p = wm_state.root;
    moe_window_level_t lv = child->window_level;

    for (;;) {
        moe_view_t *next = p->next;
        if (!next || lv < next->window_level) {
            child->next = next;
            p->next = child;
            return;
        }
        p = p->next;
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

void draw_view(moe_view_t* view, moe_rect_t *rect) {
    if (view->dib) {
        moe_point_t dp = view->frame.origin;
        if (rect) {
            dp.x += rect->origin.x;
            dp.y += rect->origin.y;
        }
        moe_blt(wm_state.screen->dib, view->dib, &dp, rect, 0);
    } else {
        moe_fill_rect(wm_state.screen->dib, rect ? rect: &view->frame, view->bgColor);
    }
}

void moe_invalidate_screen(moe_rect_t *rect) {
    if (wm_state.root) {
        if (rect) {
            moe_view_t *p = wm_state.root;
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
        } else {
            atomic_bit_test_and_set(&global_flags, screen_redraw_flag);
        }
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
    atomic_bit_test_and_set(&global_flags, mouse_redraw_flag);
}


extern moe_dib_t main_screen_dib;
_Noreturn void window_thread(void* args) {

    {
        wm_state.view_pool = mm_alloc_static(MAX_WINDOWS * sizeof(moe_view_t));
        memset(wm_state.view_pool, 0, MAX_WINDOWS * sizeof(moe_view_t));

        wm_state.screen = moe_create_view(NULL, &main_screen_dib, 0);
    }

    // Init root window
    {
        uint32_t flags = 0;
        moe_size_t size = wm_state.screen->frame.size;
        // if (size.width < size.height) {
        //     moe_size_t size2 = { size.height, size.width };
        //     size = size2;
        //     flags |= MOE_DIB_ROTATE;
        // }
        desktop_dib = moe_create_dib(&size, flags, 0);
        wm_state.root = moe_create_view(NULL, desktop_dib, 0);
        wm_state.root->window_level = window_level_lowest;

        mgs_cls();
    }

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

        moe_add_next_view(wm_state.root, mouse_cursor);
    }

    //  Main loop
    for (;;) {
        if (atomic_bit_test_and_clear(&global_flags, screen_redraw_flag)) {
            moe_invalidate_screen(&wm_state.root->frame);
            // atomic_bit_test_and_set(&global_flags, mouse_redraw_flag);
        } else if (atomic_bit_test_and_clear(&global_flags, mouse_redraw_flag)) {
            moe_rect_t oldrect = mouse_cursor->frame;
            mouse_cursor->frame.origin = mouse_point;
            moe_invalidate_screen(&oldrect);
            moe_invalidate_screen(&mouse_cursor->frame);
        }
        moe_yield();
    }
}


void window_init() {
    moe_create_thread(&window_thread, priority_highest, NULL, "Window Manager");
}


void cmd_win() {
    printf("context  attr     fb       bgcolor frame\n");
    moe_view_t *view = wm_state.root;
    do {
        printf("%08zx %08x %08zx %06x %4d %4d %4d %4d\n", (uintptr_t)view, view->flags,
            (uintptr_t)(view->dib ? view->dib->dib : 0), view->bgColor,
            view->frame.origin.x, view->frame.origin.y, view->frame.size.width, view->frame.size.height);
        
        view = view->next;
    } while(view);
}
