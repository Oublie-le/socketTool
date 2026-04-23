/*
 * ui.c — terminal UI helpers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "ui/ui.h"

int ui_color_enabled = 1;

void ui_init(void)
{
    const char *no_color = getenv("NO_COLOR");
    if (no_color && *no_color) {
        ui_color_enabled = 0;
        return;
    }
    if (!isatty(STDOUT_FILENO))
        ui_color_enabled = 0;
}

void ui_title(const char *title)
{
    size_t n = strlen(title);
    printf("\n%s%s╔", UI_BOLD, UI_CYAN);
    for (size_t i = 0; i < n + 2; i++) printf("═");
    printf("╗\n║ %s ║\n╚", title);
    for (size_t i = 0; i < n + 2; i++) printf("═");
    printf("╝%s\n", UI_RESET);
}

void ui_section(const char *title)
{
    printf("\n%s%s▶ %s%s\n", UI_BOLD, UI_BLUE, title, UI_RESET);
}

static void ui_vprintf(const char *prefix, const char *color,
                       const char *fmt, va_list ap)
{
    fprintf(stdout, "%s%s%s ", color, prefix, UI_RESET);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
}

void ui_ok(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    ui_vprintf("[ OK ]", UI_GREEN, fmt, ap);
    va_end(ap);
}

void ui_warn(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    ui_vprintf("[WARN]", UI_YELLOW, fmt, ap);
    va_end(ap);
}

void ui_err(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "%s[FAIL]%s ", UI_RED, UI_RESET);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void ui_info(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    ui_vprintf("[INFO]", UI_CYAN, fmt, ap);
    va_end(ap);
}

void ui_kv(const char *key, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("  %s%-14s%s ", UI_DIM, key, UI_RESET);
    vprintf(fmt, ap);
    putchar('\n');
    va_end(ap);
}

static void print_hline(const int widths[], int n)
{
    putchar('+');
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < widths[i] + 2; j++) putchar('-');
        putchar('+');
    }
    putchar('\n');
}

void ui_table_header(const char *cols[], const int widths[], int n)
{
    print_hline(widths, n);
    printf("%s|", UI_BOLD);
    for (int i = 0; i < n; i++)
        printf(" %-*s |", widths[i], cols[i]);
    printf("%s\n", UI_RESET);
    print_hline(widths, n);
}

void ui_table_sep(const int widths[], int n)
{
    print_hline(widths, n);
}

void ui_table_row(const char *cells[], const int widths[], int n,
                  const char *row_color)
{
    printf("|");
    for (int i = 0; i < n; i++) {
        if (row_color)
            printf(" %s%-*s%s |", row_color, widths[i], cells[i], UI_RESET);
        else
            printf(" %-*s |", widths[i], cells[i]);
    }
    putchar('\n');
}
