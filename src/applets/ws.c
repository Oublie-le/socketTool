/*
 * ws.c — minimal RFC 6455 WebSocket client and server.
 *
 * Supports: handshake, text frames, ping/pong, close.
 * No TLS (ws://), no extensions. Single-threaded server (one client at a time).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <ctype.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "core/applet.h"
#include "ui/ui.h"
#include "net/net.h"
#include "i18n/i18n.h"

#include <sys/select.h>

static volatile int g_stop;

/* ---------------------------------------------------------------- SHA1 ---- */
/* Public-domain Steve Reid SHA-1, trimmed. */
typedef struct { uint32_t s[5]; uint32_t c[2]; uint8_t b[64]; } sha1_t;

#define ROL(v,n) (((v)<<(n))|((v)>>(32-(n))))
static void sha1_tr(uint32_t s[5], const uint8_t *buf)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)buf[i*4]<<24 | (uint32_t)buf[i*4+1]<<16
             | (uint32_t)buf[i*4+2]<<8 | buf[i*4+3];
    for (int i = 16; i < 80; i++)
        w[i] = ROL(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);
    uint32_t a=s[0],b=s[1],c=s[2],d=s[3],e=s[4],f,k,t;
    for (int i = 0; i < 80; i++) {
        if (i<20)      { f = (b&c)|((~b)&d);          k = 0x5A827999; }
        else if (i<40) { f = b^c^d;                   k = 0x6ED9EBA1; }
        else if (i<60) { f = (b&c)|(b&d)|(c&d);       k = 0x8F1BBCDC; }
        else           { f = b^c^d;                   k = 0xCA62C1D6; }
        t = ROL(a,5) + f + e + k + w[i];
        e=d; d=c; c=ROL(b,30); b=a; a=t;
    }
    s[0]+=a; s[1]+=b; s[2]+=c; s[3]+=d; s[4]+=e;
}
static void sha1_init(sha1_t *h){
    h->s[0]=0x67452301; h->s[1]=0xEFCDAB89; h->s[2]=0x98BADCFE;
    h->s[3]=0x10325476; h->s[4]=0xC3D2E1F0; h->c[0]=h->c[1]=0;
}
static void sha1_upd(sha1_t *h, const void *p, size_t n){
    const uint8_t *d=p; uint32_t i, j = (h->c[0]>>3) & 63;
    if ((h->c[0] += n<<3) < (n<<3)) h->c[1]++;
    h->c[1] += n>>29;
    if (j+n > 63) {
        memcpy(&h->b[j], d, (i = 64-j));
        sha1_tr(h->s, h->b);
        for (; i+63 < n; i += 64) sha1_tr(h->s, &d[i]);
        j = 0;
    } else i = 0;
    memcpy(&h->b[j], &d[i], n-i);
}
static void sha1_fin(sha1_t *h, uint8_t out[20]){
    uint8_t fin[8];
    for (int i = 0; i < 8; i++)
        fin[i] = (h->c[(i<4)?1:0] >> ((3-(i&3))*8)) & 255;
    uint8_t c = 0x80; sha1_upd(h, &c, 1);
    c = 0; while ((h->c[0] & 504) != 448) sha1_upd(h, &c, 1);
    sha1_upd(h, fin, 8);
    for (int i = 0; i < 20; i++)
        out[i] = (h->s[i>>2] >> ((3-(i&3))*8)) & 255;
}

/* ------------------------------------------------------------- Base64 ----- */
static const char b64t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64enc(const uint8_t *in, size_t n, char *out)
{
    size_t i, o = 0;
    for (i = 0; i + 3 <= n; i += 3) {
        out[o++] = b64t[(in[i] >> 2) & 0x3f];
        out[o++] = b64t[((in[i] & 3) << 4) | (in[i+1] >> 4)];
        out[o++] = b64t[((in[i+1] & 0xf) << 2) | (in[i+2] >> 6)];
        out[o++] = b64t[in[i+2] & 0x3f];
    }
    if (i < n) {
        out[o++] = b64t[(in[i] >> 2) & 0x3f];
        if (i + 1 == n) {
            out[o++] = b64t[(in[i] & 3) << 4];
            out[o++] = '='; out[o++] = '=';
        } else {
            out[o++] = b64t[((in[i] & 3) << 4) | (in[i+1] >> 4)];
            out[o++] = b64t[(in[i+1] & 0xf) << 2];
            out[o++] = '=';
        }
    }
    out[o] = '\0';
}

/* ---------------------------------------------------- WS framing helpers - */
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

