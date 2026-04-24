/*
 * mqtt.c — minimal MQTT 3.1.1 client + broker (QoS 0 only, no auth, no TLS).
 *
 * Client:
 *   mqtt-client -H host -p port -t topic -m payload         # publish once
 *   mqtt-client -H host -p port -t topic -s [-c N]          # subscribe
 *
 * Server (broker):
 *   mqtt-server -p port
 *     Multi-client (pthread). Topic matching is literal or supports '#'
 *     (multi-level wildcard at tail) and '+' (single level).
 *
 * Wire format is the fixed-header form:
 *   Byte 1: packet type | flags
 *   Byte 2+: Remaining Length (variable byte integer, up to 4 bytes)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
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
#include "i18n/i18n.h"

static volatile int g_stop;

/* MQTT packet types (upper nibble) */
#define MQTT_CONNECT      0x10
#define MQTT_CONNACK      0x20
#define MQTT_PUBLISH      0x30
#define MQTT_SUBSCRIBE    0x80
#define MQTT_SUBACK       0x90
#define MQTT_UNSUBSCRIBE  0xA0
#define MQTT_UNSUBACK     0xB0
#define MQTT_PINGREQ      0xC0
#define MQTT_PINGRESP     0xD0
#define MQTT_DISCONNECT   0xE0

/* ---- varint & helpers ---------------------------------------------------- */

static int write_varint(uint8_t *b, uint32_t v)
{
    int n = 0;
    do {
        uint8_t d = v % 128; v /= 128;
        if (v) d |= 0x80;
        b[n++] = d;
    } while (v);
    return n;
}

static int read_varint(int fd, uint32_t *out)
{
    uint32_t v = 0, mult = 1; int n = 0; uint8_t b;
    for (;;) {
        if (recv(fd, &b, 1, MSG_WAITALL) != 1) return -1;
        v += (uint32_t)(b & 0x7f) * mult;
        n++;
        if (!(b & 0x80)) break;
        mult *= 128;
        if (n > 4) return -1;
    }
    *out = v; return n;
}

static void put_u16(uint8_t *b, uint16_t v) { b[0] = v >> 8; b[1] = v & 0xff; }
static uint16_t get_u16(const uint8_t *b) { return (b[0] << 8) | b[1]; }

static int write_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    while (len) {
        ssize_t w = send(fd, p, len, 0);
        if (w <= 0) return -1;
        p += w; len -= w;
    }
    return 0;
}

static int read_all(int fd, void *buf, size_t len)
{
    uint8_t *p = buf;
    while (len) {
        ssize_t r = recv(fd, p, len, MSG_WAITALL);
        if (r <= 0) return -1;
        p += r; len -= r;
    }
    return 0;
}

/* ---- CONNECT / PUB / SUB packet builders --------------------------------- */

static int send_connect(int fd, const char *client_id, int keepalive)
{
    uint8_t body[256]; int o = 0;
    /* Protocol name */
    put_u16(body + o, 4); o += 2; memcpy(body + o, "MQTT", 4); o += 4;
    body[o++] = 4;                        /* Protocol level (3.1.1) */
    body[o++] = 0x02;                     /* Clean session */
    body[o++] = keepalive >> 8; body[o++] = keepalive & 0xff;

    size_t id_len = strlen(client_id);
    put_u16(body + o, id_len); o += 2;
    memcpy(body + o, client_id, id_len); o += id_len;

    uint8_t pkt[300]; int n = 0;
    pkt[n++] = MQTT_CONNECT;
    n += write_varint(pkt + n, o);
    memcpy(pkt + n, body, o); n += o;
    return write_all(fd, pkt, n);
}

static int send_publish(int fd, const char *topic, const void *payload, size_t plen)
{
    size_t tlen = strlen(topic);
    size_t body_len = 2 + tlen + plen;
    uint8_t *pkt = malloc(5 + body_len); if (!pkt) return -1;
    int n = 0;
    pkt[n++] = MQTT_PUBLISH;                 /* QoS 0, no DUP, no RETAIN */
    n += write_varint(pkt + n, body_len);
    put_u16(pkt + n, tlen); n += 2;
    memcpy(pkt + n, topic, tlen); n += tlen;
    memcpy(pkt + n, payload, plen); n += plen;
    int rc = write_all(fd, pkt, n);
    free(pkt);
    return rc;
}

