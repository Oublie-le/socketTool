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

#include "core/applet.h"
#include "ui/ui.h"
#include "net/net.h"
#include "i18n/i18n.h"

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
           "  -B, --bench SECS      throughput benchmark: spam datagrams for SECS\n"
           "  -h, --help            show this help\n");
}

int udp_client_main(int argc, char **argv)
{
    const char *host = NULL, *port = NULL, *msg = "ping";
    int count = 1, interval = 1000, wait_ms = 500, bench = 0;

    static struct option opts[] = {
        {"host", 1, 0, 'H'}, {"port", 1, 0, 'p'},
        {"message", 1, 0, 'm'}, {"count", 1, 0, 'c'},
        {"interval", 1, 0, 'i'}, {"wait", 1, 0, 'w'},
        {"bench", 1, 0, 'B'},
        {"help", 0, 0, 'h'}, {0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "H:p:m:c:i:w:B:h", opts, NULL)) != -1) {
        switch (c) {
        case 'H': host = optarg; break;
        case 'p': port = optarg; break;
        case 'm': msg = optarg; break;
        case 'c': count = atoi(optarg); break;
        case 'i': interval = atoi(optarg); break;
        case 'w': wait_ms = atoi(optarg); break;
        case 'B': bench = atoi(optarg); break;
        case 'h': udp_client_help(); return 0;
        default: udp_client_help(); return 1;
        }
    }
    if (!host || !port) { udp_client_help(); return 1; }

    install_sigint(&g_stop);

    int fd = udp_socket(host, port, 0);
    if (fd < 0) { ui_err("udp socket: %s", strerror(errno)); return 2; }
    set_recv_timeout(fd, wait_ms);

    if (bench > 0) {
        ui_section(ui_icon_globe(), "UDP client (bench)");
        ui_kv(T(T_TARGET), "%s%s:%s%s", UI_BCYAN, host, port, UI_RESET);
        ui_kv("bench",     "%d s", bench);
        char buf[1400];                    /* typical MTU-safe payload */
        for (size_t i = 0; i < sizeof(buf); i++) buf[i] = 'A' + (i & 31);
        long deadline = now_ms() + (long)bench * 1000;
        long long total = 0, pkts = 0;
        long ts = now_ms();
        while (!g_stop && now_ms() < deadline) {
            ssize_t w = send(fd, buf, sizeof(buf), 0);
            if (w <= 0) break;
            total += w; pkts++;
        }
        long te = now_ms();
        double secs = (te - ts) / 1000.0;
        double mbps = secs > 0 ? (total * 8.0 / 1e6) / secs : 0;
        ui_ok("bench: %lld pkts / %.2f MB in %.2fs = %s%.2f Mbps%s",
              pkts, total / 1048576.0, secs, UI_BGREEN, mbps, UI_RESET);
        close(fd);
        return 0;
    }

    ui_section(ui_icon_globe(), "UDP client");
    ui_kv(T(T_TARGET),   "%s%s:%s%s", UI_BCYAN, host, port, UI_RESET);
    ui_kv(T(T_COUNT),    "%d", count);
    ui_kv(T(T_INTERVAL), "%d ms", interval);

    int ok = 0, lost = 0;
    char buf[4096];
    for (int i = 0; i < count && !g_stop; i++) {
        long t0 = now_ms();
        if (send(fd, msg, strlen(msg), 0) < 0) {
            ui_err(T(T_E_SEND), strerror(errno)); lost++;
            goto sleep_next;
        }
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        long rtt = now_ms() - t0;
        if (n > 0) {
            ui_ok(T(T_REPLY), i + 1, rtt, (int)n, buf);
            ok++;
        } else {
            ui_warn(T(T_NO_REPLY), i + 1, wait_ms);
            lost++;
        }
sleep_next:
        if (i + 1 < count && interval > 0) usleep(interval * 1000);
    }
    close(fd);

    ui_section(ui_icon_hourglass(), T(T_SUMMARY));
    ui_kv(T(T_SENT), "%d", ok + lost);
    ui_kv(T(T_OK_LBL),  "%s%s %d%s", UI_BGREEN, ui_icon_ok(), ok, UI_RESET);
    ui_kv(T(T_LOST), "%s%s %d%s",
          lost?UI_BRED:UI_BGREEN,
          lost?ui_icon_fail():ui_icon_ok(), lost, UI_RESET);
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
    if (fd < 0) { ui_err(T(T_E_BIND), strerror(errno)); return 2; }

    ui_section(ui_icon_globe(), "UDP server");
    ui_kv(T(T_LISTEN), "%s%s:%s%s", UI_BCYAN, host?host:"0.0.0.0", port, UI_RESET);
    ui_kv(T(T_MODE),   "%s%s%s", echo?UI_BGREEN:UI_BYELLOW,
                                  echo?T(T_ECHO):T(T_DISCARD), UI_RESET);
    ui_info(T(T_WAITING_DGRAMS));

    char buf[4096];
    long long total = 0, pkts = 0;
    long last_report = now_ms();
    while (!g_stop) {
        struct sockaddr_storage peer; socklen_t plen = sizeof(peer);
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
                             (struct sockaddr*)&peer, &plen);
        if (n < 0) {
            if (errno == EINTR) continue;
            ui_err(T(T_E_RECV), strerror(errno)); break;
        }
        char hbuf[64], sbuf[16];
        getnameinfo((struct sockaddr*)&peer, plen,
                    hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        if (echo) {
            printf("%s%s%s [%s:%s] %.*s\n",
                   UI_BMAGENTA, ui_icon_recv(), UI_RESET,
                   hbuf, sbuf, (int)n, buf);
            sendto(fd, buf, n, 0, (struct sockaddr*)&peer, plen);
        } else {
            total += n; pkts++;
            long now = now_ms();
            if (now - last_report >= 1000) {
                printf("%s%s%s [%s:%s] %lld pkts / %lld bytes\n",
                       UI_BMAGENTA, ui_icon_recv(), UI_RESET,
                       hbuf, sbuf, pkts, total);
                last_report = now;
            }
        }
    }
    close(fd);
    return 0;
}
