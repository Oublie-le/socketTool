/*
 * main.c — BusyBox-style entry point.
 *
 * Dispatch order:
 *   1. invoked as a symlink whose name matches an applet -> run that applet
 *   2. invoked as `socketTool <applet> [args...]`         -> run that applet
 *   3. otherwise print the applet list
 *
 * Global flags consumed before dispatch:
 *   --lang en|zh    runtime language override
 *   --no-color      disable colors
 *   -V, --version   print version and exit
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/applet.h"
#include "ui/ui.h"
#include "net/net.h"
#include "i18n/i18n.h"

extern int ui_color_enabled;

static int handle_global_flag(const char *arg)
{
    if (strcmp(arg, "--no-color") == 0) { ui_color_enabled = 0; return 1; }
    if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
        printf("%s\n", T(T_VERSION_STR));
        exit(0);
    }
    return 0;
}

/* shift one or two args off argv[idx] in place */
static void argv_shift(int *argc, char **argv, int idx, int n)
{
    for (int i = idx; i + n < *argc; i++) argv[i] = argv[i + n];
    *argc -= n;
}

static void preprocess_globals(int *argc, char **argv)
{
    for (int i = 1; i < *argc; ) {
        if (strcmp(argv[i], "--lang") == 0 && i + 1 < *argc) {
            i18n_set(argv[i+1][0] == 'z' ? ST_ZH : ST_EN);
            argv_shift(argc, argv, i, 2); continue;
        }
        if (handle_global_flag(argv[i])) {
            argv_shift(argc, argv, i, 1); continue;
        }
        i++;
    }
}

int main(int argc, char **argv)
{
    i18n_init();
    ui_init();

    const char *self = base_name(argv[0]);

    /* 1. symlink dispatch — global flags still parsed for the applet */
    const struct applet *a = find_applet(self);
    if (a) {
        preprocess_globals(&argc, argv);
        return a->main(argc, argv);
    }

    preprocess_globals(&argc, argv);

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
        ui_err(T(T_UNKNOWN_APPLET), argv[1]);
        print_applet_list();
        return 1;
    }
    return a->main(argc - 1, argv + 1);
}
