/*
 * diag.c — local network self-check.
 *
 * Reports: interfaces (name/IPv4/MTU), default gateway, DNS resolvers,
 *          and a few connectivity probes (gateway TCP connect-check and
 *          public DNS reachability). Read-only: uses /proc and libc only.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <ifaddrs.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "core/applet.h"
#include "ui/ui.h"
#include "net/net.h"
#include "i18n/i18n.h"

static void diag_help(void)
{
    printf("Usage: diag [options]\n"
           "  -h, --help            show this help\n"
           "\n"
           "Reports local interfaces / gateway / DNS / MTU / basic reachability.\n");
}

static void show_interfaces(void)
{
    ui_section(ui_icon_globe(), "Interfaces");
    struct ifaddrs *ifa, *it;
    if (getifaddrs(&ifa) < 0) {
        ui_err("getifaddrs: %s", strerror(errno));
        return;
    }
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    for (it = ifa; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        struct sockaddr_in *sa = (struct sockaddr_in *)it->ifa_addr;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));

        int mtu = 0;
        if (s >= 0) {
            struct ifreq r; memset(&r, 0, sizeof(r));
            snprintf(r.ifr_name, IFNAMSIZ, "%s", it->ifa_name);
            if (ioctl(s, SIOCGIFMTU, &r) == 0) mtu = r.ifr_mtu;
        }
        int up = (it->ifa_flags & IFF_UP)      ? 1 : 0;
        int lo = (it->ifa_flags & IFF_LOOPBACK)? 1 : 0;
        ui_kv(it->ifa_name, "%s%-15s%s  mtu=%-5d %s%s%s",
              UI_BCYAN, ip, UI_RESET, mtu,
              up ? UI_BGREEN : UI_BYELLOW,
              lo ? "loopback" : (up ? "up" : "down"),
              UI_RESET);
    }
    if (s >= 0) close(s);
    freeifaddrs(ifa);
}

static int read_default_gateway(char out[64])
{
    FILE *fp = fopen("/proc/net/route", "r");
    if (!fp) return -1;
    char line[512]; int found = 0;
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return -1; }
    while (fgets(line, sizeof(line), fp)) {
        char iface[32]; unsigned long dest, gw, flags;
        if (sscanf(line, "%31s %lx %lx %lx", iface, &dest, &gw, &flags) < 4) continue;
        if (dest != 0) continue;
        struct in_addr a; a.s_addr = (in_addr_t)gw;
        snprintf(out, 64, "%s via %s", inet_ntoa(a), iface);
        found = 1; break;
    }
    fclose(fp);
    return found ? 0 : -1;
}

static void show_gateway(void)
{
    ui_section(ui_icon_target(), "Default route");
    char gw[64];
    if (read_default_gateway(gw) == 0) {
        ui_kv("gateway", "%s%s%s", UI_BCYAN, gw, UI_RESET);
    } else {
        ui_warn("no default route found");
    }
}

static void show_dns(void)
{
    ui_section(ui_icon_bullet(), "Resolvers");
    FILE *fp = fopen("/etc/resolv.conf", "r");
    if (!fp) { ui_warn("cannot read /etc/resolv.conf: %s", strerror(errno)); return; }
    char line[512]; int n = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "nameserver", 10) != 0) continue;
        char *p = line + 10;
        while (*p == ' ' || *p == '\t') p++;
        size_t l = strlen(p);
        while (l > 0 && (p[l-1] == '\n' || p[l-1] == '\r' || p[l-1] == ' ')) p[--l] = 0;
        if (!*p) continue;
        ui_kv("nameserver", "%s%s%s", UI_BCYAN, p, UI_RESET);
        n++;
    }
    fclose(fp);
    if (!n) ui_warn("no nameservers configured");
}

static void probe_tcp(const char *label, const char *host, const char *port, int ms)
{
    long t0 = now_ms();
    int fd = tcp_connect(host, port, ms);
    long rtt = now_ms() - t0;
    if (fd >= 0) {
        close(fd);
        ui_ok("%s %s%s:%s%s  %ld ms", label, UI_BCYAN, host, port, UI_RESET, rtt);
    } else {
        ui_warn("%s %s:%s  %s", label, host, port, strerror(errno));
    }
}

static void show_probes(void)
{
    ui_section(ui_icon_rocket(), "Probes");
    char gw[64];
    if (read_default_gateway(gw) == 0) {
        char gwip[64]; snprintf(gwip, sizeof(gwip), "%s", gw);
        char *sp = strchr(gwip, ' '); if (sp) *sp = 0;
        probe_tcp("gateway:80   ", gwip, "80", 800);
    }
    probe_tcp("dns (1.1.1.1) ", "1.1.1.1", "53", 1500);
    probe_tcp("dns (8.8.8.8) ", "8.8.8.8", "53", 1500);
    probe_tcp("https check   ", "1.1.1.1", "443", 1500);
}

int diag_main(int argc, char **argv)
{
    static struct option opts[] = {
        {"help", 0, 0, 'h'}, {0,0,0,0},
    };
    int c;
    while ((c = getopt_long(argc, argv, "h", opts, NULL)) != -1) {
        switch (c) {
        case 'h': diag_help(); return 0;
        default:  diag_help(); return 1;
        }
    }

    show_interfaces();
    show_gateway();
    show_dns();
    show_probes();
    return 0;
}
