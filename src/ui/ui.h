/*
 * ui.h — terminal UI helpers (colors, banners, status lines).
 */
#ifndef SOCKETTOOL_UI_H
#define SOCKETTOOL_UI_H

#include <stdio.h>

extern int ui_color_enabled;

#define _UI_C(seq) (ui_color_enabled ? (seq) : "")

#define UI_RESET   _UI_C("\033[0m")
#define UI_BOLD    _UI_C("\033[1m")
#define UI_DIM     _UI_C("\033[2m")
#define UI_RED     _UI_C("\033[31m")
#define UI_GREEN   _UI_C("\033[32m")
#define UI_YELLOW  _UI_C("\033[33m")
#define UI_BLUE    _UI_C("\033[34m")
#define UI_MAGENTA _UI_C("\033[35m")
#define UI_CYAN    _UI_C("\033[36m")
#define UI_GRAY    _UI_C("\033[90m")

void ui_init(void);
void ui_title(const char *title);
void ui_section(const char *title);
void ui_ok(const char *fmt, ...);
void ui_warn(const char *fmt, ...);
void ui_err(const char *fmt, ...);
void ui_info(const char *fmt, ...);
void ui_kv(const char *key, const char *fmt, ...);

void ui_table_header(const char *cols[], const int widths[], int n);
void ui_table_row(const char *cells[], const int widths[], int n,
                  const char *row_color);
void ui_table_sep(const int widths[], int n);

#endif
