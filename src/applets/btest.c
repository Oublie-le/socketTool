/*
 * btest.c — batch protocol connectivity test.
 *
 * Targets are 'host:port[:proto]' (proto = tcp|udp|ws, default chosen by -P).
 * Source from -f <file> and/or trailing positional args.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/socket.h>

#include "core/applet.h"
#include "ui/ui.h"
#include "net/net.h"

enum { P_TCP, P_UDP, P_WS };

struct target {
    char  spec[160];
    char  host[96];
    char  port[16];
    int   proto;
    int   ok;
    long  rtt_ms;
    char  info[64];
};

struct btest_cfg {
    int  default_proto;
    int  timeout_ms;
};

struct work {
    struct target    *t;
    int               n;
    struct btest_cfg *cfg;
    pthread_mutex_t   lock;
    int               next;
};

static int proto_from(const char *s)
{
    if (!s || !*s) return -1;
    if (!strcasecmp(s, "tcp")) return P_TCP;
    if (!strcasecmp(s, "udp")) return P_UDP;
    if (!strcasecmp(s, "ws"))  return P_WS;
    return -1;
}

static const char *proto_name(int p)
{
    switch (p) { case P_TCP:return "tcp"; case P_UDP:return "udp";
                 case P_WS:return "ws"; }
    return "?";
}

/* parse "host:port[:proto]" — returns 0 on ok */
static int parse_target(struct target *t, const char *spec, int default_proto)
{
    snprintf(t->spec, sizeof(t->spec), "%s", spec);
    char tmp[160];
    snprintf(tmp, sizeof(tmp), "%s", spec);

    char *p1 = strchr(tmp, ':');
    if (!p1) return -1;
    *p1++ = '\0';
    char *p2 = strchr(p1, ':');
    if (p2) *p2++ = '\0';

    snprintf(t->host, sizeof(t->host), "%s", tmp);
    snprintf(t->port, sizeof(t->port), "%s", p1);
    t->proto = p2 ? proto_from(p2) : default_proto;
    if (t->proto < 0) return -1;
    return 0;
}

static int probe_udp(const char *host, const char *port, int timeout_ms)
{
    /* "Connect" UDP socket then send a probe; consider OK if no ICMP error
       arrives within timeout. This catches "connection refused" via async err. */
    int fd = udp_socket(host, port, 0);
    if (fd < 0) return -1;
    set_recv_timeout(fd, timeout_ms);
    const char *m = "btest";
    if (send(fd, m, strlen(m), 0) < 0) { close(fd); return -1; }
    char buf[64];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    close(fd);
    if (n >= 0)              return 0;     /* got a reply */
    if (errno == EAGAIN ||
        errno == EWOULDBLOCK) return 0;     /* no reply, assumed reachable */
    return -1;                              /* ECONNREFUSED etc */
}

static int probe_ws(const char *host, const char *port, int timeout_ms)
{
    /* Quick check: TCP connect + send minimal handshake, expect "HTTP/1.1 101". */
    int fd = tcp_connect(host, port, timeout_ms);
    if (fd < 0) return -1;
    set_recv_timeout(fd, timeout_ms);
    char req[512];
    int rl = snprintf(req, sizeof(req),
        "GET / HTTP/1.1\r\nHost: %s:%s\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n", host, port);
    if (send(fd, req, rl, 0) != rl) { close(fd); return -1; }
    char resp[64];
    ssize_t n = recv(fd, resp, sizeof(resp) - 1, 0);
    close(fd);
    if (n < 12) return -1;
    resp[n] = '\0';
    return strncmp(resp, "HTTP/1.1 101", 12) == 0 ? 0 : -1;
}

static void *worker(void *p)
{
    struct work *w = p;
    for (;;) {
        pthread_mutex_lock(&w->lock);
        int i = w->next++;
        pthread_mutex_unlock(&w->lock);
        if (i >= w->n) break;

        struct target *t = &w->t[i];
        long t0 = now_ms();
        int rc = -1;
        switch (t->proto) {
        case P_TCP: {
            int fd = tcp_connect(t->host, t->port, w->cfg->timeout_ms);
            if (fd >= 0) { rc = 0; close(fd); }
            else snprintf(t->info, sizeof(t->info), "%s", strerror(errno));
            break;
        }
        case P_UDP:
            rc = probe_udp(t->host, t->port, w->cfg->timeout_ms);
            if (rc < 0) snprintf(t->info, sizeof(t->info), "%s", strerror(errno));
            else snprintf(t->info, sizeof(t->info), "no err");
            break;
        case P_WS:
            rc = probe_ws(t->host, t->port, w->cfg->timeout_ms);
            if (rc < 0) snprintf(t->info, sizeof(t->info), "no 101 upgrade");
            else snprintf(t->info, sizeof(t->info), "101 upgrade");
            break;
        }
        t->ok = rc == 0;
        t->rtt_ms = now_ms() - t0;
    }
    return NULL;
}

