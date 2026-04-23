/*
 * stubs.c — placeholder applet entry points.
 * Each module will be filled in by its own source file.
 */
#include "applet.h"
#include "ui.h"

#define STUB(name) \
    int name(int argc, char **argv) { \
        (void)argc; (void)argv; \
        ui_warn(#name " is not yet implemented"); \
        return 2; \
    }

STUB(udp_client_main)
STUB(udp_server_main)
STUB(ws_client_main)
STUB(ws_server_main)
STUB(bping_main)
STUB(btest_main)
