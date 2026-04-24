/*
 * http.c — HTTP/1.1 client and server.
 *
 * Client:
 *   - GET / POST
 *   - custom -X method, -H header, -d body, -o output file
 *   - follows up to 5 redirects (-L)
 *   - prints status, response headers, body (size or hex tail)
 *
 * Server:
 *   - listens on -p <port>
 *   - default: returns "200 OK\nhello from socketTool\n"
 *   - -r <root>  serve static files from a directory (no .. allowed)
 *   - -1 once mode
 *   - per-connection thread (multi-client)
 *
 * No TLS. No keep-alive. No chunked-encoded bodies on the request side.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "core/applet.h"
#include "ui/ui.h"
#include "net/net.h"
#include "i18n/i18n.h"

static volatile int g_stop;

/* ---- shared util: readable from URL --------------------------------------- */

struct url {
    char  scheme[8];        /* "http"      (https rejected: TLS unsupported) */
    char  host[256];
    char  port[8];
    char  path[1024];
};

static int parse_url(const char *u, struct url *o)
{
    memset(o, 0, sizeof(*o));
    const char *p = u;
    const char *sch_end = strstr(p, "://");
    if (sch_end) {
        size_t n = sch_end - p;
        if (n >= sizeof(o->scheme)) return -1;
        memcpy(o->scheme, p, n); o->scheme[n] = 0;
        p = sch_end + 3;
    } else {
        snprintf(o->scheme, sizeof(o->scheme), "http");
    }
    if (strcmp(o->scheme, "http") != 0) return -1;

    const char *path_start = strchr(p, '/');
    const char *hostport_end = path_start ? path_start : p + strlen(p);
    size_t hp_len = hostport_end - p;
    char hp[300]; if (hp_len >= sizeof(hp)) return -1;
    memcpy(hp, p, hp_len); hp[hp_len] = 0;

    char *colon = strchr(hp, ':');
    if (colon) {
        *colon = 0;
        snprintf(o->host, sizeof(o->host), "%s", hp);
        snprintf(o->port, sizeof(o->port), "%s", colon + 1);
    } else {
        snprintf(o->host, sizeof(o->host), "%s", hp);
        snprintf(o->port, sizeof(o->port), "%s", "80");
    }
    snprintf(o->path, sizeof(o->path), "%s", path_start ? path_start : "/");
    return 0;
}

/* ---- client --------------------------------------------------------------- */

static void http_client_help(void)
{
    printf("Usage: http-client [options] <url>\n"
           "  -X, --method M        method (GET default; POST/PUT/DELETE/HEAD)\n"
           "  -H, --header H        add header (repeatable). Format: 'Name: value'\n"
           "  -d, --data BODY       request body (sets Content-Length)\n"
           "  -o, --output FILE     save body to FILE\n"
           "  -L, --location        follow up to 5 redirects (3xx)\n"
           "  -t, --timeout MS      connect timeout (default 5000)\n"
           "  -h, --help            show this help\n");
}

static int recv_until_dcrlf(int fd, char *buf, size_t cap, size_t *out_n)
{
    size_t n = 0;
    while (n + 1 < cap) {
        ssize_t r = recv(fd, buf + n, 1, 0);
        if (r <= 0) return -1;
        n += r;
        if (n >= 4 && memcmp(buf + n - 4, "\r\n\r\n", 4) == 0) {
            buf[n] = 0; *out_n = n; return 0;
        }
    }
    return -1;
}

static const char *header_get(const char *headers, const char *name, char *out, size_t cap)
{
    size_t nl = strlen(name);
    const char *p = headers;
    while (*p) {
        if (strncasecmp(p, name, nl) == 0 && p[nl] == ':') {
            const char *v = p + nl + 1;
            while (*v == ' ' || *v == '\t') v++;
            const char *eol = strstr(v, "\r\n");
            if (!eol) return NULL;
            size_t l = eol - v; if (l >= cap) l = cap - 1;
            memcpy(out, v, l); out[l] = 0;
            return out;
        }
        const char *eol = strstr(p, "\r\n");
        if (!eol) return NULL;
        p = eol + 2;
    }
    return NULL;
}

