/*
 * icmp_compat.h — platform abstraction for ICMP structures.
 *
 * Linux uses struct icmphdr (from <netinet/icmp.h>)
 * macOS/Darwin uses struct icmp (from <netinet/ip_icmp.h>)
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

#endif

#endif /* SOCKETTOOL_ICMP_COMPAT_H */
