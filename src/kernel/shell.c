// Minimal Shell
// Copyright (c) 2002,2018 MEG-OS project, All rights reserved.
// License: BSD
// #include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "hid.h"


#define VER_SYSTEM_NAME     "Minimal Operating Environment"
#define VER_SYSTEM_SHORT_NAME   "MOE"
#define VER_SYSTEM_MAJOR    0
#define VER_SYSTEM_MINOR    5
#define VER_SYSTEM_REVISION 3


extern int putchar(char);
int strncmp(const char *s1, const char *s2, size_t n);



extern uintptr_t total_memory;
extern int n_active_cpu;
extern uint64_t fw_get_time();


typedef int (*pseudo_main)(int, char**);

_Noreturn void pseudo_app_start(void *args) {
    pseudo_main p = (pseudo_main)args;
    int retval = p(0, 0);
    moe_exit_thread(retval);
}

int pseudo_app_launch(pseudo_main main, int argc, char** argv) {
    return moe_create_thread(&pseudo_app_start, 0, (void*)main, argv[0]);
}

typedef struct {
    char *name;
    pseudo_main proc;
    int separate_thread;
    char *tips; 
} command_list_t;

int cmd_ver(int argc, char **argv);
int cmd_help(int argc, char **argv);
int cmd_cls(int argc, char **argv);
int cmd_reboot(int argc, char **argv);
int cmd_exit(int argc, char **argv);
extern int cmd_mem(int argc, char **argv);
extern int cmd_win(int argc, char **argv);
extern int cmd_ps(int argc, char **argv);
extern int cmd_cpuid(int, char**);
extern int cmd_noiz2bg(int, char**);
extern int cmd_top(int argc, char **argv);

command_list_t commands[] = {
    { "help", cmd_help, 0, "Show help" },
    { "cls", cmd_cls, 0, "Clear screen" },
    { "ver", cmd_ver, 0, "Display version info" },
    { "reboot", cmd_reboot, 0, "Restart computer" },
    { "exit", cmd_exit, 0, "Exit shell" },
    { "memory", cmd_mem, 0, "Display memory info" },
    { "windows", cmd_win, 0, "Display window list" },
    { "ps", cmd_ps, 0, "Display thread list" },
    { "cpuid", cmd_cpuid, 0, "Display cpuid info" },
    { "noiz2bg", cmd_noiz2bg, 1, "DEMO" },
    { "top", cmd_top, 1, "Graphical Task list" },
    { NULL, NULL },
};


/*********************************************************************/

// int rgb32_to_luminance(uint32_t rgb) {
//     uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
//     return ((r * 19589 + g * 38444 + b * 7502) + 32767) >> 16;
// }

// void draw_logo_bitmap(moe_dib_t* screen, const uint8_t* bmp, int offset_x, int offset_y) {
//     uintptr_t offset = *((uint32_t*)(bmp + 10));
//     int bmp_w = *((uint32_t*)(bmp + 18));
//     int bmp_h = *((uint32_t*)(bmp + 22));
//     int bmp_bpp = *((uint16_t*)(bmp + 28));
//     int bmp_bpp8 = (bmp_bpp + 7) / 8;
//     int bmp_ppl = (bmp_bpp8 * bmp_w + 3) & 0xFFFFFFFC;
//     int delta = screen->delta;
//     const uint8_t* dib = bmp+offset;

//     if (offset_x < 0) offset_x = (screen->width - bmp_w) / 2;
//     if (offset_y < 0) offset_y = (screen->height - bmp_h) / 2;

//     moe_rect_t rect = { { offset_x, offset_y}, {bmp_w, bmp_h} };
//     uint32_t* vram = screen->dib;
//     vram += offset_x + offset_y * delta;

//     switch (bmp_bpp) {
//         case 24:
//         case 32:
//             for (int i = bmp_h-1; i >= 0; i--) {
//                 const uint8_t* p = dib + (i * bmp_ppl);
//                 for (int j = 0; j < bmp_w; j++) {
//                     uint32_t rgb = (p[j * bmp_bpp8 + 0]) + (p[j * bmp_bpp8 + 1] * 0x100) + (p[j * bmp_bpp8 + 2] * 0x10000);
//                     if (rgb) {
//                         uint8_t b = 255 - rgb32_to_luminance(rgb);
//                         vram[j] = b + (b<<8) + (b<<16);
//                     }
//                 }
//                 vram += delta;
//             }
//             break;

