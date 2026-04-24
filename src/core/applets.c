/*
 * applets.c — registry of available applets.
 */
#include <string.h>
#include <stdio.h>

#include "core/applet.h"
#include "ui/ui.h"
#include "i18n/i18n.h"

const struct applet applets[] = {
    { "tcp-client", tcp_client_main, T_S_TCP_CLIENT },
    { "tcp-server", tcp_server_main, T_S_TCP_SERVER },
    { "udp-client", udp_client_main, T_S_UDP_CLIENT },
    { "udp-server", udp_server_main, T_S_UDP_SERVER },
    { "ws-client",  ws_client_main,  T_S_WS_CLIENT  },
    { "ws-server",  ws_server_main,  T_S_WS_SERVER  },
    { "bping",      bping_main,      T_S_BPING      },
    { "btest",      btest_main,      T_S_BTEST      },
    { "diag",       diag_main,       T_S_DIAG       },
    { "http-client", http_client_main, T_S_HTTP_CLIENT },
    { "http-server", http_server_main, T_S_HTTP_SERVER },
    { "mqtt-client", mqtt_client_main, T_S_MQTT_CLIENT },
    { "mqtt-server", mqtt_server_main, T_S_MQTT_SERVER },
    { NULL, NULL, 0 },
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
    ui_title(T(T_TITLE));
    printf("\n  %s%s%s\n", UI_BOLD, T(T_USAGE_LINE_1), UI_RESET);
    printf("  %s\n\n", T(T_USAGE_LINE_2));
    printf("  %s%s%s\n", UI_BOLD, T(T_AVAILABLE_APPLETS), UI_RESET);
    for (const struct applet *a = applets; a->name; a++) {
        printf("    %s%s%-12s%s  %s\n",
               UI_BOLD, UI_CYAN, a->name, UI_RESET, T(a->summary_key));
    }
    printf("\n  %s\n\n", T(T_APPLET_HELP_HINT));
}
