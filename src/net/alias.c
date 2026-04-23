/*
 * alias.c — host aliases from ~/.socketToolrc.
 *
 * File format (one per line):
 *     # comment
 *     router   192.168.1.1
 *     db       db.internal:5432
 *     srv=10.0.0.2:8080
 *
 * Callers pass the alias name *without* the leading '@'; alias_apply_host()
 * handles the '@' prefix convention at the CLI boundary.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <pwd.h>
#include <unistd.h>

#include "net/net.h"

struct alias {
    char *name;
    char *host;
    char *port;     /* may be NULL */
};

static struct alias   *g_tab = NULL;
static int             g_n   = 0;
static int             g_loaded = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static char *xstrdup(const char *s) { return s ? strdup(s) : NULL; }

static void strip(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r' ||
                 s[n-1] == ' '  || s[n-1] == '\t')) s[--n] = 0;
}

static void parse_line(char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '#' || *line == 0) return;

    /* split name / rest via '=', ':' (when '=' absent) or whitespace */
    char *name = line;
    char *rest = NULL;
    char *eq = strchr(line, '=');
    if (eq) {
        *eq = 0; rest = eq + 1;
    } else {
        char *w = line;
        while (*w && *w != ' ' && *w != '\t') w++;
        if (!*w) return;
        *w = 0; rest = w + 1;
    }
    while (*rest == ' ' || *rest == '\t') rest++;
    strip(name); strip(rest);
    if (!*name || !*rest) return;

    /* split rest into host + optional port */
    char *host = rest, *port = NULL;
    char *colon = strrchr(rest, ':');
    if (colon && colon != rest) {
        /* only treat as port if all-digits after ':' */
        int ok = 1;
        for (char *p = colon + 1; *p; p++)
            if (!isdigit((unsigned char)*p)) { ok = 0; break; }
        if (ok) { *colon = 0; port = colon + 1; }
    }

    struct alias *na = realloc(g_tab, (g_n + 1) * sizeof(*g_tab));
    if (!na) return;
    g_tab = na;
    g_tab[g_n].name = xstrdup(name);
    g_tab[g_n].host = xstrdup(host);
    g_tab[g_n].port = port ? xstrdup(port) : NULL;
    g_n++;
}

static void load_once(void)
{
    if (g_loaded) return;
    g_loaded = 1;

    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (!home) return;

    char path[1024];
    snprintf(path, sizeof(path), "%s/.socketToolrc", home);
    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) parse_line(buf);
    fclose(fp);
}

int alias_resolve(const char *name, const char **host, const char **port)
{
    if (!name || !*name) return 0;
    pthread_mutex_lock(&g_lock);
    load_once();
    for (int i = 0; i < g_n; i++) {
        if (strcmp(g_tab[i].name, name) == 0) {
            if (host) *host = g_tab[i].host;
            if (port) *port = g_tab[i].port;
            pthread_mutex_unlock(&g_lock);
            return 1;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return 0;
}

void alias_apply_host(const char **host, const char **port)
{
    if (!host || !*host) return;
    const char *in = *host;
    if (in[0] != '@') return;
    const char *h = NULL, *p = NULL;
    if (!alias_resolve(in + 1, &h, &p)) return;
    if (h) *host = h;
    if (p && port && (!*port || !**port)) *port = p;
}