//         case 8:
//             for (int i = bmp_h-1; i >= 0; i--) {
//                 const uint8_t* p = dib + (i * bmp_ppl);
//                 for (int j = 0; j < bmp_w; j++) {
//                     float alpha = p[j * bmp_bpp8 + 0] / 255.0;
//                     uint8_t* vram8 = (uint8_t*)(vram+j);
//                     for (int k = 0; k < 3; k++) {
//                         vram8[k] = vram8[k] * alpha;
//                     }
//                 }
//                 vram += delta;
//             }
//             break;
//     }
// }


int zgetchar(moe_window_t *window) {
    for(;;) {
        uintptr_t event = moe_get_event(window, -1);
        uint32_t c = moe_translate_key_event(window, event);
        if (c != INVALID_UNICHAR) {
            return c;
        }
    }
}


int read_cmdline(moe_window_t *window, char* buffer, size_t max_len) {
    int cont_flag = 1;
    int len = 0, limit = max_len - 1;

    int old_cursor_visible = moe_set_console_cursor_visible(NULL, 1);
    while (cont_flag) {
        uint32_t c = zgetchar(window);
        switch (c) {
            case '\x08': // bs
            case '\x7F': // del
                if (len > 0) {
                    len--;
                    printf("\b \b");
                    if (buffer[len] < 0x20) { // ^X
                        printf("\b \b");
                    }
                }
                break;

            case '\x0D': // cr
                cont_flag = 0;
                break;
            
            default:
                if (len < limit) {
                    if (c < 0x80) {
                        buffer[len++] = c;
                        if (c < 0x20) { // ^X
                            printf("^%c", c | 0x40);
                        } else {
                            putchar(c);
                        }
                    } else { // non ascii
                        printf("?+%04x", c);
                    }
                }
                break;
        }
    }
    moe_set_console_cursor_visible(NULL, old_cursor_visible);
    buffer[len] = '\0';
    printf("\n");
    return len;
}



extern void mgs_cls();
int cmd_cls(int argc, char **argv) {
    mgs_cls();
    return 0;
}

int cmd_help(int argc, char **argv) {
    for (int i = 0; commands[i].name; i++) {
        command_list_t cmd = commands[i];
        printf("%s\t%s\n", cmd.name, cmd.tips);
    }
    return 0;
}

int cmd_ver(int argc, char **argv) {
    printf("%s v%d.%d.%d\n", VER_SYSTEM_NAME, VER_SYSTEM_MAJOR, VER_SYSTEM_MINOR, VER_SYSTEM_REVISION);
    return 0;
}

int cmd_reboot(int argc, char **argv) {
    moe_reboot();
    return 0;
}

int cmd_exit(int argc, char **argv) {
    moe_shutdown_system();
    return 0;
}


//  Pseudo shell
#define MAX_CMDLINE 80
#define MAX_ARGV    256
#define MAX_ARGBUFF 256

