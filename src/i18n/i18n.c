/*
 * i18n.c — string tables for English and Chinese.
 */
#include <stdlib.h>
#include <string.h>
#include "i18n/i18n.h"

#if defined(ST_LANG_ZH)
static enum st_lang g_lang = ST_ZH;
#else
static enum st_lang g_lang = ST_EN;
#endif

static const char *en[T_MAX] = {
    [T_HELP]              = "show this help",
    [T_USAGE]             = "Usage",
    [T_OPTIONS]           = "Options",
    [T_REQUIRED]          = "required",
    [T_DEFAULT]           = "default",
    [T_TARGET]            = "target",
    [T_TIMEOUT_MS]        = "timeout",
    [T_LISTEN]            = "listen",
    [T_MODE]              = "mode",
    [T_ECHO]              = "echo",
    [T_DISCARD]           = "discard",
    [T_COUNT]             = "count",
    [T_INTERVAL]          = "interval",
    [T_SUMMARY]           = "Summary",
    [T_SENT]              = "sent",
    [T_OK_LBL]            = "ok",
    [T_LOST]              = "lost",
    [T_FAIL_LBL]          = "fail",
    [T_ELAPSED]           = "elapsed",
    [T_HOSTS]             = "hosts",
    [T_TARGETS]           = "targets",
    [T_JOBS]              = "jobs",
    [T_HOST]              = "host",
    [T_RESULT]            = "result",
    [T_RTT]               = "rtt(ms)",
    [T_INFO]              = "info",
    [T_PROTO]             = "proto",

    [T_TITLE]             = "socketTool — multi-protocol network test toolkit",
    [T_USAGE_LINE_1]      = "Usage: socketTool <applet> [options]",
    [T_USAGE_LINE_2]      = "   or: <applet> [options]   (when invoked via symlink)",
    [T_AVAILABLE_APPLETS] = "Available applets:",
    [T_APPLET_HELP_HINT]  = "Run 'socketTool <applet> -h' for applet help.",
    [T_UNKNOWN_APPLET]    = "unknown applet: %s",
    [T_VERSION_STR]       = "socketTool 0.2.0",

    [T_CONNECTED_IN]      = "connected in %ld ms",
    [T_SENT_BYTES]        = "sent %zu bytes (#%d)",
    [T_INTERACTIVE_MODE]  = "entering interactive mode (Ctrl-C to quit)",
    [T_WAITING_CLIENTS]   = "waiting for clients (Ctrl-C to quit)",
    [T_WAITING_DGRAMS]    = "waiting for datagrams (Ctrl-C to quit)",
    [T_ACCEPTED]          = "accepted %s:%s",
    [T_CLOSED]            = "closed %s:%s",
    [T_NO_REPLY]          = "#%d  no reply within %d ms",
    [T_REPLY]             = "#%d  %ld ms  reply: %.*s",
    [T_HANDSHAKE_OK]      = "handshake complete",
    [T_SERVER_CLOSED]     = "server closed",
    [T_BAD_TARGET_SKIP]   = "skip bad target: %s",

    [T_E_CONNECT]         = "connect failed: %s",
    [T_E_LISTEN]          = "listen failed",
    [T_E_BIND]            = "bind failed: %s",
    [T_E_SEND]            = "send: %s",
    [T_E_RECV]            = "recv: %s",
    [T_E_OPEN]            = "open %s: %s",
    [T_E_HANDSHAKE]       = "handshake failed",
    [T_E_BAD_MODE]        = "bad mode: %s",
    [T_E_BAD_PROTO]       = "bad proto: %s",
    [T_E_BAD_RANGE]       = "bad host/range: %s",

    [T_BATCH_PING]        = "Batch ping",
    [T_BATCH_TEST]        = "Batch connectivity test",

    [T_S_TCP_CLIENT]      = "TCP client: connect & exchange data",
    [T_S_TCP_SERVER]      = "TCP server: listen & echo",
    [T_S_UDP_CLIENT]      = "UDP client: send datagrams",
    [T_S_UDP_SERVER]      = "UDP server: receive datagrams",
    [T_S_WS_CLIENT]       = "WebSocket client",
    [T_S_WS_SERVER]       = "WebSocket server",
    [T_S_BPING]           = "Batch ping (range/CIDR/list supported)",
    [T_S_BTEST]           = "Batch protocol connectivity test",

    [T_IP]                = "ip",
    [T_HOSTNAME]          = "hostname",
    [T_TTL]               = "ttl",
};