static int http_do_one(const struct url *u, const char *method,
                       char extra_headers[][256], int header_count,
                       const char *body, int timeout_ms,
                       int *status, char *resp_headers, size_t resp_cap,
                       int out_fd)
{
    int fd = tcp_connect(u->host, u->port, timeout_ms);
    if (fd < 0) { ui_err("connect %s:%s: %s", u->host, u->port, strerror(errno)); return -1; }

    char req[4096];
    int rl = snprintf(req, sizeof(req),
        "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: socketTool\r\nAccept: */*\r\nConnection: close\r\n",
        method, u->path, u->host);

    for (int i = 0; i < header_count; i++)
        rl += snprintf(req + rl, sizeof(req) - rl, "%s\r\n", extra_headers[i]);

    if (body) rl += snprintf(req + rl, sizeof(req) - rl,
                              "Content-Length: %zu\r\n", strlen(body));
    rl += snprintf(req + rl, sizeof(req) - rl, "\r\n");
    if (body) rl += snprintf(req + rl, sizeof(req) - rl, "%s", body);

    if (send(fd, req, rl, 0) != rl) {
        ui_err("send: %s", strerror(errno)); close(fd); return -1;
    }

    size_t hn = 0;
    if (recv_until_dcrlf(fd, resp_headers, resp_cap, &hn) < 0) {
        ui_err("no response headers"); close(fd); return -1;
    }
    if (sscanf(resp_headers, "HTTP/1.%*d %d", status) != 1) {
        ui_err("malformed status line"); close(fd); return -1;
    }

    /* stream the body to out_fd, counting bytes */
    long long total = 0;
    char buf[8192];
    for (;;) {
        ssize_t r = recv(fd, buf, sizeof(buf), 0);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) break;
        if (out_fd >= 0) { ssize_t _ = write(out_fd, buf, r); (void)_; }
        total += r;
    }
    close(fd);
    return (int)total;
}

int http_client_main(int argc, char **argv)
{
    const char *method = "GET", *body = NULL, *outfile = NULL;
    int timeout = 5000, follow = 0;
    char extra_headers[16][256]; int header_count = 0;

    static struct option opts[] = {
        {"method",1,0,'X'},{"header",1,0,'H'},{"data",1,0,'d'},
        {"output",1,0,'o'},{"location",0,0,'L'},{"timeout",1,0,'t'},
        {"help",0,0,'h'},{0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "X:H:d:o:Lt:h", opts, NULL)) != -1) {
        switch (c) {
        case 'X': method = optarg; break;
        case 'H':
            if (header_count >= 16) { ui_err("too many -H (max 16)"); return 1; }
            snprintf(extra_headers[header_count++], 256, "%s", optarg);
            break;
        case 'd': body = optarg; if (strcmp(method, "GET") == 0) method = "POST"; break;
        case 'o': outfile = optarg; break;
        case 'L': follow = 1; break;
        case 't': timeout = atoi(optarg); break;
        case 'h': http_client_help(); return 0;
        default:  http_client_help(); return 1;
        }
    }
    if (optind >= argc) { http_client_help(); return 1; }

    struct url u;
    if (parse_url(argv[optind], &u) < 0) {
        ui_err("bad url: %s (only http:// supported)", argv[optind]);
        return 1;
    }

    install_sigint(&g_stop);

    int out_fd = -1;
    if (outfile) {
        out_fd = open(outfile, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (out_fd < 0) { ui_err("open %s: %s", outfile, strerror(errno)); return 2; }
    }

    int hops = 0;
    char resp_headers[8192];
    int status;
    long long body_bytes = 0;

    for (;;) {
        ui_section(ui_icon_globe(), "HTTP client");
        ui_kv(T(T_TARGET), "%s%s://%s:%s%s%s",
              UI_BCYAN, u.scheme, u.host, u.port, u.path, UI_RESET);
        ui_kv("method",    "%s", method);
        if (body) ui_kv("body",       "%zu bytes", strlen(body));

        long t0 = now_ms();
        int rc = http_do_one(&u, method, extra_headers, header_count,
                             body, timeout, &status, resp_headers,
                             sizeof(resp_headers), out_fd);
        if (rc < 0) { if (out_fd >= 0) close(out_fd); return 3; }
        body_bytes = rc;

        ui_ok("status %s%d%s   body %lld bytes   %ld ms",
              status >= 200 && status < 300 ? UI_BGREEN :
              status >= 400 ? UI_BRED : UI_BYELLOW,
              status, UI_RESET, body_bytes, now_ms() - t0);

        if (follow && hops < 5 &&
            (status == 301 || status == 302 || status == 303 ||
             status == 307 || status == 308)) {
            char loc[1024];
            if (header_get(resp_headers, "Location", loc, sizeof(loc))) {
                hops++;
                struct url nu;
                /* relative redirect: prepend scheme+host */
                if (loc[0] == '/') {
                    nu = u;
                    snprintf(nu.path, sizeof(nu.path), "%s", loc);
                } else if (parse_url(loc, &nu) < 0) {
                    ui_warn("redirect to non-http URL skipped: %s", loc); break;
                }
                u = nu;
                ui_info("redirect [%d] -> %s://%s:%s%s",
                        hops, u.scheme, u.host, u.port, u.path);
                continue;
            }
        }
        break;
    }

    if (out_fd >= 0) close(out_fd);
    return (status >= 200 && status < 400) ? 0 : 4;
}

