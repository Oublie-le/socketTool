/*
 * ui.c — terminal UI helpers.
 */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>

#include "ui/ui.h"

int ui_color_enabled   = 1;
int ui_unicode_enabled = 1;

/* ------------------------------------------------------------ icons ------ */

#define ICON(uni, ascii) (ui_unicode_enabled ? (uni) : (ascii))

const char *ui_icon_ok(void)        { return ICON("✔", "[OK]"); }
const char *ui_icon_fail(void)      { return ICON("✘", "[X]");  }
const char *ui_icon_warn(void)      { return ICON("⚠", "[!]");  }
const char *ui_icon_info(void)      { return ICON("ℹ", "[i]");  }
const char *ui_icon_arrow(void)     { return ICON("➜", ">");    }
const char *ui_icon_bullet(void)    { return ICON("•", "*");    }
const char *ui_icon_hourglass(void) { return ICON("⏱", "(t)");  }
const char *ui_icon_rocket(void)    { return ICON("🚀","->>");  }
const char *ui_icon_recv(void)      { return ICON("◀", "<<");   }
const char *ui_icon_send(void)      { return ICON("▶", ">>");   }
const char *ui_icon_target(void)    { return ICON("◎", "()");   }
const char *ui_icon_globe(void)     { return ICON("🌐","[*]");  }

/* ------------------------------------------------------------ width ------ */

/* Display width of a UTF-8 string. CJK / emoji = 2 cols; others = 1. */
static int utf8_dwidth(const char *s)
{
    int total = 0; mbstate_t st = {0};
    while (*s) {
        wchar_t wc; size_t n = mbrtowc(&wc, s, 8, &st);
        if (n == (size_t)-1 || n == (size_t)-2 || n == 0) { s++; total++; continue; }
        int w = wcwidth(wc);
        if (w < 0) w = 1;
        total += w;
        s += n;
    }
    return total;
}

/* ----------------------------------------------------------- init -------- */

void ui_init(void)
{
    setlocale(LC_CTYPE, "");
    const char *no_color = getenv("NO_COLOR");
    if (no_color && *no_color) ui_color_enabled = 0;
    if (!isatty(STDOUT_FILENO)) ui_color_enabled = 0;

    const char *lc = setlocale(LC_CTYPE, NULL);
    const char *l  = getenv("LANG");
    int utf8 = (lc && (strstr(lc, "UTF-8") || strstr(lc, "utf8"))) ||
               (l  && (strstr(l,  "UTF-8") || strstr(l,  "utf8")));
    ui_unicode_enabled = utf8 ? 1 : 0;
}

/* ----------------------------------------------------------- title ------- */

static void repeat_glyph(const char *g, int n)
{
    for (int i = 0; i < n; i++) fputs(g, stdout);
}

void ui_title(const char *title)
{
    int w = utf8_dwidth(title);
    const char *tl = ICON("╭", "+"), *tr = ICON("╮", "+");
    const char *bl = ICON("╰", "+"), *br = ICON("╯", "+");
    const char *hz = ICON("─", "-"), *vt = ICON("│", "|");

    putchar('\n');
    printf("%s%s%s", UI_BOLD, UI_BCYAN, tl);
    repeat_glyph(hz, w + 2);
    printf("%s%s\n", tr, UI_RESET);

    printf("%s%s%s%s %s %s%s%s%s\n",
           UI_BOLD, UI_BCYAN, vt, UI_RESET,
           title,
           UI_BOLD, UI_BCYAN, vt, UI_RESET);

    printf("%s%s%s", UI_BOLD, UI_BCYAN, bl);
    repeat_glyph(hz, w + 2);
    printf("%s%s\n", br, UI_RESET);
}

void ui_section(const char *icon, const char *title)
{
    if (!icon) icon = ui_icon_arrow();
    printf("\n%s%s%s %s%s%s\n",
           UI_BOLD, UI_BBLUE, icon, UI_BOLD, title, UI_RESET);
}

/* ----------------------------------------------------------- status ----- */

static void ui_vp(FILE *f, const char *icon, const char *color,
                  const char *fmt, va_list ap)
{
    fprintf(f, " %s%s%s ", color, icon, UI_RESET);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
}