static void btest_help(void)
{
    printf("Usage: btest [-f targets.txt] [target ...] [options]\n"
           "\n"
           "  Each target: host:port[:proto]   (proto = tcp|udp|ws)\n"
           "\n"
           "  -f, --file FILE       read targets from FILE (one per line, # comments)\n"
           "  -P, --proto PROTO     default protocol when target omits it (default: tcp)\n"
           "  -t, --timeout MS      per-target timeout (default 1500)\n"
           "  -j, --jobs N          parallel workers (default 16)\n"
           "  -h, --help            show this help\n");
}

int btest_main(int argc, char **argv)
{
    const char *file = NULL;
    struct btest_cfg cfg = { .default_proto = P_TCP, .timeout_ms = 1500 };
    int jobs = 16;

    static struct option opts[] = {
        {"file",1,0,'f'},{"proto",1,0,'P'},{"timeout",1,0,'t'},
        {"jobs",1,0,'j'},{"help",0,0,'h'},{0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "f:P:t:j:h", opts, NULL)) != -1) {
        switch (c) {
        case 'f': file = optarg; break;
        case 'P': {
            int p = proto_from(optarg);
            if (p < 0) { ui_err("bad proto: %s", optarg); return 1; }
            cfg.default_proto = p; break;
        }
        case 't': cfg.timeout_ms = atoi(optarg); break;
        case 'j': jobs = atoi(optarg); break;
        case 'h': btest_help(); return 0;
        default:  btest_help(); return 1;
        }
    }

    int cap = 64, n = 0;
    struct target *t = calloc(cap, sizeof(*t));
    if (file) {
        FILE *fp = fopen(file, "r");
        if (!fp) { ui_err("open %s: %s", file, strerror(errno)); free(t); return 1; }
        char buf[160];
        while (read_line_strip(buf, sizeof(buf), fp)) {
            if (n >= cap) { cap *= 2; t = realloc(t, cap * sizeof(*t)); }
            memset(&t[n], 0, sizeof(t[n]));
            if (parse_target(&t[n], buf, cfg.default_proto) < 0) {
                ui_warn("skip bad target: %s", buf); continue;
            }
            n++;
        }
        fclose(fp);
    }
    for (int i = optind; i < argc; i++) {
        if (n >= cap) { cap *= 2; t = realloc(t, cap * sizeof(*t)); }
        memset(&t[n], 0, sizeof(t[n]));
        if (parse_target(&t[n], argv[i], cfg.default_proto) < 0) {
            ui_warn("skip bad target: %s", argv[i]); continue;
        }
        n++;
    }
    if (n == 0) { btest_help(); free(t); return 1; }

    ui_section("Batch connectivity test");
    ui_kv("targets", "%d", n);
    ui_kv("default", "%s", proto_name(cfg.default_proto));
    ui_kv("timeout", "%d ms", cfg.timeout_ms);
    ui_kv("jobs",    "%d", jobs);

    struct work w = { .t=t, .n=n, .cfg=&cfg, .next=0 };
    pthread_mutex_init(&w.lock, NULL);
    if (jobs < 1) jobs = 1;
    if (jobs > n) jobs = n;
    pthread_t *th = calloc(jobs, sizeof(*th));
    long t0 = now_ms();
    for (int i = 0; i < jobs; i++) pthread_create(&th[i], NULL, worker, &w);
    for (int i = 0; i < jobs; i++) pthread_join(th[i], NULL);
    long elapsed = now_ms() - t0;

    const char *cols[]   = { "target", "proto", "result", "rtt(ms)", "info" };
    const int   widths[] = { 30,       5,       6,        8,         24 };
    putchar('\n');
    ui_table_header(cols, widths, 5);

    int ok = 0, fail = 0;
    for (int i = 0; i < n; i++) {
        char rttbuf[16]; snprintf(rttbuf, sizeof(rttbuf), "%ld", t[i].rtt_ms);
        const char *cells[] = {
            t[i].spec, proto_name(t[i].proto),
            t[i].ok ? "OK" : "FAIL", rttbuf, t[i].info,
        };
        ui_table_row(cells, widths, 5, t[i].ok ? UI_GREEN : UI_RED);
        t[i].ok ? ok++ : fail++;
    }
    ui_table_sep(widths, 5);
    putchar('\n');
    ui_kv("ok",      "%s%d%s", UI_GREEN, ok, UI_RESET);
    ui_kv("fail",    "%s%d%s", fail ? UI_RED : UI_GREEN, fail, UI_RESET);
    ui_kv("elapsed", "%ld ms", elapsed);

    pthread_mutex_destroy(&w.lock);
    free(th); free(t);
    return fail == 0 ? 0 : 4;
}
