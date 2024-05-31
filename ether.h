#ifndef ETHER_H
#define ETHER_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#include "net.h"

#ifndef ETHER_ADDR_LEN
#define ETHER_ADDR_LEN 6
#endif
#define ETHER_ADDR_STR_LEN 18 /* "xx:xx:xx:xx:xx:xx\0" */

#define ETHER_HDR_SIZE 14
#define ETHER_FRAME_SIZE_MIN   60 /* without FCS */
#define ETHER_FRAME_SIZE_MAX 1514 /* without FCS */
#define ETHER_PAYLOAD_SIZE_MIN (ETHER_FRAME_SIZE_MIN - ETHER_HDR_SIZE)
#define ETHER_PAYLOAD_SIZE_MAX (ETHER_FRAME_SIZE_MAX - ETHER_HDR_SIZE)

/*
 * Ethernet types
 *  - see https://www.iana.org/assignments/ieee-802-numbers/ieee-802-numbers.txt
 */
#define ETHER_TYPE_IP   0x0800
#define ETHER_TYPE_ARP  0x0806
#define ETHER_TYPE_IPV6 0x86dd

struct ether_hdr {
    uint8_t dst[ETHER_ADDR_LEN];
    uint8_t src[ETHER_ADDR_LEN];
    uint16_t type;
};

extern const uint8_t ETHER_ADDR_ANY[ETHER_ADDR_LEN];
extern const uint8_t ETHER_ADDR_BROADCAST[ETHER_ADDR_LEN];

extern int
ether_addr_pton(const char *p, uint8_t *n);
extern char *
ether_addr_ntop(const uint8_t *n, char *p, size_t size);
extern void
ether_print(const uint8_t *frame, size_t flen);

#endif