static void ws_accept_key(const char *client_key, char out[32])
{
    char concat[128]; uint8_t digest[20];
    snprintf(concat, sizeof(concat), "%s%s", client_key, WS_GUID);
    sha1_t h; sha1_init(&h);
    sha1_upd(&h, concat, strlen(concat));
    sha1_fin(&h, digest);
    b64enc(digest, 20, out);
}

static int read_until_dcrlf(int fd, char *buf, size_t cap)
{
    size_t n = 0;
    while (n + 1 < cap) {
        ssize_t r = recv(fd, buf + n, 1, 0);
        if (r <= 0) return -1;
        n += r;
        if (n >= 4 && memcmp(buf + n - 4, "\r\n\r\n", 4) == 0) {
            buf[n] = '\0'; return (int)n;
        }
    }
    return -1;
}

static const char *hdr_find(const char *headers, const char *name)
{
    /* case-insensitive header search; returns pointer to value start */
    size_t nl = strlen(name);
    const char *p = headers;
    while (*p) {
        if (strncasecmp(p, name, nl) == 0 && p[nl] == ':') {
            p += nl + 1;
            while (*p == ' ' || *p == '\t') p++;
            return p;
        }
        const char *eol = strstr(p, "\r\n");
        if (!eol) return NULL;
        p = eol + 2;
    }
    return NULL;
}

/* Build text frame. mask_it=1 for client. Returns total frame len. */
static ssize_t ws_build_text(uint8_t *out, size_t cap,
                             const void *payload, size_t plen, int mask_it)
{
    size_t need = 2 + plen + (mask_it ? 4 : 0);
    if (plen >= 126) need += 2;
    if (plen > 65535) need += 6;
    if (need > cap) return -1;

    size_t o = 0;
    out[o++] = 0x81;                       /* FIN | text */
    uint8_t mb = mask_it ? 0x80 : 0;
    if (plen < 126) {
        out[o++] = mb | (uint8_t)plen;
    } else if (plen <= 65535) {
        out[o++] = mb | 126;
        out[o++] = (plen >> 8) & 0xff;
        out[o++] = plen & 0xff;
    } else {
        out[o++] = mb | 127;
        for (int i = 7; i >= 0; i--) out[o++] = (plen >> (i*8)) & 0xff;
    }
    uint8_t mk[4] = {0,0,0,0};
    if (mask_it) {
        for (int i = 0; i < 4; i++) mk[i] = rand() & 0xff;
        memcpy(out + o, mk, 4); o += 4;
    }
    const uint8_t *p = payload;
    for (size_t i = 0; i < plen; i++)
        out[o++] = mask_it ? (p[i] ^ mk[i & 3]) : p[i];
    return (ssize_t)o;
}

/* Read one frame; payload returned in malloc'd buf via *out. opcode in *op. */
static int ws_recv_frame(int fd, uint8_t **out, size_t *out_len, int *op)
{
    uint8_t hdr[2];
    if (recv(fd, hdr, 2, MSG_WAITALL) != 2) return -1;
    *op = hdr[0] & 0x0f;
    int masked = hdr[1] & 0x80;
    uint64_t len = hdr[1] & 0x7f;
    if (len == 126) {
        uint8_t e[2];
        if (recv(fd, e, 2, MSG_WAITALL) != 2) return -1;
        len = ((uint64_t)e[0] << 8) | e[1];
    } else if (len == 127) {
        uint8_t e[8];
        if (recv(fd, e, 8, MSG_WAITALL) != 8) return -1;
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
    }
    uint8_t mk[4] = {0};
    if (masked && recv(fd, mk, 4, MSG_WAITALL) != 4) return -1;
    uint8_t *p = malloc(len + 1);
    if (!p) return -1;
    if (len > 0 && recv(fd, p, len, MSG_WAITALL) != (ssize_t)len) {
        free(p); return -1;
    }
    if (masked) for (uint64_t i = 0; i < len; i++) p[i] ^= mk[i & 3];
    p[len] = '\0';
    *out = p; *out_len = len;
    return 0;
}

/* ----------------------------------------------------------- ws-client ---- */

static void ws_client_help(void)
{
    printf("Usage: ws-client -H <host> -p <port> [options]\n"
           "  -H, --host HOST       remote host (required)\n"
           "  -p, --port PORT       remote port (required)\n"
           "  -P, --path PATH       request path (default /)\n"
           "  -m, --message TEXT    text frame to send after handshake\n"
           "  -i, --interactive     after send, read until close (Ctrl-C)\n"
           "  -t, --timeout MS      connect timeout (default 5000)\n"
           "  -h, --help            show this help\n");
}

