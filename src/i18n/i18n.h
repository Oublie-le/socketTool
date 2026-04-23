/*
 * i18n.h — internationalization (English / Chinese).
 *
 * Compile-time default chosen via -DST_LANG_ZH or -DST_LANG_EN.
 * Runtime override via env ST_LANG=zh|en or `--lang zh|en` CLI flag.
 *
 * Each translatable string has a stable enum key. Use T(K) to look up.
 */
#ifndef SOCKETTOOL_I18N_H
#define SOCKETTOOL_I18N_H

enum st_lang { ST_EN = 0, ST_ZH = 1 };

void i18n_init(void);                /* read env ST_LANG */
void i18n_set(enum st_lang lang);    /* runtime override */
enum st_lang i18n_get(void);

const char *T(int key);

/* ---- string keys ----
 * Keep stable; new keys must be appended before T_MAX.
 */
enum {
    /* generic */
    T_HELP = 0,
    T_USAGE,
    T_OPTIONS,
    T_REQUIRED,
    T_DEFAULT,
    T_TARGET,
    T_TIMEOUT_MS,
    T_LISTEN,
    T_MODE,
    T_ECHO,
    T_DISCARD,
    T_COUNT,
    T_INTERVAL,
    T_SUMMARY,
    T_SENT,
    T_OK_LBL,
    T_LOST,
    T_FAIL_LBL,
    T_ELAPSED,
    T_HOSTS,
    T_TARGETS,
    T_JOBS,
    T_HOST,
    T_RESULT,
    T_RTT,
    T_INFO,
    T_PROTO,

    /* main / dispatch */
    T_TITLE,
    T_USAGE_LINE_1,
    T_USAGE_LINE_2,
    T_AVAILABLE_APPLETS,
    T_APPLET_HELP_HINT,
    T_UNKNOWN_APPLET,
    T_VERSION_STR,

    /* status messages */
    T_CONNECTED_IN,
    T_SENT_BYTES,
    T_INTERACTIVE_MODE,
    T_WAITING_CLIENTS,
    T_WAITING_DGRAMS,
    T_ACCEPTED,
    T_CLOSED,
    T_NO_REPLY,
    T_REPLY,
    T_HANDSHAKE_OK,
    T_SERVER_CLOSED,
    T_BAD_TARGET_SKIP,

    /* errors */
    T_E_CONNECT,
    T_E_LISTEN,
    T_E_BIND,
    T_E_SEND,
    T_E_RECV,
    T_E_OPEN,
    T_E_HANDSHAKE,
    T_E_BAD_MODE,
    T_E_BAD_PROTO,
    T_E_BAD_RANGE,

    /* batch sections */
    T_BATCH_PING,
    T_BATCH_TEST,

    /* applet summaries (shown in main help) */
    T_S_TCP_CLIENT, T_S_TCP_SERVER,
    T_S_UDP_CLIENT, T_S_UDP_SERVER,
    T_S_WS_CLIENT,  T_S_WS_SERVER,
    T_S_BPING,      T_S_BTEST,

    T_MAX
};

#endif
