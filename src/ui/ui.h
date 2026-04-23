/*
 * ui.h — terminal UI helpers (colors, banners, status lines, icons).
 */
#ifndef SOCKETTOOL_UI_H
#define SOCKETTOOL_UI_H

#include <stdio.h>

extern int ui_color_enabled;
extern int ui_unicode_enabled;        /* Unicode glyphs vs ASCII fallback */

#define _UI_C(seq) (ui_color_enabled ? (seq) : "")

/* base attributes */
#define UI_RESET        _UI_C("\033[0m")
#define UI_BOLD         _UI_C("\033[1m")
#define UI_DIM          _UI_C("\033[2m")
#define UI_ITALIC       _UI_C("\033[3m")
#define UI_UNDER        _UI_C("\033[4m")

/* foreground - normal */
#define UI_RED          _UI_C("\033[31m")
#define UI_GREEN        _UI_C("\033[32m")
#define UI_YELLOW       _UI_C("\033[33m")
#define UI_BLUE         _UI_C("\033[34m")
#define UI_MAGENTA      _UI_C("\033[35m")
#define UI_CYAN         _UI_C("\033[36m")
#define UI_GRAY         _UI_C("\033[90m")
#define UI_WHITE        _UI_C("\033[97m")

/* foreground - bright */
#define UI_BRED         _UI_C("\033[91m")
#define UI_BGREEN       _UI_C("\033[92m")
#define UI_BYELLOW      _UI_C("\033[93m")
#define UI_BBLUE        _UI_C("\033[94m")
#define UI_BMAGENTA     _UI_C("\033[95m")
#define UI_BCYAN        _UI_C("\033[96m")

/* background */
#define UI_BG_BLUE      _UI_C("\033[44m")
#define UI_BG_MAGENTA   _UI_C("\033[45m")

/* icon glyphs (Unicode preferred, ASCII fallback) */
const char *ui_icon_ok(void);
const char *ui_icon_fail(void);
const char *ui_icon_warn(void);
const char *ui_icon_info(void);
const char *ui_icon_arrow(void);
const char *ui_icon_bullet(void);
const char *ui_icon_hourglass(void);
const char *ui_icon_rocket(void);
const char *ui_icon_recv(void);
const char *ui_icon_send(void);
const char *ui_icon_target(void);
const char *ui_icon_globe(void);

void ui_init(void);
void ui_title(const char *title);
void ui_section(const char *icon, const char *title);
void ui_ok(const char *fmt, ...);
void ui_warn(const char *fmt, ...);
void ui_err(const char *fmt, ...);
void ui_info(const char *fmt, ...);
void ui_kv(const char *key, const char *fmt, ...);

void ui_progress(int done, int total, const char *label);
void ui_progress_done(void);

void ui_table_header(const char *cols[], const int widths[], int n);
void ui_table_row(const char *cells[], const int widths[], int n,
                  const char *row_color);
void ui_table_sep(const int widths[], int n);

#endif