static int send_subscribe(int fd, const char *topic, uint16_t pid)
{
    size_t tlen = strlen(topic);
    size_t body_len = 2 + 2 + tlen + 1;
    uint8_t pkt[300]; int n = 0;
    pkt[n++] = MQTT_SUBSCRIBE | 0x02;
    n += write_varint(pkt + n, body_len);
    put_u16(pkt + n, pid); n += 2;
    put_u16(pkt + n, tlen); n += 2;
    memcpy(pkt + n, topic, tlen); n += tlen;
    pkt[n++] = 0;                            /* requested QoS 0 */
    return write_all(fd, pkt, n);
}

static int send_simple(int fd, uint8_t type)
{
    uint8_t p[2] = { type, 0 };
    return write_all(fd, p, 2);
}

/* ---- read one packet: header byte + body into malloc'd buf --------------- */

struct pkt {
    uint8_t  type;           /* upper nibble only */
    uint8_t  flags;          /* lower nibble */
    uint32_t len;
    uint8_t *data;           /* malloc'd */
};

static void pkt_free(struct pkt *p) { free(p->data); p->data = NULL; }

static int read_pkt(int fd, struct pkt *out)
{
    uint8_t hdr;
    if (read_all(fd, &hdr, 1) < 0) return -1;
    uint32_t len;
    if (read_varint(fd, &len) < 0) return -1;
    out->type  = hdr & 0xf0;
    out->flags = hdr & 0x0f;
    out->len   = len;
    out->data  = len ? malloc(len) : NULL;
    if (len && !out->data) return -1;
    if (len && read_all(fd, out->data, len) < 0) { free(out->data); return -1; }
    return 0;
}

/* ---- topic match: supports '+' (single level) and '#' (multi level tail) - */

static int topic_match(const char *pat, const char *topic)
{
    if (strcmp(pat, "#") == 0) return 1;
    while (*pat && *topic) {
        if (*pat == '#') return 1;
        if (*pat == '+') {
            while (*topic && *topic != '/') topic++;
            pat++;
            if (*pat == '/' && *topic == '/') { pat++; topic++; }
            continue;
        }
        if (*pat != *topic) return 0;
        pat++; topic++;
    }
    return *pat == 0 && *topic == 0;
}

/* ========================================================================== */
/*                               CLIENT                                       */
/* ========================================================================== */

static void mqtt_client_help(void)
{
    printf("Usage: mqtt-client -H host -p port -t topic (-m payload | -s) [options]\n"
           "  -H, --host HOST       broker host (required)\n"
           "  -p, --port PORT       broker port (default 1883)\n"
           "  -t, --topic TOPIC     topic (required)\n"
           "  -m, --message BODY    publish BODY once\n"
           "  -s, --subscribe       subscribe to TOPIC and print messages\n"
           "  -c, --count N         receive N messages then exit (default: unlimited)\n"
           "  -i, --id ID           MQTT client id (default: socketTool-<pid>)\n"
           "  -k, --keepalive SECS  keepalive interval (default 60)\n"
           "  -h, --help            show this help\n");
}