int ws_client_main(int argc, char **argv)
{
    const char *host=NULL,*port=NULL,*path="/",*msg=NULL;
    int interactive=0, timeout=5000;

    static struct option opts[] = {
        {"host",1,0,'H'},{"port",1,0,'p'},{"path",1,0,'P'},
        {"message",1,0,'m'},{"interactive",0,0,'i'},{"timeout",1,0,'t'},
        {"help",0,0,'h'},{0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "H:p:P:m:it:h", opts, NULL)) != -1) {
        switch (c) {
        case 'H': host = optarg; break;
        case 'p': port = optarg; break;
        case 'P': path = optarg; break;
        case 'm': msg  = optarg; break;
        case 'i': interactive = 1; break;
        case 't': timeout = atoi(optarg); break;
        case 'h': ws_client_help(); return 0;
        default:  ws_client_help(); return 1;
        }
    }
    if (!host || !port) { ws_client_help(); return 1; }

    install_sigint(&g_stop);
    srand((unsigned)time(NULL) ^ getpid());

    ui_section(ui_icon_globe(), "WebSocket client");
    ui_kv(T(T_TARGET), "%sws://%s:%s%s%s", UI_BCYAN, host, port, path, UI_RESET);

    int fd = tcp_connect(host, port, timeout);
    if (fd < 0) { ui_err("connect: %s", strerror(errno)); return 2; }

    /* generate Sec-WebSocket-Key */
    uint8_t nonce[16]; for (int i = 0; i < 16; i++) nonce[i] = rand() & 0xff;
    char key[32]; b64enc(nonce, 16, key);

    char req[1024];
    int rl = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\nHost: %s:%s\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n", path, host, port, key);
    if (send(fd, req, rl, 0) != rl) {
        ui_err("send handshake: %s", strerror(errno));
        close(fd); return 3;
    }

    char resp[2048];
    if (read_until_dcrlf(fd, resp, sizeof(resp)) < 0) {
        ui_err("no handshake response"); close(fd); return 3;
    }
    if (strncmp(resp, "HTTP/1.1 101", 12) != 0) {
        ui_err("server refused handshake (not 101)"); close(fd); return 3;
    }
    char expect[32]; ws_accept_key(key, expect);
    const char *acc = hdr_find(resp, "Sec-WebSocket-Accept");
    if (!acc || strncmp(acc, expect, strlen(expect)) != 0) {
        ui_err("bad Sec-WebSocket-Accept"); close(fd); return 3;
    }
    ui_ok(T(T_HANDSHAKE_OK));

    if (msg) {
        uint8_t frame[8192];
        ssize_t fl = ws_build_text(frame, sizeof(frame), msg, strlen(msg), 1);
        if (fl < 0 || send(fd, frame, fl, 0) != fl) {
            ui_err("send frame failed"); close(fd); return 3;
        }
        ui_ok("sent text frame (%zu bytes payload)", strlen(msg));
    }

    /*
     * Default behavior:
     *   - if -m and not -i  : send once, wait briefly for a reply, exit.
     *   - if -i, or no -m   : enter interactive mode (stdin <-> ws),
     *                          stay until peer close / Ctrl-C / stdin EOF.
     */
    int once = (msg && !interactive);
    if (once) set_recv_timeout(fd, 1000);
    else {
        set_recv_timeout(fd, 0);
        ui_info(T(T_INTERACTIVE_MODE));
    }

    int maxfd = fd > STDIN_FILENO ? fd : STDIN_FILENO;
    char inbuf[4096];
    for (;;) {
        if (g_stop) break;

        if (once) {
            uint8_t *pl = NULL; size_t pn = 0; int op = 0;
            if (ws_recv_frame(fd, &pl, &pn, &op) < 0) break;
            if (op == 0x1)      printf("%s%s%s %.*s\n",
                                       UI_BMAGENTA, ui_icon_recv(), UI_RESET,
                                       (int)pn, pl);
            else if (op == 0x8) { ui_info(T(T_SERVER_CLOSED)); free(pl); break; }
            free(pl);
            break;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        FD_SET(STDIN_FILENO, &rfds);
        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (FD_ISSET(fd, &rfds)) {
            uint8_t *pl = NULL; size_t pn = 0; int op = 0;
            if (ws_recv_frame(fd, &pl, &pn, &op) < 0) break;
            if (op == 0x1)      printf("%s%s%s %.*s\n",
                                       UI_BMAGENTA, ui_icon_recv(), UI_RESET,
                                       (int)pn, pl);
            else if (op == 0x8) { ui_info(T(T_SERVER_CLOSED)); free(pl); break; }
            free(pl);
        }
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            ssize_t n = read(STDIN_FILENO, inbuf, sizeof(inbuf));
            if (n <= 0) break;
            /* strip trailing newline so the peer sees a clean frame */
            while (n > 0 && (inbuf[n-1] == '\n' || inbuf[n-1] == '\r')) n--;
            if (n == 0) continue;
            uint8_t frame[8192];
            ssize_t fl = ws_build_text(frame, sizeof(frame), inbuf, n, 1);
            if (fl < 0 || send(fd, frame, fl, 0) != fl) break;
        }
    }
    close(fd);
    return 0;
}

