/*
 * icmp.c — native ICMPv4 echo ping (no shelling out to /bin/ping).
 *
 * Tries SOCK_DGRAM/IPPROTO_ICMP first (unprivileged "ping group" sockets;
 * Linux 3.0+, controlled by net.ipv4.ping_group_range). Falls back to
 * SOCK_RAW/IPPROTO_ICMP, which needs CAP_NET_RAW or root.
 *
 * For DGRAM sockets, the kernel rewrites the echo identifier so we cannot
 * pin it ourselves — but it also delivers only matching replies, so the
 * (id, seq) match check below still works because the kernel has already
 * filtered. For RAW sockets we must compare both fields manually.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#if defined(__APPLE__) && defined(__MACH__)
/* macOS / Darwin */
#include <netinet/ip.h>
#define IP_HL(ip) ((ip)->ip_hl)
typedef struct ip ip_hdr_t;
#else
/* Linux */
#include <netinet/ip.h>
#define IP_HL(ip) ((ip)->ihl)
typedef struct iphdr ip_hdr_t;
#endif

#include "net/net.h"
#include "net/icmp_compat.h"

static uint16_t ip_csum(const void *p, size_t n)
{
    const uint16_t *w = p;
    uint32_t sum = 0;
    while (n >= 2) { sum += *w++; n -= 2; }
    if (n) sum += *(const uint8_t *)w;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static long now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}

int reverse_dns(const char *ip, char *out, size_t outlen)
{
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) {
        snprintf(out, outlen, "%s", ip);
        return -1;
    }
    if (getnameinfo((struct sockaddr*)&sa, sizeof(sa),
                    out, outlen, NULL, 0, NI_NAMEREQD) != 0) {
        snprintf(out, outlen, "%s", ip);
        return -1;
    }
    return 0;
}

int icmp_ping_once(const char *host, int identifier, int sequence,
                   int timeout_ms,
                   char resolved_ip[16],
                   char *err, size_t errlen)
{
    /* resolve to first IPv4 */
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    int rc = getaddrinfo(host, NULL, &hints, &res);
    if (rc != 0 || !res) {
        if (err) snprintf(err, errlen, "resolve: %s", gai_strerror(rc));
        return -1;
    }
    struct sockaddr_in dst = *(struct sockaddr_in *)res->ai_addr;
    inet_ntop(AF_INET, &dst.sin_addr, resolved_ip, 16);
    freeaddrinfo(res);

    /* try DGRAM first (unprivileged), then RAW */
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    int dgram = (fd >= 0);
    if (fd < 0) {
        fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (fd < 0) {
            if (err) snprintf(err, errlen,
                "icmp socket: %s (need CAP_NET_RAW or "
                "ping_group_range)", strerror(errno));
            return -1;
        }
    }

    /* assemble packet: 8-byte ICMP header + 48 bytes payload (timestamp) */
    uint8_t pkt[64] = {0};
    icmp_hdr_t *hdr = (icmp_hdr_t *)pkt;
    hdr->ICMP_TYPE = ICMP_ECHO;
    hdr->ICMP_CODE = 0;
    hdr->ICMP_ID   = htons((uint16_t)identifier);
    hdr->ICMP_SEQ  = htons((uint16_t)sequence);

    long t_send = now_us();
    memcpy(pkt + sizeof(*hdr), &t_send, sizeof(t_send));

    hdr->ICMP_CKSUM = 0;
    hdr->ICMP_CKSUM = ip_csum(pkt, sizeof(pkt));

    if (sendto(fd, pkt, sizeof(pkt), 0,
               (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        if (err) snprintf(err, errlen, "send: %s", strerror(errno));
        close(fd); return -1;
    }

    /* wait for matching reply */
    long deadline_us = t_send + (long)timeout_ms * 1000;
    for (;;) {
        long now = now_us();
        if (now >= deadline_us) {
            if (err) snprintf(err, errlen, "timeout");
            close(fd); return -1;
        }
        long left = deadline_us - now;
        struct timeval tv = { left / 1000000, left % 1000000 };
        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd, &rfds);
        int r = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (r == 0) {
            if (err) snprintf(err, errlen, "timeout");
            close(fd); return -1;
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            if (err) snprintf(err, errlen, "select: %s", strerror(errno));
            close(fd); return -1;
        }

        uint8_t buf[1500];
        struct sockaddr_in from; socklen_t fl = sizeof(from);
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&from, &fl);
        if (n <= 0) continue;

        icmp_hdr_t *rh;
        if (dgram) {
            /* DGRAM: kernel strips IP header; payload starts at buf */
            if (n < (ssize_t)sizeof(*rh)) continue;
            rh = (icmp_hdr_t *)buf;
        } else {
            /* RAW: includes IP header */
            if (n < (ssize_t)(sizeof(ip_hdr_t) + sizeof(*rh))) continue;
            ip_hdr_t *iph = (ip_hdr_t *)buf;
            rh = (icmp_hdr_t *)(buf + IP_HL(iph) * 4);
        }
        if (rh->ICMP_TYPE != ICMP_ECHOREPLY) continue;
        if (!dgram && ntohs(rh->ICMP_ID) != identifier) continue;
        if (ntohs(rh->ICMP_SEQ) != sequence) continue;

        long rtt_us = now_us() - t_send;
        close(fd);
        return (int)((rtt_us + 500) / 1000);   /* ms, rounded */
    }
}
