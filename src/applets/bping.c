/*
 * bping.c — batch ping applet.
 *
 * Two ping modes:
 *   - tcp  (default): TCP connect probe to a port (no privileges required)
 *   - icmp:           shells out to /bin/ping (-c1 -W1)
 *
 * Hosts come from -f <file> (one per line, '#' comments allowed) and/or
 * trailing positional args. Concurrency via pthreads.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/wait.h>

#include "core/applet.h"
#include "ui/ui.h"
#include "net/net.h"
#include "i18n/i18n.h"

struct host {
    char       name[128];
    int        ok;
    long       rtt_ms;
    char       err[64];
};

struct bping_cfg {
    int        timeout_ms;
    int        mode_icmp;
    const char *port;
};

struct work {
    struct host        *hosts;
    int                 n;
    struct bping_cfg   *cfg;
    pthread_mutex_t     lock;
    int                 next;
};

static int icmp_probe(const char *host, int timeout_ms)
{
    char cmd[256];
    int s = (timeout_ms + 999) / 1000; if (s < 1) s = 1;
    snprintf(cmd, sizeof(cmd),
             "ping -c 1 -W %d %s >/dev/null 2>&1", s, host);
    int rc = system(cmd);
    return WIFEXITED(rc) && WEXITSTATUS(rc) == 0 ? 0 : -1;
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
        long t0 = now_ms();
        if (w->cfg->mode_icmp) {
            h->ok = icmp_probe(h->name, w->cfg->timeout_ms) == 0;
            if (!h->ok) snprintf(h->err, sizeof(h->err), "no reply");
        } else {
            int fd = tcp_connect(h->name, w->cfg->port, w->cfg->timeout_ms);
            if (fd >= 0) { h->ok = 1; close(fd); }
            else { h->ok = 0; snprintf(h->err, sizeof(h->err), "%s", strerror(errno)); }
        }
        h->rtt_ms = now_ms() - t0;
    }
    return NULL;
}

static void bping_help(void)
{
    printf("Usage: bping [-f hosts.txt] [host ...] [options]\n"
           "  -f, --file FILE       read hosts from FILE (one per line, # comments)\n"
           "  -m, --mode MODE       'tcp' (default) or 'icmp'\n"
           "  -p, --port PORT       port for tcp mode (default 80)\n"
           "  -t, --timeout MS      per-host timeout (default 1500)\n"
           "  -j, --jobs N          parallel workers (default 16)\n"
           "  -h, --help            show this help\n");
}

int bping_main(int argc, char **argv)
{
    const char *file = NULL;
    struct bping_cfg cfg = { .timeout_ms = 1500, .mode_icmp = 0, .port = "80" };
    int jobs = 16;

    static struct option opts[] = {
        {"file",1,0,'f'},{"mode",1,0,'m'},{"port",1,0,'p'},
        {"timeout",1,0,'t'},{"jobs",1,0,'j'},{"help",0,0,'h'},{0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "f:m:p:t:j:h", opts, NULL)) != -1) {
        switch (c) {
        case 'f': file = optarg; break;
        case 'm':
            if (strcmp(optarg, "icmp") == 0) cfg.mode_icmp = 1;
            else if (strcmp(optarg, "tcp") == 0) cfg.mode_icmp = 0;
            else { ui_err("bad mode: %s", optarg); return 1; }
            break;
        case 'p': cfg.port = optarg; break;
        case 't': cfg.timeout_ms = atoi(optarg); break;
        case 'j': jobs = atoi(optarg); break;
        case 'h': bping_help(); return 0;
        default:  bping_help(); return 1;
        }
    }

    /* collect hosts */
    int cap = 64, n = 0;
    struct host *hosts = calloc(cap, sizeof(*hosts));
    if (file) {
        FILE *fp = fopen(file, "r");
        if (!fp) { ui_err("open %s: %s", file, strerror(errno)); free(hosts); return 1; }
        char buf[256];
        while (read_line_strip(buf, sizeof(buf), fp)) {
            if (n >= cap) { cap *= 2; hosts = realloc(hosts, cap * sizeof(*hosts)); }
            memset(&hosts[n], 0, sizeof(hosts[n]));
            snprintf(hosts[n].name, sizeof(hosts[n].name), "%s", buf);
            n++;
        }
        fclose(fp);
    }
    for (int i = optind; i < argc; i++) {
        if (n >= cap) { cap *= 2; hosts = realloc(hosts, cap * sizeof(*hosts)); }
        memset(&hosts[n], 0, sizeof(hosts[n]));
        strncpy(hosts[n].name, argv[i], sizeof(hosts[n].name) - 1);
        n++;
    }
    if (n == 0) { bping_help(); free(hosts); return 1; }

    ui_section(ui_icon_rocket(), T(T_BATCH_PING));
    ui_kv(T(T_HOSTS),      "%d", n);
    ui_kv(T(T_MODE),       "%s%s%s", UI_BCYAN, cfg.mode_icmp ? "icmp" : "tcp",
          UI_RESET);
    if (!cfg.mode_icmp) ui_kv("port", "%s", cfg.port);
    ui_kv(T(T_TIMEOUT_MS), "%d ms", cfg.timeout_ms);
    ui_kv(T(T_JOBS),       "%d", jobs);

    struct work w = { .hosts=hosts, .n=n, .cfg=&cfg, .next=0 };
    pthread_mutex_init(&w.lock, NULL);

    if (jobs < 1) jobs = 1;
    if (jobs > n) jobs = n;
    pthread_t *th = calloc(jobs, sizeof(*th));
    long t0 = now_ms();
    for (int i = 0; i < jobs; i++) pthread_create(&th[i], NULL, worker, &w);
    for (int i = 0; i < jobs; i++) pthread_join(th[i], NULL);
    long elapsed = now_ms() - t0;

    /* output table */
    const char *cols[]   = { T(T_HOST), T(T_RESULT), T(T_RTT), T(T_INFO) };
    const int   widths[] = { 28,     6,        8,         24 };
    putchar('\n');
    ui_table_header(cols, widths, 4);

    int ok = 0, fail = 0;
    for (int i = 0; i < n; i++) {
        char rttbuf[16]; snprintf(rttbuf, sizeof(rttbuf), "%ld", hosts[i].rtt_ms);
        const char *cells[] = {
            hosts[i].name,
            hosts[i].ok ? "OK" : "FAIL",
            rttbuf,
            hosts[i].ok ? "" : hosts[i].err,
        };
        ui_table_row(cells, widths, 4, hosts[i].ok ? UI_GREEN : UI_RED);
        hosts[i].ok ? ok++ : fail++;
    }
    ui_table_sep(widths, 4);

    putchar('\n');
    ui_kv(T(T_OK_LBL),   "%s%s %d%s", UI_BGREEN, ui_icon_ok(), ok, UI_RESET);
    ui_kv(T(T_FAIL_LBL), "%s%s %d%s", fail?UI_BRED:UI_BGREEN,
          fail?ui_icon_fail():ui_icon_ok(), fail, UI_RESET);
    ui_kv(T(T_ELAPSED),  "%ld ms", elapsed);

    pthread_mutex_destroy(&w.lock);
    free(th); free(hosts);
    return fail == 0 ? 0 : 4;
}
