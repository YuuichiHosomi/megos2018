// Pseudo Shell Interface
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

#include <stdatomic.h>
#include "moe.h"
#include "kernel.h"
#include "hid.h"

#include "rsrc.h"

extern int putchar(int);
extern void gs_cls();


typedef int (*PSEUDO_MAIN)(int, char**);
typedef struct {
    const char *name;
    PSEUDO_MAIN proc;
    const char *tips;
} command_list_t;


const char* get_string(string id) {
    if(id >= string_max) return NULL;

    const char *retval = NULL;

    if(!retval) {
        retval = string_default[id];
    }

    return retval;
}


int cmd_cls(int argc, char **argv) {
    gs_cls();
    return 0;
}

int cmd_ver(int argc, char **argv) {
    char buffer[256];
    _zputs(moe_kname(buffer, 256));
    return 0;
}

int cmd_reboot(int argc, char **argv) {
    moe_reboot();
    return 0;
}

int cmd_shutdown(int argc, char **argv) {
    moe_shutdown_system();
    return 0;
}

int cmd_help(int argc, char **argv);
int cmd_cpuid(int argc, char **argv) __attribute__((weak));
int cmd_ps(int argc, char **argv) __attribute__((weak));
int cmd_exp(int argc, char **argv);
int cmd_stall(int argc, char **argv);
int cmd_lsusb(int argc, char **argv) __attribute__((weak));
int cmd_lspci(int argc, char **argv) __attribute__((weak));
int cmd_mode(int argc, char **argv) __attribute__((weak));

command_list_t commands[] = {
    { "help", cmd_help, "Display this help" },
    { "cls", cmd_cls, "Clear screen" },
    { "ver", cmd_ver, "Show version information" },
    { "reboot", cmd_reboot, "Restart Computer" },
    { "exit", cmd_shutdown, "Exit" },
    { "cpuid", cmd_cpuid, "Show CPUID Informations" },
    { "lspci", cmd_lspci, "Show PCI Informations" },
    { "lsusb", cmd_lsusb, "Show USB Informations" },
    { "ps", cmd_ps, NULL },
    { "exp", cmd_exp, NULL },
    { "stall", cmd_stall, NULL},
    { "mode", cmd_mode, NULL},
    { 0 },
};


int cmd_help(int argc, char **argv) {
    for (int i = 0; commands[i].name; i++) {
        if (commands[i].tips && commands[i].proc) {
            printf("%s\t%s\n", commands[i].name, commands[i].tips);
        }
    }
    return 0;
}


// user mode experiments
_Atomic intptr_t uid_src = 1;
extern void exp_user_mode(void *base, void *stack_top);
void proc_hello(void *args) {
    uintptr_t base = atomic_fetch_add(&uid_src, 1) << 39;
    size_t code_size = 0x10000;
    size_t padding = 0x400000;
    size_t stack_size = 0x10000;
    void *code = pg_map_user(base, code_size, 0);
    uint8_t *data = pg_map_user(base + padding, stack_size, 0);
    moe_usleep(100000);

    exp_user_mode(code, data + stack_size);
}

int cmd_exp(int argc, char **argv) {
    moe_create_process(&proc_hello, 0, NULL, "hello");
    return 0;
}

int cmd_stall(int argc, char **argv) {
    int time = argv[1][0] & 15;
    if (!time) time = 3;
    moe_usleep(time * 1000000);
    return 0;
}


/*********************************************************************/


static moe_queue_t *cin;


int moe_send_key_event(moe_hid_kbd_state_t *state) {
    if (!cin) return 0;
    hid_raw_kbd_report_t report = state->current;
    uint32_t uni = hid_usage_to_unicode(report.keydata[0], report.modifier);
    if (uni != INVALID_UNICHAR) {
        return moe_queue_write(cin, uni);
    } else {
        return 0;
    }
}

uint32_t zgetchar(int64_t wait) {
    intptr_t result = 0;
    if (wait) {
        while (!moe_queue_wait(cin, &result, UINT32_MAX));
    } else {
        result = moe_queue_read(cin, 0);
    }
    return result;
}


int read_cmdline(char* buffer, size_t max_len) {
    int cont_flag = 1;
    int len = 0, limit = max_len - 1;

    int old_cursor_visible = moe_set_console_cursor_visible(NULL, 1);
    while (cont_flag) {
        uint32_t c = zgetchar(MOE_FOREVER);
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
            case '\x0A': // lf
                cont_flag = 0;
                break;
            
            default:
                if (len < limit) {
                    if (c < 0x80) {
                        buffer[len++] = c;
                        if (c < 0x20) { // ^X
                            printf("^%c", c | 0x40);
                        } else {
                            printf("%c", c);
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


/*********************************************************************/
//  Pseudo shell

#define MAX_CMDLINE 80
#define MAX_ARGV    256
#define MAX_ARGBUFF 256

void shell_start(const wchar_t *cmdline) {

    cin = moe_queue_create(256);

    cmd_ver(0, NULL);

    // moe_create_thread(&fiber_test_thread, 0, NULL, "fiber_test");
    moe_usleep(1000000);

    // const char *autoexec = "stall 3\nlsusb\n";
    // for (int i = 0; autoexec[i]; i++) {
    //     moe_queue_write(cin, autoexec[i]);
    // }

    int argc;
    char* argv[MAX_ARGV];
    char con_buff[MAX_CMDLINE];
    char arg_buff[MAX_ARGBUFF];

    printf("\n%s\n", get_string(string_banner));
    for (;;) {
        printf(">");
        read_cmdline(con_buff, MAX_CMDLINE);
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
        if (cmd_to_run && cmd_to_run->proc) {
            cmd_to_run->proc(argc, argv);
        } else {
            printf("%s\n", get_string(string_bad_command));
        }
   }
}
