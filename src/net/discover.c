/*
 * discover.c — LAN host identity discovery.
 *
 * arp_lookup    : read /proc/net/arp
 * nbns_lookup   : NetBIOS Name Service node status query (UDP 137)
 * mdns_lookup   : Multicast DNS reverse PTR (UDP 5353, unicast to host)
 *
 * All three return 0 on success and copy a printable name/MAC into out.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "net/discover.h"

/* ------------------------------------------------------------------ ARP */

int arp_lookup(const char *ip, char *out, size_t outlen)
{
    FILE *fp = fopen("/proc/net/arp", "r");
    if (!fp) return -1;

    char line[256];
    /* skip header */
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return -1; }

    char addr[64], hw_type[16], flags[16], mac[32];
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%63s %15s %15s %31s",
                   addr, hw_type, flags, mac) != 4) continue;
        if (strcmp(addr, ip) != 0) continue;
        if (strcmp(mac, "00:00:00:00:00:00") == 0) continue;
        snprintf(out, outlen, "%s", mac);
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return -1;
}

/* ----------------------------------------------------------- NBNS (137) */

/*
 * NetBIOS node-status request:
 *   12-byte DNS-like header + question for "*<padded to 16, 0x00 trailer>"
 *   encoded as "first-level" name (32 bytes) + qtype=NBSTAT(0x21) qclass=IN
 *
 * Reply payload: 1 byte name count, then for each name: 15 chars + suffix +
 *                2 bytes flags. We pick the first name that does not have
 *                the GROUP bit (0x80) set in flags.
 */
static void nb_encode_wildcard(uint8_t out[34])
{
    /* "*<NUL>...<NUL>" 16 bytes -> first-level encoded as 32 nibble pairs */
    static const uint8_t nb[16] = {
        '*',0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
    };
    out[0] = 32;
    for (int i = 0; i < 16; i++) {
        out[1 + i*2]     = 'A' + ((nb[i] >> 4) & 0xf);
        out[1 + i*2 + 1] = 'A' + (nb[i] & 0xf);
    }
    out[33] = 0;   /* root label */
}

int nbns_lookup(const char *ip, int timeout_ms, char *out, size_t outlen)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(137);
    if (inet_pton(AF_INET, ip, &dst.sin_addr) != 1) { close(fd); return -1; }

    uint8_t pkt[50] = {0};
    /* transaction id = pid lo */
    uint16_t txid = (uint16_t)(getpid() & 0xffff);
    pkt[0] = txid >> 8; pkt[1] = txid & 0xff;
    /* flags = 0 (standard query, broadcast bit OK to leave 0 for unicast) */
    pkt[4] = 0; pkt[5] = 1;            /* qdcount = 1 */
    nb_encode_wildcard(pkt + 12);
    /* qtype NBSTAT (0x0021), qclass IN (0x0001) */
    pkt[12 + 34 + 0] = 0x00; pkt[12 + 34 + 1] = 0x21;
    pkt[12 + 34 + 2] = 0x00; pkt[12 + 34 + 3] = 0x01;

    if (sendto(fd, pkt, 12 + 34 + 4, 0,
               (struct sockaddr*)&dst, sizeof(dst)) < 0) {
        close(fd); return -1;
    }

    uint8_t buf[1500];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    close(fd);
    if (n < 60) return -1;
    if (buf[0] != pkt[0] || buf[1] != pkt[1]) return -1;
    if ((buf[2] & 0x80) == 0) return -1;       /* must be a response */

    /* skip header (12) + question name (34) + qtype/class (4) +
       answer name (34) + type/class/ttl (8) + rdlength (2) = 94 */
    int off = 94;
    if (n < off + 1) return -1;
    int count = buf[off++];
    for (int i = 0; i < count && off + 18 <= n; i++) {
        uint8_t *name  = buf + off;          /* 15 bytes printable */
        uint8_t suffix = buf[off + 15];
        uint16_t flags = (buf[off + 16] << 8) | buf[off + 17];
        off += 18;

        /* skip group entries; we want a unique workstation name */
        if (flags & 0x8000) continue;
        /* trim trailing spaces */
        char tmp[20] = {0};
        memcpy(tmp, name, 15);
        for (int k = 14; k >= 0 && (tmp[k] == ' ' || tmp[k] == 0); k--)
            tmp[k] = '\0';
        if (!tmp[0]) continue;
        /* prefer suffix 0x00 (workstation) and 0x20 (file server) */
        if (suffix == 0x00 || suffix == 0x20) {
            snprintf(out, outlen, "%s", tmp);
            return 0;
        }
        /* fallback: first usable */
        if (i == 0) snprintf(out, outlen, "%s", tmp);
    }
    return out[0] ? 0 : -1;
}

