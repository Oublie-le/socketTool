/*
 * bping.c — batch ping applet.
 *
 * Probe modes:
 *   - tcp  (default): TCP connect to a port (no privileges required)
 *   - icmp:           native ICMP echo via raw/dgram socket (no /bin/ping)
 *
 * Targets come from -f <file> and/or trailing positional args. Each target may
 * be a single host, an IP range, a CIDR block or a hostname — see help text.
 *
 * Output is a table with: target | resolved IP | hostname (rDNS) |
 *                         result | rtt(ms) | info
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "core/applet.h"
#include "ui/ui.h"
#include "net/net.h"
#include "net/discover.h"
#include "i18n/i18n.h"

static volatile int g_stop;

struct host {
    char       name[128];        /* original target (e.g. "192.168.1.10") */
    char       ip[16];           /* resolved IPv4 dotted */
    char       hostname[128];    /* rDNS / mDNS / NBNS, or "" if all failed */
    char       hostname_src[8];  /* "dns" | "mdns" | "nbns" | ""           */
    char       mac[20];          /* from /proc/net/arp, or "" */
    int        ok;
    long       rtt_ms;
    char       err[64];
};

struct bping_cfg {
    int        timeout_ms;
    int        mode_icmp;
    int        no_rdns;
    int        no_discover;       /* skip mDNS / NBNS / ARP */
    int        quiet;             /* suppress progress bar (for json/csv) */
    const char *port;
};

struct work {
    struct host        *hosts;
    int                 n;
    struct bping_cfg   *cfg;
    pthread_mutex_t     lock;
    int                 next;
    int                 done;
    int                 total;
};

static int resolve_ipv4(const char *host, char ip[16])
{
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return -1;
    struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
    inet_ntop(AF_INET, &sa->sin_addr, ip, 16);
    freeaddrinfo(res);
    return 0;
}

static void *worker(void *p)
{
    struct work *w = p;
    for (;;) {
        pthread_mutex_lock(&w->lock);
        int i = w->next++;
        pthread_mutex_unlock(&w->lock);
        if (i >= w->n) break;

        struct host *h = &w->hosts[i];

        if (w->cfg->mode_icmp) {
            char err[64] = "";
            int rtt = icmp_ping_once(h->name,
                                     getpid() & 0xffff, i + 1,
                                     w->cfg->timeout_ms,
                                     h->ip, err, sizeof(err));
            if (rtt >= 0) { h->ok = 1; h->rtt_ms = rtt; }
            else          { h->ok = 0; h->rtt_ms = w->cfg->timeout_ms;
                            snprintf(h->err, sizeof(h->err), "%s",
                                     *err ? err : "no reply"); }
        } else {
            if (resolve_ipv4(h->name, h->ip) < 0)
                snprintf(h->ip, sizeof(h->ip), "?");
            long t0 = now_ms();
            int fd = tcp_connect(h->name, w->cfg->port, w->cfg->timeout_ms);
            h->rtt_ms = now_ms() - t0;
            if (fd >= 0) { h->ok = 1; close(fd); }
            else { h->ok = 0;
                   snprintf(h->err, sizeof(h->err), "%s", strerror(errno)); }
        }

        if (h->ok && h->ip[0] && h->ip[0] != '?') {
            /* 1) DNS PTR */
            if (!w->cfg->no_rdns &&
                reverse_dns(h->ip, h->hostname, sizeof(h->hostname)) == 0 &&
                strcmp(h->hostname, h->ip) != 0) {
                snprintf(h->hostname_src, sizeof(h->hostname_src), "dns");
            } else {
                h->hostname[0] = '\0';
            }
            /* 2) mDNS (Apple/Avahi) */
            if (!h->hostname[0] && !w->cfg->no_discover) {
                if (mdns_lookup(h->ip, 200,
                                h->hostname, sizeof(h->hostname)) == 0)
                    snprintf(h->hostname_src, sizeof(h->hostname_src), "mdns");
            }
            /* 3) NBNS (Windows/Samba) */
            if (!h->hostname[0] && !w->cfg->no_discover) {
                if (nbns_lookup(h->ip, 200,
                                h->hostname, sizeof(h->hostname)) == 0)
                    snprintf(h->hostname_src, sizeof(h->hostname_src), "nbns");
            }
            /* 4) MAC from ARP table (kernel cached after our ICMP probe) */
            if (!w->cfg->no_discover)
                arp_lookup(h->ip, h->mac, sizeof(h->mac));
        }

        pthread_mutex_lock(&w->lock);
        w->done++;
        if (!w->cfg->quiet) ui_progress(w->done, w->total, h->name);
        pthread_mutex_unlock(&w->lock);
    }
    return NULL;
}

