/*
 * util.c — generic helpers.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "net/net.h"

const char *base_name(const char *path)
{
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

int net_resolve(const char *host, const char *port,
                int socktype, struct sockaddr_storage *out, socklen_t *outlen)
{
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = socktype;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) return -1;

    memcpy(out, res->ai_addr, res->ai_addrlen);
    *outlen = res->ai_addrlen;
    freeaddrinfo(res);
    return 0;
}

int set_nonblock(int fd, int on)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -1;
    if (on) fl |= O_NONBLOCK; else fl &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, fl);
}

int set_recv_timeout(int fd, int ms)
{
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

int set_send_timeout(int fd, int ms)
{
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

int tcp_connect(const char *host, const char *port, int timeout_ms)
{
    struct addrinfo hints = {0}, *res = NULL, *rp;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;

    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        if (timeout_ms > 0) set_nonblock(fd, 1);

        int r = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (r == 0) goto done;
        if (r < 0 && errno != EINPROGRESS) { close(fd); fd = -1; continue; }

        fd_set wfds;
        FD_ZERO(&wfds); FD_SET(fd, &wfds);
        struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        r = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (r > 0) {
            int err = 0; socklen_t el = sizeof(err);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) == 0 && err == 0)
                goto done;
        }
        close(fd); fd = -1;
    }
done:
    if (res) freeaddrinfo(res);
    if (fd >= 0 && timeout_ms > 0) set_nonblock(fd, 0);
    return fd;
}

int tcp_listen(const char *host, const char *port, int backlog)
{
    struct addrinfo hints = {0}, *res = NULL, *rp;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    if (getaddrinfo(host && *host ? host : NULL, port, &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0 &&
            listen(fd, backlog) == 0)
            break;
        close(fd); fd = -1;
    }
    if (res) freeaddrinfo(res);
    return fd;
}

int udp_socket(const char *host, const char *port, int bind_it)
{
    struct addrinfo hints = {0}, *res = NULL, *rp;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (bind_it) hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(host && *host ? host : NULL, port, &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (bind_it) {
            int yes = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
            if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
            close(fd); fd = -1;
        } else {
            if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
            close(fd); fd = -1;
        }
    }
    if (res) freeaddrinfo(res);
    return fd;
}

char *read_line_strip(char *buf, size_t n, FILE *fp)
{
    while (fgets(buf, n, fp)) {
        char *s = buf;
        while (*s && isspace((unsigned char)*s)) s++;
        if (*s == '#' || *s == '\0') continue;
        char *e = s + strlen(s);
        while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
        if (s != buf) memmove(buf, s, e - s + 1);
        return buf;
    }
    return NULL;
}

long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static volatile int *g_int_flag;
static void on_sigint(int s) { (void)s; if (g_int_flag) *g_int_flag = 1; }

void install_sigint(volatile int *flag)
{
    g_int_flag = flag;
    struct sigaction sa = {0};
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* ----------------------------------------------- host range expansion --- */

static int parse_ipv4(const char *s, uint32_t *out)
{
    unsigned a, b, c, d; char x;
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &x) != 4) return -1;
    if (a > 255 || b > 255 || c > 255 || d > 255) return -1;
    *out = (a << 24) | (b << 16) | (c << 8) | d;
    return 0;
}

static char *ip_to_str(uint32_t ip)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             (ip >> 24) & 0xff, (ip >> 16) & 0xff,
             (ip >> 8) & 0xff, ip & 0xff);
    return strdup(buf);
}

static int append_host(char ***out, int *n, int *cap, char *s)
{
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 16;
        char **t = realloc(*out, *cap * sizeof(char *));
        if (!t) { free(s); return -1; }
        *out = t;
    }
    (*out)[(*n)++] = s;
    return 0;
}

void host_list_free(char **list, int n)
{
    if (!list) return;
    for (int i = 0; i < n; i++) free(list[i]);
    free(list);
}

int host_range_expand(const char *expr, char ***out_list, int max_hosts)
{
    *out_list = NULL;
    int n = 0, cap = 0;

    /* CIDR ? */
    const char *slash = strchr(expr, '/');
    if (slash) {
        char ip_part[32];
        size_t k = (size_t)(slash - expr);
        if (k >= sizeof(ip_part)) return -1;
        memcpy(ip_part, expr, k); ip_part[k] = '\0';
        int prefix = atoi(slash + 1);
        if (prefix < 0 || prefix > 32) return -1;

        uint32_t base;
        if (parse_ipv4(ip_part, &base) < 0) return -1;
        uint32_t mask  = prefix == 0 ? 0 : (~0u << (32 - prefix));
        uint32_t net   = base & mask;
        uint32_t bcast = net | ~mask;

        /* /31 and /32 cover all addrs; otherwise skip net + bcast */
        uint32_t lo = net, hi = bcast;
        if (prefix < 31) { lo = net + 1; hi = bcast - 1; }
        if (hi < lo) return -1;

        uint64_t total = (uint64_t)hi - lo + 1;
        if (total > (uint64_t)max_hosts) return -1;

        for (uint32_t ip = lo; ; ip++) {
            if (append_host(out_list, &n, &cap, ip_to_str(ip)) < 0)
                goto fail;
            if (ip == hi) break;
        }
        return n;
    }

    /* range with '-' ? */
    const char *dash = strchr(expr, '-');
    if (dash) {
        char left[64], right[64];
        size_t lk = (size_t)(dash - expr);
        if (lk >= sizeof(left)) return -1;
        memcpy(left, expr, lk); left[lk] = '\0';
        snprintf(right, sizeof(right), "%s", dash + 1);

        uint32_t a;
        if (parse_ipv4(left, &a) < 0) return -1;

        uint32_t b;
        if (strchr(right, '.')) {
            if (parse_ipv4(right, &b) < 0) return -1;
        } else {
            char *end; long last = strtol(right, &end, 10);
            if (*end != '\0' || last < 0 || last > 255) return -1;
            b = (a & 0xffffff00) | (uint32_t)last;
        }
        if (b < a) return -1;
        if ((b - a + 1) > (uint64_t)max_hosts) return -1;

        for (uint32_t ip = a; ; ip++) {
            if (append_host(out_list, &n, &cap, ip_to_str(ip)) < 0) goto fail;
            if (ip == b) break;
        }
        return n;
    }

    /* single host or hostname */
    if (append_host(out_list, &n, &cap, strdup(expr)) < 0) goto fail;
    return n;

fail:
    host_list_free(*out_list, n);
    *out_list = NULL;
    return -1;
}
