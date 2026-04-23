/*
 * udp.c — udp-client and udp-server applets.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "applet.h"
#include "ui.h"
#include "util.h"

static volatile int g_stop;

static void udp_client_help(void)
{
    printf("Usage: udp-client -H <host> -p <port> [options]\n"
           "  -H, --host HOST       remote host (required)\n"
           "  -p, --port PORT       remote port (required)\n"
           "  -m, --message TEXT    payload to send (default: ping)\n"
           "  -c, --count N         send N datagrams (default 1)\n"
           "  -i, --interval MS     ms between sends (default 1000)\n"
           "  -w, --wait MS         ms to wait for reply per send (default 500)\n"
           "  -h, --help            show this help\n");
}

int udp_client_main(int argc, char **argv)
{
    const char *host = NULL, *port = NULL, *msg = "ping";
    int count = 1, interval = 1000, wait_ms = 500;

    static struct option opts[] = {
        {"host", 1, 0, 'H'}, {"port", 1, 0, 'p'},
        {"message", 1, 0, 'm'}, {"count", 1, 0, 'c'},
        {"interval", 1, 0, 'i'}, {"wait", 1, 0, 'w'},
        {"help", 0, 0, 'h'}, {0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "H:p:m:c:i:w:h", opts, NULL)) != -1) {
        switch (c) {
        case 'H': host = optarg; break;
        case 'p': port = optarg; break;
        case 'm': msg = optarg; break;
        case 'c': count = atoi(optarg); break;
        case 'i': interval = atoi(optarg); break;
        case 'w': wait_ms = atoi(optarg); break;
        case 'h': udp_client_help(); return 0;
        default: udp_client_help(); return 1;
        }
    }
    if (!host || !port) { udp_client_help(); return 1; }

    install_sigint(&g_stop);

    int fd = udp_socket(host, port, 0);
    if (fd < 0) { ui_err("udp socket: %s", strerror(errno)); return 2; }
    set_recv_timeout(fd, wait_ms);

    ui_section("UDP client");
    ui_kv("target", "%s:%s", host, port);
    ui_kv("count", "%d", count);
    ui_kv("interval", "%d ms", interval);

    int ok = 0, lost = 0;
    char buf[4096];
    for (int i = 0; i < count && !g_stop; i++) {
        long t0 = now_ms();
        if (send(fd, msg, strlen(msg), 0) < 0) {
            ui_err("send: %s", strerror(errno)); lost++;
            goto sleep_next;
        }
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        long rtt = now_ms() - t0;
        if (n > 0) {
            ui_ok("#%d  %ld ms  reply: %.*s", i + 1, rtt, (int)n, buf);
            ok++;
        } else {
            ui_warn("#%d  no reply within %d ms", i + 1, wait_ms);
            lost++;
        }
sleep_next:
        if (i + 1 < count && interval > 0) usleep(interval * 1000);
    }
    close(fd);

    ui_section("Summary");
    ui_kv("sent", "%d", ok + lost);
    ui_kv("ok",   "%s%d%s", UI_GREEN, ok, UI_RESET);
    ui_kv("lost", "%s%d%s", lost ? UI_RED : UI_GREEN, lost, UI_RESET);
    return lost == 0 ? 0 : 3;
}

static void udp_server_help(void)
{
    printf("Usage: udp-server -p <port> [options]\n"
           "  -H, --host HOST       bind address (default: any)\n"
           "  -p, --port PORT       listen port (required)\n"
           "  -e, --echo            echo datagrams back (default)\n"
           "  -d, --discard         discard datagrams\n"
           "  -h, --help            show this help\n");
}

int udp_server_main(int argc, char **argv)
{
    const char *host = NULL, *port = NULL;
    int echo = 1;

    static struct option opts[] = {
        {"host", 1, 0, 'H'}, {"port", 1, 0, 'p'},
        {"echo", 0, 0, 'e'}, {"discard", 0, 0, 'd'},
        {"help", 0, 0, 'h'}, {0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "H:p:edh", opts, NULL)) != -1) {
        switch (c) {
        case 'H': host = optarg; break;
        case 'p': port = optarg; break;
        case 'e': echo = 1; break;
        case 'd': echo = 0; break;
        case 'h': udp_server_help(); return 0;
        default: udp_server_help(); return 1;
        }
    }
    if (!port) { udp_server_help(); return 1; }

    install_sigint(&g_stop);

    int fd = udp_socket(host, port, 1);
    if (fd < 0) { ui_err("udp bind: %s", strerror(errno)); return 2; }

    ui_section("UDP server");
    ui_kv("listen", "%s:%s", host?host:"0.0.0.0", port);
    ui_kv("mode", "%s", echo ? "echo" : "discard");
    ui_info("waiting for datagrams (Ctrl-C to quit)");

    char buf[4096];
    while (!g_stop) {
        struct sockaddr_storage peer; socklen_t plen = sizeof(peer);
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
                             (struct sockaddr*)&peer, &plen);
        if (n < 0) {
            if (errno == EINTR) continue;
            ui_err("recv: %s", strerror(errno)); break;
        }
        char hbuf[64], sbuf[16];
        getnameinfo((struct sockaddr*)&peer, plen,
                    hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        printf("%s<<%s [%s:%s] %.*s\n", UI_DIM, UI_RESET,
               hbuf, sbuf, (int)n, buf);
        if (echo) sendto(fd, buf, n, 0, (struct sockaddr*)&peer, plen);
    }
    close(fd);
    return 0;
}