static void bping_help(void)
{
    printf("Usage: bping [-f hosts.txt] [target ...] [options]\n"
           "\n"
           "  Each target may be:\n"
           "    192.168.1.10                  single host\n"
           "    192.168.1.10-50               last-octet range\n"
           "    192.168.1.10-192.168.1.50     full IP range\n"
           "    10.0.0.0/24                   CIDR (capped at 65536 hosts)\n"
           "    host.example.com              hostname\n"
           "\n"
           "  -f, --file FILE       read targets from FILE (one per line, # comments)\n"
           "  -m, --mode MODE       'tcp' (default) or 'icmp' (native, no /bin/ping)\n"
           "  -p, --port PORT       port for tcp mode (default 80)\n"
           "  -t, --timeout MS      per-host timeout (default 1500)\n"
           "  -j, --jobs N          parallel workers (default 32)\n"
           "  -n, --no-rdns         skip reverse-DNS lookup\n"
           "  -D, --no-discover     skip mDNS / NetBIOS / ARP discovery\n"
           "  -o, --output FMT      output format: table (default) | json | csv\n"
           "  -W, --watch SECS      re-scan every SECS seconds (Ctrl-C to stop)\n"
           "  -h, --help            show this help\n"
           "\n"
           "  ICMP mode requires raw-socket privileges:\n"
           "    sudo setcap cap_net_raw+ep ./socketTool       (preferred), or\n"
           "    sudo sysctl -w net.ipv4.ping_group_range='0 2147483647'\n");
}

/* ---------------------------------------------------------- output -- */

static void emit_table(struct host *hosts, int n, long elapsed)
{
    const char *cols[]   = {
        T(T_TARGET), T(T_IP), T(T_HOSTNAME), "src", "mac",
        T(T_RESULT), T(T_RTT), T(T_INFO)
    };
    const int   widths[] = { 18, 15, 24, 4, 17, 6, 7, 18 };
    putchar('\n');
    ui_table_header(cols, widths, 8);

    int ok = 0, fail = 0;
    for (int i = 0; i < n; i++) {
        char rttbuf[16]; snprintf(rttbuf, sizeof(rttbuf), "%ld", hosts[i].rtt_ms);
        const char *cells[] = {
            hosts[i].name,
            hosts[i].ip[0]           ? hosts[i].ip           : "-",
            hosts[i].hostname[0]     ? hosts[i].hostname     : "-",
            hosts[i].hostname_src[0] ? hosts[i].hostname_src : "-",
            hosts[i].mac[0]          ? hosts[i].mac          : "-",
            hosts[i].ok ? "OK" : "FAIL",
            rttbuf,
            hosts[i].ok ? "" : hosts[i].err,
        };
        ui_table_row(cells, widths, 8, hosts[i].ok ? UI_BGREEN : UI_BRED);
        hosts[i].ok ? ok++ : fail++;
    }
    ui_table_sep(widths, 8);

    putchar('\n');
    ui_kv(T(T_OK_LBL),   "%s%s %d%s", UI_BGREEN, ui_icon_ok(), ok, UI_RESET);
    ui_kv(T(T_FAIL_LBL), "%s%s %d%s", fail?UI_BRED:UI_BGREEN,
          fail?ui_icon_fail():ui_icon_ok(), fail, UI_RESET);
    ui_kv(T(T_ELAPSED),  "%ld ms", elapsed);
}

static void json_str(const char *s)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char*)s; *p; p++) {
        switch (*p) {
        case '"': fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default:
            if (*p < 0x20) printf("\\u%04x", *p);
            else putchar(*p);
        }
    }
    putchar('"');
}

