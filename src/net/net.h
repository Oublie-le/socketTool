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

/*
 * Expand a host expression to a list of IP/host strings.
 *
 * Supported forms:
 *   "192.168.1.10"             single IPv4
 *   "192.168.1.10-50"          last-octet range
 *   "192.168.1.10-192.168.1.50" full IPv4 range
 *   "10.0.0.0/24"              CIDR (caps at MAX hosts; default 4096)
 *   "host.example.com"         single hostname
 *
 * On success returns >= 0 = number of hosts; *out is a malloc'd array of
 * malloc'd strings (caller must free strings + array). On error returns -1.
 *
 * max_hosts: hard cap; if expansion would exceed, returns -1.
 */
int host_range_expand(const char *expr, char ***out, int max_hosts);

void host_list_free(char **list, int n);

/*
 * Native ICMP echo ping (IPv4 only, single probe).
 *
 * Resolves `host` (hostname or dotted IP), sends one ICMP echo with the
 * given identifier+sequence, waits up to timeout_ms for a matching reply.
 *
 * On success: returns RTT in milliseconds (>= 0) and copies the resolved
 *             dotted IP into resolved_ip[16].
 * On failure: returns -1; if errno == EACCES the caller can fall back to
 *             tcp-ping. err[errlen] receives a short human-readable reason.
 */
int icmp_ping_once(const char *host, int identifier, int sequence,
                   int timeout_ms,
                   char resolved_ip[16],
                   char *err, size_t errlen);

/*
 * Reverse DNS for an IPv4 dotted string. Writes hostname into
 * out[outlen] on success; returns 0 on success, -1 on failure (in which
 * case out is set to the original IP for convenience).
 */
int reverse_dns(const char *ip, char *out, size_t outlen);

#endif
