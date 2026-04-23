/*
 * main.c — BusyBox-style entry point.
 *
 * Dispatch order:
 *   1. invoked as a symlink whose name matches an applet -> run that applet
 *   2. invoked as `socketTool <applet> [args...]`         -> run that applet
 *   3. otherwise print the applet list
 */
#include <stdio.h>
#include <string.h>

#include "core/applet.h"
#include "ui/ui.h"
#include "net/net.h"

int main(int argc, char **argv)
{
    ui_init();

    const char *self = base_name(argv[0]);

    /* 1. symlink dispatch */
    const struct applet *a = find_applet(self);
    if (a) return a->main(argc, argv);

    /* 2. main binary: need a sub-command */
    if (argc < 2 ||
        strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "help") == 0) {
        print_applet_list();
        return argc < 2 ? 1 : 0;
    }

    a = find_applet(argv[1]);
    if (!a) {
        ui_err("unknown applet: %s", argv[1]);
        print_applet_list();
        return 1;
    }
    return a->main(argc - 1, argv + 1);
}
