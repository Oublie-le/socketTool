/*
 * util.h — generic helpers (net, files, signals).
 */
#ifndef SOCKETTOOL_UTIL_H
#define SOCKETTOOL_UTIL_H

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

/* basename without modifying input */
const char *base_name(const char *path);

/* net helpers — returns -1 on error */
int net_resolve(const char *host, const char *port,
                int socktype, struct sockaddr_storage *out, socklen_t *outlen);

int tcp_connect(const char *host, const char *port, int timeout_ms);
int tcp_listen(const char *host, const char *port, int backlog);
int udp_socket(const char *host, const char *port, int bind_it);

int set_nonblock(int fd, int on);
int set_recv_timeout(int fd, int ms);
int set_send_timeout(int fd, int ms);

/* read a line from file, strips trailing whitespace + skips '#' comments */
char *read_line_strip(char *buf, size_t n, FILE *fp);

/* monotonic ms */
long now_ms(void);

/* install a SIGINT handler that flips *flag to 1 */
void install_sigint(volatile int *flag);

#endif
