// Minimal Window Manager
// Copyright (c) 2018 MEG-OS project, All rights reserved.
// License: BSD
#include <stdatomic.h>
#include "moe.h"


typedef struct moe_view_t {
    void *context;

    moe_view_t *parent;
    moe_view_t *firstchild, *sibling;

    moe_dib_t *dib;
    moe_rect_t frame;
    uint32_t bgColor;

} moe_view_t;


enum {
    screen_redraw_flag,
    mouse_redraw_flag,
};
static uint32_t global_flags;

static moe_point_t mouse_point;
moe_view_t *main_screen;
moe_view_t *desktop;
moe_view_t *mouse_cursor;
moe_dib_t *desktop_dib = NULL;

const moe_rect_t rect_zero = {{0, 0}, {0, 0}};
const moe_point_t *moe_point_zero = &rect_zero.origin;
const moe_size_t *moe_size_zero = &rect_zero.size;
const moe_rect_t *moe_rect_zero = &rect_zero;

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


moe_view_t *moe_create_view(moe_rect_t *frame, moe_dib_t* dib) {
    moe_view_t *self = mm_alloc_static(sizeof(moe_view_t));
    memset(self, 0, sizeof(moe_view_t));
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


void draw_view(moe_view_t* view, moe_rect_t *rect) {
    if (view->dib) {
        moe_point_t dp = view->frame.origin;
        if (rect) {
            dp.x += rect->origin.x;
            dp.y += rect->origin.y;
        }
        moe_blt(main_screen->dib, view->dib, &dp, rect, 0);
    } else {
        moe_fill_rect(main_screen->dib, rect ? rect: &view->frame, view->bgColor);
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

void moe_invalidate_screen(moe_rect_t *rect) {
    if (desktop) {
        if (rect) {
            draw_view(desktop, rect);
            if (moe_hit_test_rect(rect, &mouse_cursor->frame)) {
                atomic_bit_test_and_set(&global_flags, mouse_redraw_flag);
            }
        } else {
            atomic_bit_test_and_set(&global_flags, mouse_redraw_flag);
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
    if (new_mouse_x >= main_screen->frame.size.width) new_mouse_x = main_screen->frame.size.width - 1;
    if (new_mouse_y >= main_screen->frame.size.height) new_mouse_y = main_screen->frame.size.height - 1;
    mouse_point.x = new_mouse_x;
    mouse_point.y = new_mouse_y;
    atomic_bit_test_and_set(&global_flags, mouse_redraw_flag);
}


_Noreturn void window_thread(void* args) {

    // Init desktop
    {
        uint32_t flags = 0;
        moe_size_t size = main_screen->frame.size;
        // if (size.width < size.height) {
        //     moe_size_t size2 = { size.height, size.width };
        //     size = size2;
        //     flags |= MOE_DIB_ROTATE;
        // }
        desktop_dib = moe_create_dib(&size, flags, 0);
        desktop = moe_create_view(NULL, desktop_dib);
        mgs_cls();
    }

    // Init mouse
    {
        moe_size_t size = { MOUSE_CURSOR_WIDTH, MOUSE_CURSOR_HEIGHT };
        moe_dib_t *dib = moe_create_dib(&size, MOE_DIB_COLOR_KEY, mouse_cursor_palette[0]);
        for (int i = 0; i < MOUSE_CURSOR_WIDTH * MOUSE_CURSOR_HEIGHT; i++) {
            dib->dib[i] = mouse_cursor_palette[mouse_cursor_source[0][i]];
        }
        mouse_cursor = moe_create_view(NULL, dib);
        mouse_cursor->frame.origin.x = main_screen->frame.size.width / 2;
        mouse_cursor->frame.origin.y = main_screen->frame.size.height / 2;
        mouse_point = mouse_cursor->frame.origin;
    }

    //  Main loop
    for (;;) {
        if (atomic_bit_test_and_clear(&global_flags, screen_redraw_flag)) {
            draw_view(desktop, NULL);
            atomic_bit_test_and_set(&global_flags, mouse_redraw_flag);
        }
        if (atomic_bit_test_and_clear(&global_flags, mouse_redraw_flag)) {
            draw_view(desktop, &mouse_cursor->frame);
            mouse_cursor->frame.origin = mouse_point;
            draw_view(mouse_cursor, NULL);
        }
        moe_yield();
    }
}


extern moe_dib_t main_screen_dib;
void mwm_init() {
    main_screen = moe_create_view(NULL, &main_screen_dib);
    moe_create_thread(&window_thread, NULL, "Window Manager");
}