/* ----------------------------------------------------------- ws-server ---- */

static void ws_server_help(void)
{
    printf("Usage: ws-server -p <port> [options]\n"
           "  -H, --host HOST       bind address (default: any)\n"
           "  -p, --port PORT       listen port (required)\n"
           "  -e, --echo            echo frames back (default)\n"
           "  -1, --once            handle a single client then exit\n"
           "  -h, --help            show this help\n");
}

static int ws_handshake_server(int cfd)
{
    char req[2048];
    if (read_until_dcrlf(cfd, req, sizeof(req)) < 0) return -1;
    const char *key = hdr_find(req, "Sec-WebSocket-Key");
    if (!key) return -1;
    char keybuf[64]; size_t i = 0;
    while (key[i] && key[i] != '\r' && i < sizeof(keybuf) - 1) {
        keybuf[i] = key[i]; i++;
    }
    keybuf[i] = '\0';

    char accept[32]; ws_accept_key(keybuf, accept);
    char resp[512];
    int rl = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);
    return send(cfd, resp, rl, 0) == rl ? 0 : -1;
}

int ws_server_main(int argc, char **argv)
{
    const char *host = NULL, *port = NULL;
    int echo = 1, once = 0;

    static struct option opts[] = {
        {"host",1,0,'H'},{"port",1,0,'p'},{"echo",0,0,'e'},
        {"once",0,0,'1'},{"help",0,0,'h'},{0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "H:p:e1h", opts, NULL)) != -1) {
        switch (c) {
        case 'H': host = optarg; break;
        case 'p': port = optarg; break;
        case 'e': echo = 1; break;
        case '1': once = 1; break;
        case 'h': ws_server_help(); return 0;
        default:  ws_server_help(); return 1;
        }
    }
    if (!port) { ws_server_help(); return 1; }

    install_sigint(&g_stop);
    int sfd = tcp_listen(host, port, 16);
    if (sfd < 0) { ui_err(T(T_E_LISTEN)); return 2; }

    ui_section(ui_icon_globe(), "WebSocket server");
    ui_kv(T(T_LISTEN), "%sws://%s:%s/%s", UI_BCYAN, host?host:"0.0.0.0", port, UI_RESET);
    ui_kv(T(T_MODE),   "%s%s%s", echo?UI_BGREEN:UI_BYELLOW,
                                  echo?T(T_ECHO):T(T_DISCARD), UI_RESET);
    ui_info(T(T_WAITING_CLIENTS));

    while (!g_stop) {
        struct sockaddr_storage peer; socklen_t plen = sizeof(peer);
        int cfd = accept(sfd, (struct sockaddr*)&peer, &plen);
        if (cfd < 0) { if (errno == EINTR) continue; break; }

        char hbuf[64], sbuf[16];
        getnameinfo((struct sockaddr*)&peer, plen,
                    hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
                    NI_NUMERICHOST | NI_NUMERICSERV);

        if (ws_handshake_server(cfd) < 0) {
            ui_warn(T(T_E_HANDSHAKE));
            close(cfd); continue;
        }
        ui_ok(T(T_ACCEPTED), hbuf, sbuf);

        for (;;) {
            uint8_t *pl = NULL; size_t pn = 0; int op = 0;
            if (ws_recv_frame(cfd, &pl, &pn, &op) < 0) break;
            if (op == 0x8) { free(pl); break; }
            if (op == 0x1) {
                printf("%s<<%s [%s:%s] %.*s\n",
                       UI_DIM, UI_RESET, hbuf, sbuf, (int)pn, pl);
                if (echo) {
                    uint8_t f[8192];
                    ssize_t fl = ws_build_text(f, sizeof(f), pl, pn, 0);
                    if (fl > 0) send(cfd, f, fl, 0);
                }
            }
            free(pl);
        }
        ui_info(T(T_CLOSED), hbuf, sbuf);
        close(cfd);
        if (once) break;
    }
    close(sfd);
    return 0;
}
