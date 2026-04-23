/*
 * tests/unit_range.c — unit tests for host_range_expand.
 * Compiled by tests/run_all.sh and executed standalone.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "net/net.h"

static int failures = 0;

#define EXPECT(cond, msg) do {                                     \
    if (!(cond)) { printf("  ✘ %s\n", msg); failures++; }          \
    else         { printf("  ✔ %s\n", msg); }                      \
} while (0)

int main(void)
{
    char **list = NULL;
    int n;

    n = host_range_expand("127.0.0.1", &list, 64);
    EXPECT(n == 1 && strcmp(list[0], "127.0.0.1") == 0, "single host");
    host_list_free(list, n);

    n = host_range_expand("10.0.0.1-3", &list, 64);
    EXPECT(n == 3 && strcmp(list[0], "10.0.0.1") == 0
                  && strcmp(list[2], "10.0.0.3") == 0, "last-octet range 1-3");
    host_list_free(list, n);

    n = host_range_expand("10.0.0.5-10.0.0.7", &list, 64);
    EXPECT(n == 3 && strcmp(list[0], "10.0.0.5") == 0
                  && strcmp(list[2], "10.0.0.7") == 0, "full IPv4 range");
    host_list_free(list, n);

    /* /30: net=.0 bcast=.3 -> usable .1, .2 (2 hosts) */
    n = host_range_expand("192.168.1.0/30", &list, 64);
    EXPECT(n == 2 && strcmp(list[0], "192.168.1.1") == 0
                  && strcmp(list[1], "192.168.1.2") == 0, "CIDR /30 usable");
    host_list_free(list, n);

    /* /32: single host kept */
    n = host_range_expand("8.8.8.8/32", &list, 64);
    EXPECT(n == 1 && strcmp(list[0], "8.8.8.8") == 0, "CIDR /32 single");
    host_list_free(list, n);

    /* over-cap: /24 with cap=10 must error */
    n = host_range_expand("10.0.0.0/24", &list, 10);
    EXPECT(n < 0, "over-cap returns -1");

    /* malformed input */
    n = host_range_expand("999.0.0.1", &list, 64);
    /* hostnames pass through; "999.0.0.1" is not parsed as range/CIDR
       so it falls through to single-host append (DNS would later fail). */
    EXPECT(n == 1, "non-IP string passes through as hostname");
    host_list_free(list, n);

    n = host_range_expand("10.0.0.5-10.0.0.1", &list, 64);
    EXPECT(n < 0, "reversed range rejected");

    if (failures == 0) {
        printf("  ✔ all %d range-expansion checks passed\n", 8);
        return 0;
    }
    printf("  ✘ %d range-expansion check(s) failed\n", failures);
    return 1;
}