extern void console_init(moe_console_context_t *self, moe_window_t* window, moe_dib_t *dib, const moe_edge_insets_t* insets);
_Noreturn void pseudo_shell(void* args) {

    moe_window_t *window;

    // Init root console
    {
        uint32_t console_attributes = 0xF8;
        moe_rect_t frame = {{16, 32}, {608, 436}};
        window = moe_create_window(&frame, MOE_WS_CAPTION | MOE_WS_BORDER, 0, "Terminal");
        moe_edge_insets_t insets = moe_get_client_insets(window);
        console_init(NULL, window, moe_get_window_bitmap(window), &insets);
        moe_set_console_attributes(NULL, console_attributes);
        moe_set_active_window(window);
    }

    int argc;
    char* argv[MAX_ARGV];
    char con_buff[MAX_CMDLINE];
    char arg_buff[MAX_ARGBUFF];

    cmd_ver(0, 0);
    // printf("Hello, world!\n");
    // moe_alert("Hello", "Hello, world!", 0);
    // cmd_ps(0, 0);

    for (;;) {
        printf("# ");
        read_cmdline(window, con_buff, MAX_CMDLINE);
        strncpy(arg_buff, con_buff, MAX_ARGBUFF);

        char *p = arg_buff;
        for (int cont = 1; cont; ) {
            char c = *p;
            switch(c) {
                case ' ':
                case '\t':
                    p++;
                    break;
                default:
                    cont = 0;
                    break;
            }
        }
        if (!*p) continue;

        int stop = 0;
        argc = 0;
        do {
            argv[argc++] = p;
            for (int cont = 1; cont; ) {
                char c = *p;
                switch(c) {
                    case ' ':
                    case '\t':
                        cont = 0;
                        break;
                    case '\0':
                        stop = 1;
                        cont = 0;
                        break;
                    default:
                        p++;
                        break;
                }
            }
            *p++ = '\0';
            for (int cont = 1; cont;) {
                char c = *p;
                switch(c) {
                    case ' ':
                    case '\t':
                        p++;
                        break;
                    case '\0':
                        stop = 1;
                        cont = 0;
                        break;
                    default:
                        cont = 0;
                        break;
                }
            }
        } while (!stop);

        command_list_t *cmd_to_run = NULL;
        for (int i = 0; commands[i].name; i++) {
            int x = strncmp(commands[i].name, argv[0], 8);
            if (!x){
                cmd_to_run = &commands[i];
                break;
            }
        }
        if (cmd_to_run) {
            if (cmd_to_run->separate_thread) {
                pseudo_app_launch(cmd_to_run->proc, argc, argv);
            } else {
                cmd_to_run->proc(argc, argv);
            }
        } else {
            printf("Bad command or file name\n");
        }
    }
}


//  Clock and Statusbar thread
_Noreturn void statusbar_thread(void *args) {

    uint32_t statusbar_bgcolor = 0xEEEEF7;
    uint32_t fgcolor = 0x555555;

    moe_size_t screen_size = moe_get_screen_size();
    moe_rect_t rect_statusbar = {{0, 0}, {screen_size.width, 22}};
    moe_window_t *statusbar = moe_create_window(&rect_statusbar, MOE_WS_CLIENT_RECT, window_level_higher, "Statusbar");
    moe_set_window_bgcolor(statusbar, statusbar_bgcolor);
    moe_dib_t *statusbar_dib = moe_get_window_bitmap(statusbar);

    const size_t size_buff = 256;
    char buff[size_buff];

    int width_clock = 8 * 8, height = 20, padding_x = 8, padding_y = 1;
    moe_rect_t rect_c = { {screen_size.width - width_clock - padding_x, padding_y}, {width_clock, height} };

    int width_usage = 6 * 8;
    moe_rect_t rect_u = {{rect_c.origin.x - padding_x - width_usage, padding_y}, {width_usage, height}};

    uint64_t time_base = fw_get_time() - moe_get_measure();

    {
        moe_point_t origin = {4, padding_y};
        moe_draw_string(statusbar_dib, NULL, &origin, NULL, "@ | File  Edit  View  Window  Help", fgcolor);
    }

    moe_edge_insets_t insets = {rect_statusbar.size.height, 0, 0, 0};
    moe_show_window(statusbar);
    moe_add_screen_insets(&insets);

    moe_rect_t rect_redraw = {{rect_u.origin.x, 0}, {rect_statusbar.size.width - rect_u.origin.x, rect_statusbar.size.height - 2}};

    uintptr_t event;
    while ((event = moe_get_event(statusbar, 250000))) {
        moe_fill_rect(statusbar_dib, &rect_redraw, statusbar_bgcolor);

        uint32_t now = ((time_base + moe_get_measure()) / 1000000LL);
        unsigned time0 = now % 60;
        unsigned time1 = (now / 60) % 60;
        unsigned time2 = (now / 3600) % 24;
        snprintf(buff, size_buff, "%02d:%02d:%02d", time2, time1, time0);
        moe_draw_string(statusbar_dib, NULL, NULL, &rect_c, buff, fgcolor);

        unsigned usage = moe_get_usage();
        unsigned usage0 = usage % 10;
        unsigned usage1 = usage / 10;
        snprintf(buff, size_buff, "%3d.%1d%%", usage1, usage0);
        moe_draw_string(statusbar_dib, NULL, NULL, &rect_u, buff, fgcolor);

        moe_invalidate_rect(statusbar, &rect_redraw);
    }
    moe_exit_thread(0);
}