int mqtt_client_main(int argc, char **argv)
{
    const char *host = NULL, *port = "1883", *topic = NULL;
    const char *msg = NULL, *client_id = NULL;
    int subscribe = 0, count = -1, keepalive = 60;
    char id_buf[64];

    static struct option opts[] = {
        {"host",1,0,'H'},{"port",1,0,'p'},{"topic",1,0,'t'},
        {"message",1,0,'m'},{"subscribe",0,0,'s'},{"count",1,0,'c'},
        {"id",1,0,'i'},{"keepalive",1,0,'k'},
        {"help",0,0,'h'},{0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "H:p:t:m:sc:i:k:h", opts, NULL)) != -1) {
        switch (c) {
        case 'H': host = optarg; break;
        case 'p': port = optarg; break;
        case 't': topic = optarg; break;
        case 'm': msg = optarg; break;
        case 's': subscribe = 1; break;
        case 'c': count = atoi(optarg); break;
        case 'i': client_id = optarg; break;
        case 'k': keepalive = atoi(optarg); break;
        case 'h': mqtt_client_help(); return 0;
        default:  mqtt_client_help(); return 1;
        }
    }
    if (!host || !topic || (!msg && !subscribe)) { mqtt_client_help(); return 1; }

    alias_apply_host(&host, &port);
    install_sigint(&g_stop);

    if (!client_id) {
        snprintf(id_buf, sizeof(id_buf), "socketTool-%d", (int)getpid());
        client_id = id_buf;
    }

    ui_section(ui_icon_globe(), "MQTT client");
    ui_kv(T(T_TARGET), "%s%s:%s%s", UI_BCYAN, host, port, UI_RESET);
    ui_kv("topic",     "%s", topic);
    ui_kv("client-id", "%s", client_id);

    int fd = tcp_connect(host, port, 5000);
    if (fd < 0) { ui_err("connect: %s", strerror(errno)); return 2; }

    if (send_connect(fd, client_id, keepalive) < 0) {
        ui_err("send CONNECT failed"); close(fd); return 3;
    }

    struct pkt p = {0};
    if (read_pkt(fd, &p) < 0 || p.type != MQTT_CONNACK) {
        ui_err("no/bad CONNACK"); pkt_free(&p); close(fd); return 3;
    }
    if (p.len >= 2 && p.data[1] != 0) {
        ui_err("CONNECT refused, return code %d", p.data[1]);
        pkt_free(&p); close(fd); return 3;
    }
    pkt_free(&p);
    ui_ok("CONNACK ok");

    if (msg) {
        if (send_publish(fd, topic, msg, strlen(msg)) < 0) {
            ui_err("publish: %s", strerror(errno)); close(fd); return 3;
        }
        ui_ok("published %zu bytes to %s", strlen(msg), topic);
        send_simple(fd, MQTT_DISCONNECT);
        close(fd);
        return 0;
    }

    /* subscribe */
    if (send_subscribe(fd, topic, 1) < 0) {
        ui_err("subscribe: %s", strerror(errno)); close(fd); return 3;
    }
    if (read_pkt(fd, &p) < 0 || p.type != MQTT_SUBACK) {
        ui_err("no/bad SUBACK"); pkt_free(&p); close(fd); return 3;
    }
    pkt_free(&p);
    ui_ok("subscribed, waiting for messages (Ctrl-C to stop)");

    int got = 0;
    while (!g_stop) {
        if (read_pkt(fd, &p) < 0) break;
        if (p.type == MQTT_PUBLISH) {
            if (p.len < 2) { pkt_free(&p); continue; }
            uint16_t tlen = get_u16(p.data);
            if ((uint32_t)(2 + tlen) > p.len) { pkt_free(&p); continue; }
            int has_pid = (p.flags & 0x06) != 0;
            size_t off = 2 + tlen + (has_pid ? 2 : 0);
            if (off > p.len) { pkt_free(&p); continue; }
            printf("%s%s%s [%.*s] %.*s\n",
                   UI_BMAGENTA, ui_icon_recv(), UI_RESET,
                   (int)tlen, p.data + 2,
                   (int)(p.len - off), p.data + off);
            got++;
            if (count > 0 && got >= count) { pkt_free(&p); break; }
        }
        pkt_free(&p);
    }
    send_simple(fd, MQTT_DISCONNECT);
    close(fd);
    return 0;
}

/* ========================================================================== */
/*                               SERVER (broker)                              */
/* ========================================================================== */

#define MAX_CLIENTS     64
#define MAX_SUBS_PER    16