/* ---- server --------------------------------------------------------------- */

static void http_server_help(void)
{
    printf("Usage: http-server -p <port> [options]\n"
           "  -H, --host HOST       bind address (default: any)\n"
           "  -p, --port PORT       listen port (required)\n"
           "  -r, --root DIR        serve static files from DIR\n"
           "  -1, --once            handle a single client then exit\n"
           "  -h, --help            show this help\n");
}

struct http_serve_arg {
    int                       cfd;
    struct sockaddr_storage   peer;
    const char               *root;
};

static const char *mime_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcasecmp(dot, ".html")||!strcasecmp(dot, ".htm")) return "text/html";
    if (!strcasecmp(dot, ".css"))  return "text/css";
    if (!strcasecmp(dot, ".js"))   return "application/javascript";
    if (!strcasecmp(dot, ".json")) return "application/json";
    if (!strcasecmp(dot, ".png"))  return "image/png";
    if (!strcasecmp(dot, ".jpg")||!strcasecmp(dot, ".jpeg")) return "image/jpeg";
    if (!strcasecmp(dot, ".gif"))  return "image/gif";
    if (!strcasecmp(dot, ".svg"))  return "image/svg+xml";
    if (!strcasecmp(dot, ".txt"))  return "text/plain";
    return "application/octet-stream";
}

static void send_simple(int cfd, int status, const char *reason,
                        const char *body)
{
    char hdr[512];
    int len = body ? (int)strlen(body) : 0;
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\n"
        "Content-Length: %d\r\nConnection: close\r\n\r\n",
        status, reason, len);
    send(cfd, hdr, n, 0);
    if (body) send(cfd, body, len, 0);
}

