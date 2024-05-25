#include <stdint.h>
#include <stddef.h>

#include "util.h"
#include "ip.h"
#include "icmp.h"

#define icmp_type com.type
#define icmp_code com.code
#define icmp_sum  com.sum

struct icmp_common {
    uint8_t type;
    uint8_t code;
    uint16_t sum;
};

struct icmp_hdr {
    struct icmp_common com;
    uint32_t dep; /* message dependent field*/
};

struct icmp_echo {
    struct icmp_common com;
    uint16_t id;
    uint16_t seq;
};

struct icmp_dest_unreach {
    struct icmp_common com;
    uint32_t unused; /* zero */
};

static char *
icmp_type_ntoa(uint8_t type)
{
}

static void
icmp_print(const uint8_t *data, size_t len)
{
}

static void
icmp_input(const struct ip_hdr *iphdr, const uint8_t *data, size_t len, struct ip_iface *iface)
{
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];

    debugf("%s => %s, len=%zu",
        ip_addr_ntop(iphdr->src, addr1, sizeof(addr1)),
        ip_addr_ntop(iphdr->dst, addr2, sizeof(addr2)), len);
    debugdump(data, len);
}

int
icmp_init(void)
{
    if (ip_protocol_register(IP_PROTOCOL_ICMP, icmp_input) == -1) {
        errorf("ip_protocol_register() failure");
        return -1;
    }
    return 0;
}