static const char *zh[T_MAX] = {
    [T_HELP]              = "显示帮助",
    [T_USAGE]             = "用法",
    [T_OPTIONS]           = "选项",
    [T_REQUIRED]          = "必填",
    [T_DEFAULT]           = "默认",
    [T_TARGET]            = "目标",
    [T_TIMEOUT_MS]        = "超时",
    [T_LISTEN]            = "监听",
    [T_MODE]              = "模式",
    [T_ECHO]              = "回显",
    [T_DISCARD]           = "丢弃",
    [T_COUNT]             = "次数",
    [T_INTERVAL]          = "间隔",
    [T_SUMMARY]           = "汇总",
    [T_SENT]              = "已发送",
    [T_OK_LBL]            = "成功",
    [T_LOST]              = "丢失",
    [T_FAIL_LBL]          = "失败",
    [T_ELAPSED]           = "耗时",
    [T_HOSTS]             = "主机数",
    [T_TARGETS]           = "目标数",
    [T_JOBS]              = "并发",
    [T_HOST]              = "主机",
    [T_RESULT]            = "结果",
    [T_RTT]               = "时延(ms)",
    [T_INFO]              = "信息",
    [T_PROTO]             = "协议",

    [T_TITLE]             = "socketTool — 多协议网络测试工具",
    [T_USAGE_LINE_1]      = "用法: socketTool <子命令> [选项]",
    [T_USAGE_LINE_2]      = "  或: <子命令> [选项]   (通过软链接调用时)",
    [T_AVAILABLE_APPLETS] = "可用子命令:",
    [T_APPLET_HELP_HINT]  = "运行 'socketTool <子命令> -h' 查看子命令帮助。",
    [T_UNKNOWN_APPLET]    = "未知子命令: %s",
    [T_VERSION_STR]       = "socketTool 0.2.0",

    [T_CONNECTED_IN]      = "已连接，耗时 %ld ms",
    [T_SENT_BYTES]        = "已发送 %zu 字节 (第 %d 次)",
    [T_INTERACTIVE_MODE]  = "进入交互模式 (Ctrl-C 退出)",
    [T_WAITING_CLIENTS]   = "等待客户端连接 (Ctrl-C 退出)",
    [T_WAITING_DGRAMS]    = "等待数据报 (Ctrl-C 退出)",
    [T_ACCEPTED]          = "接受连接 %s:%s",
    [T_CLOSED]            = "关闭连接 %s:%s",
    [T_NO_REPLY]          = "#%d  在 %d ms 内无回复",
    [T_REPLY]             = "#%d  %ld ms  收到: %.*s",
    [T_HANDSHAKE_OK]      = "握手完成",
    [T_SERVER_CLOSED]     = "服务端已关闭",
    [T_BAD_TARGET_SKIP]   = "跳过非法目标: %s",

    [T_E_CONNECT]         = "连接失败: %s",
    [T_E_LISTEN]          = "监听失败",
    [T_E_BIND]            = "绑定失败: %s",
    [T_E_SEND]            = "发送失败: %s",
    [T_E_RECV]            = "接收失败: %s",
    [T_E_OPEN]            = "打开 %s 失败: %s",
    [T_E_HANDSHAKE]       = "握手失败",
    [T_E_BAD_MODE]        = "非法模式: %s",
    [T_E_BAD_PROTO]       = "非法协议: %s",
    [T_E_BAD_RANGE]       = "非法主机/范围: %s",

    [T_BATCH_PING]        = "批量 ping",
    [T_BATCH_TEST]        = "批量连通性测试",

    [T_S_TCP_CLIENT]      = "TCP 客户端：连接并收发数据",
    [T_S_TCP_SERVER]      = "TCP 服务端：监听并回显",
    [T_S_UDP_CLIENT]      = "UDP 客户端：发送数据报",
    [T_S_UDP_SERVER]      = "UDP 服务端：接收数据报",
    [T_S_WS_CLIENT]       = "WebSocket 客户端",
    [T_S_WS_SERVER]       = "WebSocket 服务端",
    [T_S_BPING]           = "批量 ping (支持范围 / CIDR / 列表)",
    [T_S_BTEST]           = "批量协议连通性测试",

    [T_IP]                = "IP",
    [T_HOSTNAME]          = "主机名",
    [T_TTL]               = "TTL",
};

void i18n_init(void)
{
    const char *e = getenv("ST_LANG");
    if (!e || !*e) e = getenv("LANG");
    if (!e) return;
    if (e[0] == 'z' && e[1] == 'h') g_lang = ST_ZH;
    else if (e[0] == 'e' && e[1] == 'n') g_lang = ST_EN;
}

void i18n_set(enum st_lang lang) { g_lang = lang; }
enum st_lang i18n_get(void)      { return g_lang; }

const char *T(int key)
{
    if (key < 0 || key >= T_MAX) return "";
    const char *s = (g_lang == ST_ZH) ? zh[key] : en[key];
    if (!s) s = en[key];                           /* fallback to en */
    return s ? s : "";
}