static void emit_json(struct host *hosts, int n, long elapsed)
{
    int ok = 0;
    for (int i = 0; i < n; i++) if (hosts[i].ok) ok++;

    printf("{\"elapsed_ms\":%ld,\"total\":%d,\"ok\":%d,\"fail\":%d,"
           "\"results\":[", elapsed, n, ok, n - ok);
    for (int i = 0; i < n; i++) {
        if (i) putchar(',');
        printf("{\"target\":");        json_str(hosts[i].name);
        printf(",\"ip\":");            json_str(hosts[i].ip);
        printf(",\"hostname\":");      json_str(hosts[i].hostname);
        printf(",\"hostname_src\":");  json_str(hosts[i].hostname_src);
        printf(",\"mac\":");           json_str(hosts[i].mac);
        printf(",\"ok\":%s",           hosts[i].ok ? "true" : "false");
        printf(",\"rtt_ms\":%ld",      hosts[i].rtt_ms);
        printf(",\"err\":");           json_str(hosts[i].err);
        putchar('}');
    }
    printf("]}\n");
}

static void csv_field(const char *s)
{
    int needs_quote = 0;
    for (const char *p = s; *p; p++)
        if (*p == ',' || *p == '"' || *p == '\n') { needs_quote = 1; break; }
    if (!needs_quote) { fputs(s, stdout); return; }
    putchar('"');
    for (const char *p = s; *p; p++) {
        if (*p == '"') fputs("\"\"", stdout);
        else putchar(*p);
    }
    putchar('"');
}

static void emit_csv(struct host *hosts, int n, long elapsed)
{
    (void)elapsed;
    printf("target,ip,hostname,hostname_src,mac,ok,rtt_ms,err\n");
    for (int i = 0; i < n; i++) {
        csv_field(hosts[i].name);          putchar(',');
        csv_field(hosts[i].ip);            putchar(',');
        csv_field(hosts[i].hostname);      putchar(',');
        csv_field(hosts[i].hostname_src);  putchar(',');
        csv_field(hosts[i].mac);           putchar(',');
        printf("%s,", hosts[i].ok ? "true" : "false");
        printf("%ld,", hosts[i].rtt_ms);
        csv_field(hosts[i].err);           putchar('\n');
    }
}

/* ---------------------------------------------------------- one scan -- */

static int run_scan(struct host *hosts, int n, struct bping_cfg *cfg, int jobs,
                    long *out_elapsed)
{
    struct work w = { .hosts=hosts, .n=n, .cfg=cfg, .next=0,
                      .done=0, .total=n };
    pthread_mutex_init(&w.lock, NULL);

    if (jobs < 1) jobs = 1;
    if (jobs > n) jobs = n;
    pthread_t *th = calloc(jobs, sizeof(*th));
    long t0 = now_ms();
    for (int i = 0; i < jobs; i++) pthread_create(&th[i], NULL, worker, &w);
    for (int i = 0; i < jobs; i++) pthread_join(th[i], NULL);
    *out_elapsed = now_ms() - t0;
    if (!cfg->quiet) ui_progress_done();

    pthread_mutex_destroy(&w.lock);
    free(th);

    int fail = 0;
    for (int i = 0; i < n; i++) if (!hosts[i].ok) fail++;
    return fail;
}

