/*
 * applet.h — BusyBox-style applet registry.
 */
#ifndef SOCKETTOOL_APPLET_H
#define SOCKETTOOL_APPLET_H

typedef int (*applet_main_fn)(int argc, char **argv);

struct applet {
    const char    *name;
    applet_main_fn main;
    int            summary_key;   /* i18n key, see i18n/i18n.h */
};

extern const struct applet applets[];

const struct applet *find_applet(const char *name);
void print_applet_list(void);

/* applet entry points */
int tcp_client_main(int argc, char **argv);
int tcp_server_main(int argc, char **argv);
int udp_client_main(int argc, char **argv);
int udp_server_main(int argc, char **argv);
int ws_client_main(int argc, char **argv);
int ws_server_main(int argc, char **argv);
int bping_main(int argc, char **argv);
int btest_main(int argc, char **argv);
int diag_main(int argc, char **argv);

#endif
