/*
 * tcp.c — tcp-client and tcp-server applets.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "applet.h"
#include "ui.h"
#include "util.h"

static volatile int g_stop;

/* ------------------------------------------------------------------ client */

static void tcp_client_help(void)
{
    printf("Usage: tcp-client -H <host> -p <port> [options]\n"
           "  -H, --host HOST       remote host (required)\n"
           "  -p, --port PORT       remote port (required)\n"
           "  -m, --message TEXT    send TEXT after connect\n"
           "  -i, --interactive     interactive mode (stdin <-> socket)\n"
           "  -t, --timeout MS      connect timeout (default 5000)\n"
           "  -c, --count N         send the message N times then exit\n"
           "  -h, --help            show this help\n");
}

static int tcp_pump(int fd)
{
    char buf[4096];
    fd_set rfds;
    int maxfd = fd > STDIN_FILENO ? fd : STDIN_FILENO;
    while (!g_stop) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        FD_SET(STDIN_FILENO, &rfds);
        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (FD_ISSET(fd, &rfds)) {
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) return n == 0 ? 0 : -1;
            fwrite(buf, 1, n, stdout); fflush(stdout);
        }
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) return 0;
            if (send(fd, buf, n, 0) < 0) return -1;
        }
    }
    return 0;
}

int tcp_client_main(int argc, char **argv)
{
    const char *host = NULL, *port = NULL, *msg = NULL;
    int interactive = 0, timeout = 5000, count = 1;

    static struct option opts[] = {
        {"host", 1, 0, 'H'}, {"port", 1, 0, 'p'},
        {"message", 1, 0, 'm'}, {"interactive", 0, 0, 'i'},
        {"timeout", 1, 0, 't'}, {"count", 1, 0, 'c'},
        {"help", 0, 0, 'h'}, {0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "H:p:m:it:c:h", opts, NULL)) != -1) {
        switch (c) {
        case 'H': host = optarg; break;
        case 'p': port = optarg; break;
        case 'm': msg = optarg; break;
        case 'i': interactive = 1; break;
        case 't': timeout = atoi(optarg); break;
        case 'c': count = atoi(optarg); break;
        case 'h': tcp_client_help(); return 0;
        default: tcp_client_help(); return 1;
        }
    }
    if (!host || !port) { tcp_client_help(); return 1; }

    install_sigint(&g_stop);

    ui_section("TCP client");
    ui_kv("target", "%s:%s", host, port);
    ui_kv("timeout", "%d ms", timeout);

    long t0 = now_ms();
    int fd = tcp_connect(host, port, timeout);
    if (fd < 0) { ui_err("connect failed: %s", strerror(errno)); return 2; }
    ui_ok("connected in %ld ms", now_ms() - t0);

    if (msg) {
        for (int i = 0; i < count && !g_stop; i++) {
            if (send(fd, msg, strlen(msg), 0) < 0) {
                ui_err("send: %s", strerror(errno)); close(fd); return 3;
            }
            ui_ok("sent %zu bytes (#%d)", strlen(msg), i + 1);
        }
    }

    if (interactive || !msg) {
        ui_info("entering interactive mode (Ctrl-C to quit)");
        tcp_pump(fd);
    } else {
        /* drain a short reply */
        set_recv_timeout(fd, 500);
        char buf[4096]; ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) { fwrite(buf, 1, n, stdout); fputc('\n', stdout); }
    }
    close(fd);
    return 0;
}

/* ------------------------------------------------------------------ server */

static void tcp_server_help(void)
{
    printf("Usage: tcp-server -p <port> [options]\n"
           "  -H, --host HOST       bind address (default: any)\n"
           "  -p, --port PORT       listen port (required)\n"
           "  -e, --echo            echo back received data (default)\n"
           "  -d, --discard         discard received data\n"
           "  -1, --once            accept a single client then exit\n"
           "  -h, --help            show this help\n");
}

static void serve_one(int cfd, struct sockaddr_storage *peer, int echo)
{
    char host[64], serv[16];
    getnameinfo((struct sockaddr*)peer, sizeof(*peer),
                host, sizeof(host), serv, sizeof(serv),
                NI_NUMERICHOST | NI_NUMERICSERV);
    ui_ok("accepted %s:%s", host, serv);

    char buf[4096];
    for (;;) {
        ssize_t n = recv(cfd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        fprintf(stdout, "%s<<%s %.*s%s\n",
                UI_DIM, UI_RESET, (int)n, buf,
                buf[n-1] == '\n' ? "" : "");
        if (echo) send(cfd, buf, n, 0);
    }
    ui_info("closed %s:%s", host, serv);
    close(cfd);
}

int tcp_server_main(int argc, char **argv)
{
    const char *host = NULL, *port = NULL;
    int echo = 1, once = 0;

    static struct option opts[] = {
        {"host", 1, 0, 'H'}, {"port", 1, 0, 'p'},
        {"echo", 0, 0, 'e'}, {"discard", 0, 0, 'd'},
        {"once", 0, 0, '1'}, {"help", 0, 0, 'h'}, {0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "H:p:ed1h", opts, NULL)) != -1) {
        switch (c) {
        case 'H': host = optarg; break;
        case 'p': port = optarg; break;
        case 'e': echo = 1; break;
        case 'd': echo = 0; break;
        case '1': once = 1; break;
        case 'h': tcp_server_help(); return 0;
        default: tcp_server_help(); return 1;
        }
    }
    if (!port) { tcp_server_help(); return 1; }

    install_sigint(&g_stop);

    int sfd = tcp_listen(host, port, 16);
    if (sfd < 0) { ui_err("listen %s:%s failed", host?host:"*", port); return 2; }

    ui_section("TCP server");
    ui_kv("listen", "%s:%s", host?host:"0.0.0.0", port);
    ui_kv("mode", "%s", echo ? "echo" : "discard");
    ui_info("waiting for clients (Ctrl-C to quit)");

    while (!g_stop) {
        struct sockaddr_storage peer; socklen_t plen = sizeof(peer);
        int cfd = accept(sfd, (struct sockaddr*)&peer, &plen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            ui_err("accept: %s", strerror(errno)); break;
        }
        serve_one(cfd, &peer, echo);
        if (once) break;
    }
    close(sfd);
    return 0;
}