/* ------------------------------------------------------------ mDNS PTR */

/*
 * Encode "<a>.<b>.<c>.<d>.in-addr.arpa." as DNS labels into out, returns
 * length consumed.
 */
static int dns_encode_revname(const char *ip, uint8_t *out, size_t cap)
{
    unsigned a, b, c, d;
    if (sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return -1;
    char name[80];
    int nl = snprintf(name, sizeof(name),
                      "%u.%u.%u.%u.in-addr.arpa", d, c, b, a);
    if (nl < 0 || (size_t)nl >= sizeof(name)) return -1;

    size_t o = 0;
    char *p = name;
    while (*p) {
        char *dot = strchr(p, '.');
        size_t len = dot ? (size_t)(dot - p) : strlen(p);
        if (len > 63 || o + 1 + len + 1 > cap) return -1;
        out[o++] = (uint8_t)len;
        memcpy(out + o, p, len); o += len;
        if (!dot) break;
        p = dot + 1;
    }
    out[o++] = 0;
    return (int)o;
}

int mdns_lookup(const char *ip, int timeout_ms, char *out, size_t outlen)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(5353);
    if (inet_pton(AF_INET, ip, &dst.sin_addr) != 1) { close(fd); return -1; }

    uint8_t pkt[256] = {0};
    pkt[0] = 0; pkt[1] = 0;            /* txid 0, per RFC 6762 */
    pkt[4] = 0; pkt[5] = 1;            /* qdcount = 1 */
    int nlen = dns_encode_revname(ip, pkt + 12, sizeof(pkt) - 12);
    if (nlen < 0) { close(fd); return -1; }
    int off = 12 + nlen;
    /* qtype PTR (12), qclass IN (1) with QU bit set (0x8000) for unicast reply */
    pkt[off++] = 0x00; pkt[off++] = 0x0c;
    pkt[off++] = 0x80; pkt[off++] = 0x01;

    if (sendto(fd, pkt, off, 0, (struct sockaddr*)&dst, sizeof(dst)) < 0) {
        close(fd); return -1;
    }

    uint8_t buf[1500];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    close(fd);
    if (n < 12) return -1;
    if ((buf[2] & 0x80) == 0) return -1;
    int ancount = (buf[6] << 8) | buf[7];
    if (ancount == 0) return -1;

    /* skip question */
    int p = 12;
    while (p < n && buf[p] != 0) {
        if ((buf[p] & 0xc0) == 0xc0) { p += 2; goto q_done; }
        p += buf[p] + 1;
    }
    p++;        /* zero label */
q_done:
    p += 4;     /* qtype + qclass */

    /* first answer: skip name, type, class, ttl, rdlength -> rdata */
    /* name */
    while (p < n && buf[p] != 0) {
        if ((buf[p] & 0xc0) == 0xc0) { p += 2; goto a_done; }
        p += buf[p] + 1;
    }
    p++;
a_done:
    if (p + 10 > n) return -1;
    uint16_t atype = (buf[p] << 8) | buf[p+1]; p += 2;
    p += 6;  /* class + ttl */
    uint16_t rdlen = (buf[p] << 8) | buf[p+1]; p += 2;
    if (atype != 12 || p + rdlen > n) return -1;

    /* decode PTR rdata as dotted hostname (handle compression once) */
    char name[256] = {0};
    size_t no = 0;
    int rp = p;
    int hops = 0;
    while (hops < 5 && rp < n) {
        uint8_t L = buf[rp];
        if (L == 0) break;
        if ((L & 0xc0) == 0xc0) {
            if (rp + 1 >= n) return -1;
            rp = ((L & 0x3f) << 8) | buf[rp + 1];
            hops++;
            continue;
        }
        rp++;
        if (rp + L > n || no + L + 1 >= sizeof(name)) return -1;
        if (no) name[no++] = '.';
        memcpy(name + no, buf + rp, L); no += L;
        rp += L;
    }
    if (!no) return -1;
    name[no] = '\0';
    snprintf(out, outlen, "%s", name);
    return 0;
}
