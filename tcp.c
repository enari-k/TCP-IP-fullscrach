#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "ip.h"
#include "tcp.h"

#define TCP_FLG_FIN 0x01
#define TCP_FLG_SYN 0x02
#define TCP_FLG_RST 0x04
#define TCP_FLG_PSH 0x08
#define TCP_FLG_ACK 0x10
#define TCP_FLG_URG 0x20

#define TCP_FLG_IS(x, y) ((x & 0x3f) == (y))
#define TCP_FLG_ISSET(x, y) ((x & 0x3f) & (y) ? 1 : 0)

struct pseudo_hdr {
    uint32_t src;
    uint32_t dst;
    uint8_t zero;
    uint8_t protocol;
    uint16_t len;
};

struct tcp_hdr {
    uint16_t src;
    uint16_t dst;
    uint32_t seq;
    uint32_t ack;
    uint8_t off;
    uint8_t flg;
    uint16_t wnd;
    uint16_t sum;
    uint16_t up;
};

static char *
tcp_flg_ntoa(uint8_t flg)
{
    static char str[9];

    snprintf(str, sizeof(str), "--%c%c%c%c%c%c",
        TCP_FLG_ISSET(flg, TCP_FLG_URG) ? 'U' : '-',
        TCP_FLG_ISSET(flg, TCP_FLG_ACK) ? 'A' : '-',
        TCP_FLG_ISSET(flg, TCP_FLG_PSH) ? 'P' : '-',
        TCP_FLG_ISSET(flg, TCP_FLG_RST) ? 'R' : '-',
        TCP_FLG_ISSET(flg, TCP_FLG_SYN) ? 'S' : '-',
        TCP_FLG_ISSET(flg, TCP_FLG_FIN) ? 'F' : '-');
    return str;
}

static char *
tcp_opt_ntoa(uint8_t opt)
{
    switch (opt) {
    case 0:
        return "End of Option List (EOL)";
    case 1:
        return "No-Operation (NOP)";
    case 2:
        return "Maximum Segment Size (MSS)";
    case 3:
        return "Window Scale";
    case 4:
        return "SACK Permitted";
    case 5:
        return "SACK";
    case 8:
        return "Timestamps";
    default:
        return "Unknown";
    }
}

static void
tcp_print(const uint8_t *data, size_t len)
{
    struct tcp_hdr *hdr;
    uint8_t hlen, *opt;
    int i = 0;

    flockfile(stderr);
    hdr = (struct tcp_hdr *)data;
    fprintf(stderr, "        src: %u\n", ntoh16(hdr->src));
    fprintf(stderr, "        dst: %u\n", ntoh16(hdr->dst));
    fprintf(stderr, "        seq: %u\n", ntoh16(hdr->seq));
    fprintf(stderr, "        ack: %u\n", ntoh16(hdr->ack));
    hlen = (hdr->off >> 4) << 2;
    fprintf(stderr, "        off: 0x%02x (%u) (options: %ld, payload: %ld)\n",
        hdr->off, hlen, hlen - sizeof(*hdr), len - hlen);
    fprintf(stderr, "        flg: 0x%02x (%s)\n", hdr->flg, tcp_flg_ntoa(hdr->flg));
    fprintf(stderr, "        wnd: %u\n", ntoh16(hdr->wnd));
    fprintf(stderr, "        sum: 0x%04x\n", ntoh16(hdr->sum));
    fprintf(stderr, "         up: %u\n", ntoh16(hdr->up));
    opt = (uint8_t *)(hdr + 1);
    while (opt < (uint8_t *)hdr + hlen)
    {
        if (*opt == 0)
        {
            fprintf(stderr, "    opt[%d]: kind=%u (%s)\n",
                i++, *opt, tcp_opt_ntoa(*opt));
            break;
        }
        if (*opt == 1)
        {
            fprintf(stderr, "    opt[%d]: kind=%u (%s)\n",
                i++, *opt, tcp_opt_ntoa(*opt));
            opt++;
        }
        else
        {
            fprintf(stderr, "    opt[%d]: kind=%u (%s), len=%u\n",
                i++, *opt, tcp_opt_ntoa(*opt), *(opt + 1));
            opt += *(opt + 1);
        }
    }
#ifdef HEXDUMP
    hexdump(stderr, data, len);
#endif
    funlockfile(stderr);
}

static void
tcp_input(const struct ip_hdr *iphdr, const uint8_t *data, size_t len, struct ip_iface *iface)
{
    struct tcp_hdr *hdr;
    struct pseudo_hdr pseudo;
    uint16_t psum;
    ip_endp_t src, dst;
    char ep1[IP_ENDP_STR_LEN];
    char ep2[IP_ENDP_STR_LEN];

    if (len < sizeof(*hdr))
    {
        errorf("too short");
        return;
    }
    hdr = (struct tcp_hdr *)data;
    pseudo.src = iphdr->src;
    pseudo.dst = iphdr->dst;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTOCOL_TCP;
    pseudo.len = hton16(len);
    psum = ~cksum16((uint16_t *)&pseudo, sizeof(pseudo), 0);
    if (cksum16((uint16_t *)hdr, len, psum) != 0)
    {
        errorf("checksum error");
        return;
    }
    src.addr = iphdr->src;
    src.port = hdr->src;
    dst.addr = iphdr->dst;
    dst.port = hdr->dst;
    ip_endp_ntop(src, ep1, sizeof(ep1));
    ip_endp_ntop(dst, ep2, sizeof(ep2));
    if (src.addr == IP_ADDR_BROADCAST || src.addr == iface->broadcast ||
        dst.addr == IP_ADDR_BROADCAST || dst.addr == iface->broadcast)
    {
        errorf("only supports unicast, src=%s, dst=%s", ep1, ep2);
        return;
    }
    debugf("%s => %s, len=%zu, dev=%s", ep1, ep2, len, NET_IFACE(iface)->dev->name);
    tcp_print(data, len);
    return;
}

int
tcp_init(void)
{
    if (ip_protocol_register(IP_PROTOCOL_TCP, tcp_input) == -1)
    {
        errorf("ip_protocol_register() failure");
        return -1;
    }
    return 0;
}