int bping_main(int argc, char **argv)
{
    const char *file = NULL;
    const char *out_fmt = "table";       /* table | json | csv */
    int watch_sec = 0;                   /* 0 = run once */
    struct bping_cfg cfg = { .timeout_ms = 1500, .mode_icmp = 0,
                             .no_rdns = 0, .no_discover = 0, .port = "80" };
    int jobs = 32;

    static struct option opts[] = {
        {"file",1,0,'f'},{"mode",1,0,'m'},{"port",1,0,'p'},
        {"timeout",1,0,'t'},{"jobs",1,0,'j'},{"no-rdns",0,0,'n'},
        {"no-discover",0,0,'D'},
        {"output",1,0,'o'},{"watch",1,0,'W'},
        {"help",0,0,'h'},{0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "f:m:p:t:j:nDo:W:h", opts, NULL)) != -1) {
        switch (c) {
        case 'f': file = optarg; break;
        case 'm':
            if (strcmp(optarg, "icmp") == 0) cfg.mode_icmp = 1;
            else if (strcmp(optarg, "tcp") == 0) cfg.mode_icmp = 0;
            else { ui_err(T(T_E_BAD_MODE), optarg); return 1; }
            break;
        case 'p': cfg.port = optarg; break;
        case 't': cfg.timeout_ms = atoi(optarg); break;
        case 'j': jobs = atoi(optarg); break;
        case 'o':
            if (strcmp(optarg, "table") && strcmp(optarg, "json") &&
                strcmp(optarg, "csv")) {
                ui_err("bad output format: %s (table|json|csv)", optarg);
                return 1;
            }
            out_fmt = optarg; break;
        case 'W': watch_sec = atoi(optarg); break;
        case 'n': cfg.no_rdns = 1; break;
        case 'D': cfg.no_discover = 1; break;
        case 'h': bping_help(); return 0;
        default:  bping_help(); return 1;
        }
    }

    /* collect hosts (with range/CIDR expansion) */
    int cap = 64, n = 0;
    struct host *hosts = calloc(cap, sizeof(*hosts));
    int max_total = 65536;

    #define ADD_EXPR(expr) do { \
        char **list = NULL; \
        int m = host_range_expand((expr), &list, max_total); \
        if (m < 0) { ui_warn(T(T_E_BAD_RANGE), (expr)); break; } \
        for (int j = 0; j < m; j++) { \
            if (n >= cap) { cap *= 2; hosts = realloc(hosts, cap * sizeof(*hosts)); } \
            memset(&hosts[n], 0, sizeof(hosts[n])); \
            strncpy(hosts[n].name, list[j], sizeof(hosts[n].name) - 1); \
            n++; \
        } \
        host_list_free(list, m); \
    } while (0)

    if (file) {
        FILE *fp = fopen(file, "r");
        if (!fp) { ui_err(T(T_E_OPEN), file, strerror(errno)); free(hosts); return 1; }
        char buf[256];
        while (read_line_strip(buf, sizeof(buf), fp)) ADD_EXPR(buf);
        fclose(fp);
    }
    for (int i = optind; i < argc; i++) ADD_EXPR(argv[i]);
    #undef ADD_EXPR

    if (n == 0) { bping_help(); free(hosts); return 1; }

    int is_table = (strcmp(out_fmt, "table") == 0);
    if (!is_table) cfg.quiet = 1;

    if (is_table) {
        ui_section(ui_icon_rocket(), T(T_BATCH_PING));
        ui_kv(T(T_HOSTS),      "%d", n);
        ui_kv(T(T_MODE),       "%s%s%s", UI_BCYAN,
                                         cfg.mode_icmp ? "icmp" : "tcp", UI_RESET);
        if (!cfg.mode_icmp) ui_kv("port", "%s", cfg.port);
        ui_kv(T(T_TIMEOUT_MS), "%d ms", cfg.timeout_ms);
        ui_kv(T(T_JOBS),       "%d", jobs);
        if (watch_sec > 0) ui_kv("watch", "%d s", watch_sec);
    }

    install_sigint(&g_stop);

    int last_fail = 0, round = 0;
    do {
        /* reset per-host result fields between rounds */
        for (int i = 0; i < n; i++) {
            hosts[i].ok = 0; hosts[i].rtt_ms = 0;
            hosts[i].err[0] = 0;
            /* keep ip/hostname/mac discovered in earlier rounds */
        }

        long elapsed;
        last_fail = run_scan(hosts, n, &cfg, jobs, &elapsed);

        if (is_table) {
            if (watch_sec > 0 && round > 0)
                fputs("\033[2J\033[H", stdout);   /* clear screen on re-scan */
            emit_table(hosts, n, elapsed);
        } else if (strcmp(out_fmt, "json") == 0) {
            emit_json(hosts, n, elapsed);
        } else {
            emit_csv(hosts, n, elapsed);
        }
        fflush(stdout);

        round++;
        if (watch_sec <= 0 || g_stop) break;
        for (int s = 0; s < watch_sec && !g_stop; s++) sleep(1);
    } while (!g_stop);

    free(hosts);
    return last_fail == 0 ? 0 : 4;
}
