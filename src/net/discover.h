/*
 * discover.h — LAN host identity discovery (ARP / NBNS / mDNS).
 */
#ifndef SOCKETTOOL_DISCOVER_H
#define SOCKETTOOL_DISCOVER_H

#include <stddef.h>

/*
 * Read /proc/net/arp and copy the MAC for `ip` (IPv4 dotted) into
 * out[outlen]. Returns 0 on success, -1 if not present.
 *
 * Requires that something has recently sent traffic to `ip` (e.g. our ICMP
 * ping) so the kernel's ARP table has an entry.
 */
int arp_lookup(const char *ip, char *out, size_t outlen);

/*
 * NetBIOS Name Service node-status query (UDP 137).
 * Returns 0 + writes the machine's NetBIOS name (Windows / Samba host name)
 * into out[outlen]. -1 on timeout or non-NBNS host.
 */
int nbns_lookup(const char *ip, int timeout_ms, char *out, size_t outlen);

/*
 * Multicast DNS reverse lookup. We unicast a PTR query for
 * `<a.b.c.d>.in-addr.arpa.` to UDP 5353 of the target IP itself
 * (per RFC 6762 §5.5 unicast queries). 0 + sets out on success.
 */
int mdns_lookup(const char *ip, int timeout_ms, char *out, size_t outlen);

#endif
