/*
 * icmp_compat.h — platform abstraction for ICMP structures.
 *
 * Linux uses struct icmphdr (from <netinet/icmp.h>)
 * macOS/Darwin uses struct icmp (from <netinet/ip_icmp.h>)
 *
 * Key platform difference:
 * - Linux: SOCK_DGRAM ICMP socket strips IP header from received packets
 * - macOS: SOCK_DGRAM ICMP socket includes IP header in received packets
 */

#ifndef SOCKETTOOL_ICMP_COMPAT_H
#define SOCKETTOOL_ICMP_COMPAT_H

#include <stdint.h>

#if defined(__APPLE__) && defined(__MACH__)
/* macOS / Darwin */
#include <netinet/ip_icmp.h>

#define ICMP_TYPE icmp_type
#define ICMP_CODE icmp_code
#define ICMP_CKSUM icmp_cksum
#define ICMP_ID icmp_hun.ih_idseq.icd_id
#define ICMP_SEQ icmp_hun.ih_idseq.icd_seq

typedef struct icmp icmp_hdr_t;

/* macOS DGRAM socket includes IP header in receive */
#define ICMP_DGRAM_INCLUDES_IP_HEADER 1

#else
/* Linux */
#include <netinet/icmp.h>
#include <netinet/ip_icmp.h>

#define ICMP_TYPE type
#define ICMP_CODE code
#define ICMP_CKSUM checksum
#define ICMP_ID un.echo.id
#define ICMP_SEQ un.echo.sequence

typedef struct icmphdr icmp_hdr_t;

/* Linux DGRAM socket strips IP header from receive */
#define ICMP_DGRAM_INCLUDES_IP_HEADER 0

#endif

#endif /* SOCKETTOOL_ICMP_COMPAT_H */
