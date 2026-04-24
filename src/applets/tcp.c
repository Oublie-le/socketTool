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
#include <pthread.h>

#include "core/applet.h"
#include "ui/ui.h"
#include "net/net.h"
#include "i18n/i18n.h"

static volatile int g_stop;

static void tcp_client_help(void)
{
    printf("%s: tcp-client -H <host> -p <port> [%s]\n"
           "  -H, --host HOST       remote host (%s)\n"
           "  -p, --port PORT       remote port (%s)\n"
           "  -m, --message TEXT    send TEXT after connect\n"
           "  -i, --interactive     interactive mode (stdin <-> socket)\n"
           "  -t, --timeout MS      connect timeout (%s 5000)\n"
           "  -c, --count N         send the message N times then exit\n"
           "  -B, --bench SECS      throughput benchmark: spam data for SECS seconds\n"
           "  -h, --help            %s\n",
           T(T_USAGE), T(T_OPTIONS), T(T_REQUIRED), T(T_REQUIRED),
           T(T_DEFAULT), T(T_HELP));
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
    int interactive = 0, timeout = 5000, count = 1, bench = 0;

    static struct option opts[] = {
        {"host", 1, 0, 'H'}, {"port", 1, 0, 'p'},
        {"message", 1, 0, 'm'}, {"interactive", 0, 0, 'i'},
        {"timeout", 1, 0, 't'}, {"count", 1, 0, 'c'},
        {"bench", 1, 0, 'B'},
        {"help", 0, 0, 'h'}, {0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "H:p:m:it:c:B:h", opts, NULL)) != -1) {
        switch (c) {
        case 'H': host = optarg; break;
        case 'p': port = optarg; break;
        case 'm': msg = optarg; break;
        case 'i': interactive = 1; break;
        case 't': timeout = atoi(optarg); break;
        case 'c': count = atoi(optarg); break;
        case 'B': bench = atoi(optarg); break;
        case 'h': tcp_client_help(); return 0;
        default: tcp_client_help(); return 1;
        }
    }
    if (!host || !port) { tcp_client_help(); return 1; }

    alias_apply_host(&host, &port);

    install_sigint(&g_stop);

    ui_section(ui_icon_globe(), "TCP client");
    ui_kv(T(T_TARGET),     "%s%s:%s%s", UI_BCYAN, host, port, UI_RESET);
    ui_kv(T(T_TIMEOUT_MS), "%d ms", timeout);
    if (bench > 0) ui_kv("bench", "%d s", bench);

    long t0 = now_ms();
    int fd = tcp_connect(host, port, timeout);
    if (fd < 0) { ui_err(T(T_E_CONNECT), strerror(errno)); return 2; }
    ui_ok(T(T_CONNECTED_IN), now_ms() - t0);

    if (bench > 0) {
        /* fire-hose send fixed-size buffer until deadline; count bytes */
        char buf[65536];
        for (size_t i = 0; i < sizeof(buf); i++) buf[i] = 'A' + (i & 31);
        long deadline = now_ms() + (long)bench * 1000;
        long long total = 0;
        long ts = now_ms();
        while (!g_stop && now_ms() < deadline) {
            ssize_t w = send(fd, buf, sizeof(buf), 0);
            if (w <= 0) break;
            total += w;
        }
        long te = now_ms();
        double secs = (te - ts) / 1000.0;
        double mbps = secs > 0 ? (total * 8.0 / 1e6) / secs : 0;
        ui_ok("bench: %.2f MB in %.2fs  = %s%.2f Mbps%s",
              total / 1048576.0, secs, UI_BGREEN, mbps, UI_RESET);
        close(fd);
        return 0;
    }

    if (msg) {
        for (int i = 0; i < count && !g_stop; i++) {
            if (send(fd, msg, strlen(msg), 0) < 0) {
                ui_err(T(T_E_SEND), strerror(errno)); close(fd); return 3;
            }
            ui_ok(T(T_SENT_BYTES), strlen(msg), i + 1);
        }
    }

    if (interactive || !msg) {
        ui_info(T(T_INTERACTIVE_MODE));
        tcp_pump(fd);
    } else {
        set_recv_timeout(fd, 500);
        char buf[4096]; ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) { fwrite(buf, 1, n, stdout); fputc('\n', stdout); }
    }
    close(fd);
    return 0;
}

