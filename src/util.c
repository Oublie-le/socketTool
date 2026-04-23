/*
 * util.c — generic helpers.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
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

#include "util.h"

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