static void serve_static(int cfd, const char *root, const char *raw_path)
{
    /* strip query, reject ".." */
    char path[1024]; snprintf(path, sizeof(path), "%s", raw_path);
    char *q = strchr(path, '?'); if (q) *q = 0;
    if (strstr(path, "..")) { send_simple(cfd, 400, "Bad Request", "bad path\n"); return; }

    char full[2048];
    snprintf(full, sizeof(full), "%s%s", root, path);
    struct stat st;
    if (stat(full, &st) < 0) { send_simple(cfd, 404, "Not Found", "not found\n"); return; }
    if (S_ISDIR(st.st_mode)) {
        size_t l = strlen(full);
        if (l + 11 < sizeof(full)) snprintf(full + l, sizeof(full) - l, "/index.html");
        if (stat(full, &st) < 0) { send_simple(cfd, 404, "Not Found", "no index\n"); return; }
    }
    int f = open(full, O_RDONLY);
    if (f < 0) { send_simple(cfd, 500, "Internal Server Error", "open failed\n"); return; }

    char hdr[512];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
        "Content-Length: %lld\r\nConnection: close\r\n\r\n",
        mime_for(full), (long long)st.st_size);
    send(cfd, hdr, n, 0);

    char buf[8192];
    ssize_t r;
    while ((r = read(f, buf, sizeof(buf))) > 0) send(cfd, buf, r, 0);
    close(f);
}

static int http_serve_one(struct http_serve_arg *a)
{
    int cfd = a->cfd;
    const char *root = a->root;
    char host[64], serv[16];
    getnameinfo((struct sockaddr*)&a->peer, sizeof(a->peer),
                host, sizeof(host), serv, sizeof(serv),
                NI_NUMERICHOST | NI_NUMERICSERV);

    char buf[8192]; size_t hn = 0;
    if (recv_until_dcrlf(cfd, buf, sizeof(buf), &hn) < 0) {
        close(cfd); return 0;
    }

    char method[16] = "", path[1024] = "";
    sscanf(buf, "%15s %1023s", method, path);
    ui_ok("[%s:%s] %s %s", host, serv, method, path);

    if (root)
        serve_static(cfd, root, path);
    else
        send_simple(cfd, 200, "OK", "hello from socketTool\n");

    close(cfd);
    return 1;
}

static void *http_serve_thread(void *p)
{
    struct http_serve_arg *a = p;
    http_serve_one(a);
    free(a);
    return NULL;
}

int http_server_main(int argc, char **argv)
{
    const char *host = NULL, *port = NULL, *root = NULL;
    int once = 0;

    static struct option opts[] = {
        {"host",1,0,'H'},{"port",1,0,'p'},{"root",1,0,'r'},
        {"once",0,0,'1'},{"help",0,0,'h'},{0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "H:p:r:1h", opts, NULL)) != -1) {
        switch (c) {
        case 'H': host = optarg; break;
        case 'p': port = optarg; break;
        case 'r': root = optarg; break;
        case '1': once = 1; break;
        case 'h': http_server_help(); return 0;
        default:  http_server_help(); return 1;
        }
    }
    if (!port) { http_server_help(); return 1; }

    install_sigint(&g_stop);
    int sfd = tcp_listen(host, port, 16);
    if (sfd < 0) { ui_err("%s %s:%s", T(T_E_LISTEN), host?host:"*", port); return 2; }

    ui_section(ui_icon_globe(), "HTTP server");
    ui_kv(T(T_LISTEN), "%shttp://%s:%s/%s", UI_BCYAN, host?host:"0.0.0.0", port, UI_RESET);
    if (root) ui_kv("root", "%s", root);
    ui_info(T(T_WAITING_CLIENTS));

    while (!g_stop) {
        struct sockaddr_storage peer; socklen_t plen = sizeof(peer);
        int cfd = accept(sfd, (struct sockaddr*)&peer, &plen);
        if (cfd < 0) { if (errno == EINTR) continue; break; }

        struct http_serve_arg *a = malloc(sizeof(*a));
        if (!a) { close(cfd); continue; }
        a->cfd = cfd; a->peer = peer; a->root = root;

        if (once) { http_serve_one(a); free(a); break; }
        pthread_t th;
        if (pthread_create(&th, NULL, http_serve_thread, a) != 0) {
            ui_warn("pthread_create: %s", strerror(errno));
            close(cfd); free(a); continue;
        }
        pthread_detach(th);
    }
    close(sfd);
    return 0;
}