static void tcp_server_help(void)
{
    printf("%s: tcp-server -p <port> [%s]\n"
           "  -H, --host HOST       bind address (%s: any)\n"
           "  -p, --port PORT       listen port (%s)\n"
           "  -e, --echo            echo back received data (%s)\n"
           "  -d, --discard         discard received data\n"
           "  -1, --once            accept a single client then exit\n"
           "  -h, --help            %s\n",
           T(T_USAGE), T(T_OPTIONS), T(T_DEFAULT), T(T_REQUIRED),
           T(T_DEFAULT), T(T_HELP));
}

struct serve_arg {
    int                       cfd;
    struct sockaddr_storage   peer;
    int                       echo;
};

static void serve_one(int cfd, struct sockaddr_storage *peer, int echo)
{
    char host[64], serv[16];
    getnameinfo((struct sockaddr*)peer, sizeof(*peer),
                host, sizeof(host), serv, sizeof(serv),
                NI_NUMERICHOST | NI_NUMERICSERV);
    ui_ok(T(T_ACCEPTED), host, serv);

    char buf[4096];
    long long total = 0;
    long last_report = now_ms();
    for (;;) {
        ssize_t n = recv(cfd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        if (echo) {
            fprintf(stdout, "%s%s%s %.*s%s\n",
                    UI_BMAGENTA, ui_icon_recv(), UI_RESET, (int)n, buf,
                    buf[n-1] == '\n' ? "" : "");
            send(cfd, buf, n, 0);
        } else {
            total += n;
            long now = now_ms();
            if (now - last_report >= 1000) {
                fprintf(stdout, "%s%s%s %s:%s %lld bytes\n",
                        UI_BMAGENTA, ui_icon_recv(), UI_RESET,
                        host, serv, total);
                last_report = now;
            }
        }
    }
    if (!echo && total > 0) {
        fprintf(stdout, "%s%s%s %s:%s total %lld bytes\n",
                UI_BMAGENTA, ui_icon_recv(), UI_RESET, host, serv, total);
    }
    ui_info(T(T_CLOSED), host, serv);
    close(cfd);
}

static void *serve_thread(void *p)
{
    struct serve_arg *a = p;
    serve_one(a->cfd, &a->peer, a->echo);
    free(a);
    return NULL;
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
    if (sfd < 0) { ui_err("%s %s:%s", T(T_E_LISTEN), host?host:"*", port); return 2; }

    ui_section(ui_icon_globe(), "TCP server");
    ui_kv(T(T_LISTEN), "%s%s:%s%s", UI_BCYAN, host?host:"0.0.0.0", port, UI_RESET);
    ui_kv(T(T_MODE),   "%s%s%s", echo?UI_BGREEN:UI_BYELLOW,
                                  echo?T(T_ECHO):T(T_DISCARD), UI_RESET);
    ui_info(T(T_WAITING_CLIENTS));

    while (!g_stop) {
        struct sockaddr_storage peer; socklen_t plen = sizeof(peer);
        int cfd = accept(sfd, (struct sockaddr*)&peer, &plen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            ui_err(T(T_E_RECV), strerror(errno)); break;
        }
        if (once) {
            serve_one(cfd, &peer, echo);
            break;
        }
        struct serve_arg *a = malloc(sizeof(*a));
        if (!a) { close(cfd); continue; }
        a->cfd = cfd; a->peer = peer; a->echo = echo;
        pthread_t th;
        if (pthread_create(&th, NULL, serve_thread, a) != 0) {
            ui_warn("pthread_create: %s", strerror(errno));
            close(cfd); free(a); continue;
        }
        pthread_detach(th);
    }
    close(sfd);
    return 0;
}
