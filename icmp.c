#include <stdint.h>
#include <stddef.h>

#include "util.h"
#include "ip.h"
#include "icmp.h"

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