void ui_ok(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    ui_vp(stdout, ui_icon_ok(),   UI_BGREEN,  fmt, ap);
    va_end(ap);
}
void ui_warn(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    ui_vp(stdout, ui_icon_warn(), UI_BYELLOW, fmt, ap);
    va_end(ap);
}
void ui_err(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    ui_vp(stderr, ui_icon_fail(), UI_BRED,    fmt, ap);
    va_end(ap);
}
void ui_info(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    ui_vp(stdout, ui_icon_info(), UI_BCYAN,   fmt, ap);
    va_end(ap);
}

void ui_kv(const char *key, const char *fmt, ...)
{
    int kw = utf8_dwidth(key);
    int pad = 14 - kw; if (pad < 1) pad = 1;
    printf("  %s%s%s%s%*s ", UI_DIM, UI_GRAY, key, UI_RESET, pad, "");
    va_list ap; va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

/* ----------------------------------------------------------- progress -- */

void ui_progress(int done, int total, const char *label)
{
    if (total <= 0) return;
    int width = 30;
    int filled = (done * width) / total;
    int pct    = (done * 100) / total;

    fputc('\r', stdout);
    printf("  %s%s%s [", UI_BCYAN, ui_icon_hourglass(), UI_RESET);
    for (int i = 0; i < width; i++) {
        if (i < filled) printf("%s%s%s", UI_BGREEN, ICON("█", "#"), UI_RESET);
        else            fputs(ICON("░", "."), stdout);
    }
    printf("] %3d%%  %d/%d  %s",
           pct, done, total, label ? label : "");
    fflush(stdout);
}

void ui_progress_done(void)
{
    fputs("\r\033[K", stdout); fflush(stdout);
}

/* ----------------------------------------------------------- table ----- */

static void print_top(const int widths[], int n)
{
    fputs(ICON("┌", "+"), stdout);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < widths[i] + 2; j++) fputs(ICON("─","-"),stdout);
        fputs(i==n-1 ? ICON("┐", "+") : ICON("┬", "+"), stdout);
    }
    putchar('\n');
}
static void print_mid(const int widths[], int n)
{
    fputs(ICON("├", "+"), stdout);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < widths[i] + 2; j++) fputs(ICON("─","-"),stdout);
        fputs(i==n-1 ? ICON("┤", "+") : ICON("┼", "+"), stdout);
    }
    putchar('\n');
}
static void print_bot(const int widths[], int n)
{
    fputs(ICON("└", "+"), stdout);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < widths[i] + 2; j++) fputs(ICON("─","-"),stdout);
        fputs(i==n-1 ? ICON("┘", "+") : ICON("┴", "+"), stdout);
    }
    putchar('\n');
}

static void print_cell(const char *s, int w, const char *color)
{
    int dw = utf8_dwidth(s);
    int pad = w - dw; if (pad < 0) pad = 0;
    fputs(ICON("│", "|"), stdout);
    putchar(' ');
    if (color) fputs(color, stdout);
    fputs(s, stdout);
    if (color) fputs(UI_RESET, stdout);
    for (int i = 0; i < pad; i++) putchar(' ');
    putchar(' ');
}

void ui_table_header(const char *cols[], const int widths[], int n)
{
    print_top(widths, n);
    for (int i = 0; i < n; i++) {
        int w = widths[i], dw = utf8_dwidth(cols[i]);
        int pad = w - dw; if (pad < 0) pad = 0;
        fputs(ICON("│", "|"), stdout);
        printf(" %s%s%s%s", UI_BOLD, UI_BCYAN, cols[i], UI_RESET);
        for (int j = 0; j < pad; j++) putchar(' ');
        putchar(' ');
    }
    fputs(ICON("│", "|"), stdout); putchar('\n');
    print_mid(widths, n);
}

void ui_table_sep(const int widths[], int n)
{
    print_bot(widths, n);
}

void ui_table_row(const char *cells[], const int widths[], int n,
                  const char *row_color)
{
    for (int i = 0; i < n; i++) print_cell(cells[i], widths[i], row_color);
    fputs(ICON("│", "|"), stdout); putchar('\n');
}
