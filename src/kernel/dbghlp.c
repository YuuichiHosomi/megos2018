// Debugger Support
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: BSD
#include "moe.h"
#include "kernel.h"

int strncmp(const char *s1, const char *s2, size_t n);
extern int cmd_mem(int argc, char **argv);
extern int cmd_win(int argc, char **argv);

typedef int (*pseudo_main)(int, char**);

typedef struct {
    char *name;
    pseudo_main proc;
} command_list_t;

static command_list_t commands[] = {
    { "mem", cmd_mem, },
    { "win", cmd_win, },
    { NULL, NULL },
};

int cmd_dbg(int argc, char **argv) {
    command_list_t *cmd_to_run = NULL;
    if (argc > 1) {
        for (int i = 0; commands[i].name; i++) {
            int x = strncmp(commands[i].name, argv[1], 8);
            if (!x){
                cmd_to_run = &commands[i];
                break;
            }
        }
    }
    if (cmd_to_run) {
        return cmd_to_run->proc(argc - 1, argv + 1);
    } else {
        printf("dbg command\n");
        return 1;
    }
}