struct client {
    int      fd;
    int      in_use;
    char     id[64];
    char     subs[MAX_SUBS_PER][128];
    int      nsubs;
};

static struct client g_clients[MAX_CLIENTS];
static pthread_mutex_t g_clients_lock = PTHREAD_MUTEX_INITIALIZER;

static int client_register(int fd, const char *id)
{
    pthread_mutex_lock(&g_clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_clients[i].in_use) {
            g_clients[i].in_use = 1;
            g_clients[i].fd = fd;
            snprintf(g_clients[i].id, sizeof(g_clients[i].id), "%s", id);
            g_clients[i].nsubs = 0;
            pthread_mutex_unlock(&g_clients_lock);
            return i;
        }
    }
    pthread_mutex_unlock(&g_clients_lock);
    return -1;
}

static void client_unregister(int slot)
{
    pthread_mutex_lock(&g_clients_lock);
    if (slot >= 0 && slot < MAX_CLIENTS) {
        g_clients[slot].in_use = 0;
        g_clients[slot].fd = -1;
    }
    pthread_mutex_unlock(&g_clients_lock);
}

static void broker_distribute(int from_slot, const char *topic,
                              const uint8_t *payload, size_t plen)
{
    pthread_mutex_lock(&g_clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_clients[i].in_use || i == from_slot) continue;
        for (int s = 0; s < g_clients[i].nsubs; s++) {
            if (topic_match(g_clients[i].subs[s], topic)) {
                send_publish(g_clients[i].fd, topic, payload, plen);
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_clients_lock);
}

struct broker_arg { int cfd; struct sockaddr_storage peer; };

static void *broker_thread(void *p)
{
    struct broker_arg *a = p;
    int fd = a->cfd;
    char host[64], serv[16];
    getnameinfo((struct sockaddr*)&a->peer, sizeof(a->peer),
                host, sizeof(host), serv, sizeof(serv),
                NI_NUMERICHOST | NI_NUMERICSERV);
    free(a);

    /* expect CONNECT */
    struct pkt p0 = {0};
    if (read_pkt(fd, &p0) < 0 || p0.type != MQTT_CONNECT) {
        pkt_free(&p0); close(fd); return NULL;
    }
    /* parse client id: skip 2-byte len + "MQTT" (or name) + level + flags + ka */
    char id[64] = "unknown";
    if (p0.len >= 12) {
        uint16_t name_len = get_u16(p0.data);
        size_t off = 2 + name_len + 1 + 1 + 2;
        if (off + 2 <= p0.len) {
            uint16_t id_len = get_u16(p0.data + off);
            off += 2;
            if (off + id_len <= p0.len) {
                size_t n = id_len < sizeof(id) - 1 ? id_len : sizeof(id) - 1;
                memcpy(id, p0.data + off, n); id[n] = 0;
            }
        }
    }
    pkt_free(&p0);

    /* send CONNACK */
    uint8_t ack[] = { MQTT_CONNACK, 2, 0, 0 };
    write_all(fd, ack, sizeof(ack));

    int slot = client_register(fd, id);
    if (slot < 0) { close(fd); return NULL; }
    ui_ok("connected [%s:%s] id=%s slot=%d", host, serv, id, slot);

    for (;;) {
        struct pkt pk = {0};
        if (read_pkt(fd, &pk) < 0) break;

        if (pk.type == MQTT_PUBLISH) {
            if (pk.len >= 2) {
                uint16_t tlen = get_u16(pk.data);
                if ((uint32_t)(2 + tlen) <= pk.len) {
                    int has_pid = (pk.flags & 0x06) != 0;
                    size_t off = 2 + tlen + (has_pid ? 2 : 0);
                    if (off <= pk.len) {
                        char topic[256];
                        size_t n = tlen < sizeof(topic) - 1 ? tlen : sizeof(topic) - 1;
                        memcpy(topic, pk.data + 2, n); topic[n] = 0;
                        ui_info("pub from %s -> %s (%u bytes)",
                                id, topic, (unsigned)(pk.len - off));
                        broker_distribute(slot, topic, pk.data + off, pk.len - off);
                    }
                }
            }
        } else if (pk.type == MQTT_SUBSCRIBE) {
            /* skip 2-byte packet id */
            if (pk.len >= 5) {
                size_t off = 2;
                while (off + 3 <= pk.len) {
                    uint16_t tlen = get_u16(pk.data + off); off += 2;
                    if (off + tlen + 1 > pk.len) break;
                    pthread_mutex_lock(&g_clients_lock);
                    if (g_clients[slot].nsubs < MAX_SUBS_PER) {
                        size_t n = tlen < sizeof(g_clients[slot].subs[0]) - 1
                                 ? tlen : sizeof(g_clients[slot].subs[0]) - 1;
                        memcpy(g_clients[slot].subs[g_clients[slot].nsubs],
                               pk.data + off, n);
                        g_clients[slot].subs[g_clients[slot].nsubs][n] = 0;
                        g_clients[slot].nsubs++;
                    }
                    pthread_mutex_unlock(&g_clients_lock);
                    off += tlen + 1;
                }
            }
            /* SUBACK: pkt_id + one return code per topic */
            uint8_t suback[8];
            int slen = 0;
            suback[slen++] = MQTT_SUBACK;
            suback[slen++] = 3;                /* remaining */
            suback[slen++] = pk.data[0];       /* pkt id MSB */
            suback[slen++] = pk.data[1];       /* pkt id LSB */
            suback[slen++] = 0;                /* QoS 0 granted */
            write_all(fd, suback, slen);
            ui_ok("sub from %s", id);
        } else if (pk.type == MQTT_PINGREQ) {
            uint8_t pr[] = { MQTT_PINGRESP, 0 };
            write_all(fd, pr, sizeof(pr));
        } else if (pk.type == MQTT_DISCONNECT) {
            pkt_free(&pk); break;
        }
        pkt_free(&pk);
    }

    client_unregister(slot);
    ui_info("closed [%s:%s] id=%s", host, serv, id);
    close(fd);
    return NULL;
}

static void mqtt_server_help(void)
{
    printf("Usage: mqtt-server -p <port> [options]\n"
           "  -H, --host HOST       bind address (default: any)\n"
           "  -p, --port PORT       listen port (required, classic: 1883)\n"
           "  -h, --help            show this help\n");
}

int mqtt_server_main(int argc, char **argv)
{
    const char *host = NULL, *port = NULL;
    static struct option opts[] = {
        {"host",1,0,'H'},{"port",1,0,'p'},{"help",0,0,'h'},{0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "H:p:h", opts, NULL)) != -1) {
        switch (c) {
        case 'H': host = optarg; break;
        case 'p': port = optarg; break;
        case 'h': mqtt_server_help(); return 0;
        default:  mqtt_server_help(); return 1;
        }
    }
    if (!port) { mqtt_server_help(); return 1; }

    install_sigint(&g_stop);
    int sfd = tcp_listen(host, port, 16);
    if (sfd < 0) { ui_err("%s %s:%s", T(T_E_LISTEN), host?host:"*", port); return 2; }

    ui_section(ui_icon_globe(), "MQTT broker");
    ui_kv(T(T_LISTEN), "%smqtt://%s:%s%s", UI_BCYAN, host?host:"0.0.0.0", port, UI_RESET);
    ui_info(T(T_WAITING_CLIENTS));

    while (!g_stop) {
        struct sockaddr_storage peer; socklen_t plen = sizeof(peer);
        int cfd = accept(sfd, (struct sockaddr*)&peer, &plen);
        if (cfd < 0) { if (errno == EINTR) continue; break; }
        struct broker_arg *a = malloc(sizeof(*a));
        if (!a) { close(cfd); continue; }
        a->cfd = cfd; a->peer = peer;
        pthread_t th;
        if (pthread_create(&th, NULL, broker_thread, a) != 0) {
            close(cfd); free(a); continue;
        }
        pthread_detach(th);
    }
    close(sfd);
    return 0;
}
