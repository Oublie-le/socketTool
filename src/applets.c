/*
 * applets.c — registry of available applets.
 */
#include <string.h>
#include <stdio.h>

#include "applet.h"
#include "ui.h"

const struct applet applets[] = {
    { "tcp-client", tcp_client_main, "TCP client: connect & exchange data" },
    { "tcp-server", tcp_server_main, "TCP server: listen & echo"           },
    { "udp-client", udp_client_main, "UDP client: send datagrams"          },
    { "udp-server", udp_server_main, "UDP server: receive datagrams"       },
    { "ws-client",  ws_client_main,  "WebSocket client"                    },
    { "ws-server",  ws_server_main,  "WebSocket server"                    },
    { "bping",      bping_main,      "Batch ping a list of hosts"          },
    { "btest",      btest_main,      "Batch protocol connectivity test"    },
    { NULL, NULL, NULL },
};

const struct applet *find_applet(const char *name)
{
    if (!name) return NULL;
    for (const struct applet *a = applets; a->name; a++) {
        if (strcmp(a->name, name) == 0)
            return a;
    }
    return NULL;
}

void print_applet_list(void)
{
    ui_title("socketTool — multi-protocol network test toolkit");
    printf("\n  Usage: %ssocketTool%s <applet> [options]\n", UI_BOLD, UI_RESET);
    printf("     or: <applet> [options]   (when invoked via symlink)\n\n");
    printf("  %sAvailable applets:%s\n", UI_BOLD, UI_RESET);
    for (const struct applet *a = applets; a->name; a++) {
        printf("    %s%-12s%s  %s\n", UI_CYAN, a->name, UI_RESET, a->summary);
    }
    printf("\n  Run '%ssocketTool <applet> -h%s' for applet help.\n\n",
           UI_BOLD, UI_RESET);
}