_Noreturn void key_test_thread(void *args) {
    const size_t max_buff = 16;
    char buff[max_buff+1];
    memset(buff, 0, max_buff+1);
    uintptr_t ptr = 0;
    const uint32_t bgcolor = 0xFFFFFFFF;
    const uint32_t fgcolor = 0xFF555555;

    moe_size_t screen_size = moe_get_screen_size();
    int width = 160;
    moe_rect_t frame = {{screen_size.width - width - 8, 32}, {width, 56}};
    moe_window_t *window = moe_create_window(&frame, MOE_WS_CAPTION, 0, "Key Test");
    moe_show_window(window);

    uintptr_t event;
    while ((event = moe_get_event(window, -1))) {
        uint32_t c = moe_translate_key_event(window, event);
        if (c != INVALID_UNICHAR) {
            if (c == '\b' && ptr > 0) {
                buff[--ptr] = '\0';
            } else if (c >= 0x20 && c < 0x80 && ptr < max_buff) {
                buff[ptr++] = c;
            }
            moe_rect_t rect = moe_get_client_rect(window);
            moe_fill_rect(moe_get_window_bitmap(window), &rect, bgcolor);
            moe_draw_string(moe_get_window_bitmap(window), NULL, NULL, &rect, buff, fgcolor);
            moe_invalidate_rect(window, NULL);
        }
    }
    moe_exit_thread(0);
}


_Noreturn void button_test_thread(void *args) {

    moe_size_t screen_size = moe_get_screen_size();
    int width = 160, height = 120;
    moe_rect_t frame = {{screen_size.width - width - 8, 96}, {width, height}};
    moe_window_t *window = moe_create_window(&frame, MOE_WS_CAPTION, 0, "Button Test");

    int button_width = 120, button_height = 24;
    int cursor = 0;
    moe_dib_t *dib = moe_get_window_bitmap(window);

    {
        cursor+= button_height + 4;
        moe_point_t origin = {32, 2};
        moe_rect_t rect = {{(width - button_width)/2, cursor}, {button_width, button_height}};
        moe_fill_round_rect(dib, &rect, 4, 0xFF3366FF);
        moe_draw_round_rect(dib, &rect, 4, 0xFF2244AA);
        moe_draw_string(dib, NULL, &origin, &rect, "Default", 0xFFFFFFFF);
    }

    {
        cursor+= button_height + 4;
        moe_point_t origin = {16, 2};
        moe_rect_t rect = {{(width - button_width)/2, cursor}, {button_width, button_height}};
        moe_fill_round_rect(dib, &rect, 4, 0xFFFF3366);
        moe_draw_round_rect(dib, &rect, 4, 0xFFAA2244);
        moe_draw_string(dib, NULL, &origin, &rect, "Destructive", 0xFFFFFFFF);
    }

    {
        cursor+= button_height + 4;
        moe_point_t origin = {40, 2};
        moe_rect_t rect = {{(width - button_width)/2, cursor}, {button_width, button_height}};
        moe_draw_round_rect(dib, &rect, 4, 0xFF777777);
        moe_draw_string(dib, NULL, &origin, &rect, "Other", 0xFF555555);
    }

    moe_show_window(window);
    uintptr_t event;
    while ((event = moe_get_event(window, -1))) {
        ;
    }
    moe_exit_thread(0);
}


_Noreturn void start_init(void* args) {

    // //  Show BGRT (Boot Graphics Resource Table) from ACPI
    // acpi_bgrt_t* bgrt = acpi_find_table(ACPI_BGRT_SIGNATURE);
    // if (bgrt) {
    //     draw_logo_bitmap(?, (uint8_t*)bgrt->Image_Address, bgrt->Image_Offset_X, bgrt->Image_Offset_Y);
    // }

    moe_create_thread(&statusbar_thread, 0, 0, "statusbar");
    moe_create_thread(&pseudo_shell, 0, 0, "shell");
    moe_create_thread(&key_test_thread, 0, 0, "key test");
    moe_create_thread(&button_test_thread, 0, 0, "button test");

    for (;;) { moe_usleep(100000); }
}
