#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/param.h>

#include "platform.h"

#include "util.h"
#include "ip.h"
#include "udp.h"

#define UDP_PCB_SIZE 16

#define UDP_PCB_STATE_FREE    0
#define UDP_PCB_STATE_OPEN    1
#define UDP_PCB_STATE_CLOSING 2

struct pseudo_hdr {
    uint32_t src;
    uint32_t dst;
    uint8_t zero;
    uint8_t protocol;
    uint16_t len;
};

struct udp_hdr {
    uint16_t src;
    uint16_t dst;
    uint16_t len;
    uint16_t sum;
};

struct udp_pcb {
    int state;
    ip_endp_t local;
    struct queue queue; /* receive queue */
};

struct udp_queue_entry {
    struct queue_entry entry;
    ip_endp_t remote;
    uint16_t len;
    /* data bytes exists after this structure. */
};

static lock_t lock = LOCK_INITIALIZER;
static struct udp_pcb pcbs[UDP_PCB_SIZE];

/*
 * Protocol Control Block (PCB)
 *
 * NOTE: PCB functions must be called after locked
 */

static int
udp_pcb_desc(struct udp_pcb *pcb)
{
}

static struct udp_pcb *
udp_pcb_get(int desc)
{
}

static struct udp_pcb *
udp_pcb_alloc(void)
{
}

static void
udp_pcb_release(struct udp_pcb *pcb)
{
}

static struct udp_pcb *
udp_pcb_select(ip_endp_t key)
{
}

static void
udp_print(const uint8_t *data, size_t len)
{
    struct udp_hdr *hdr;
    uint16_t total;

    flockfile(stderr);
    hdr = (struct udp_hdr *)data;
    fprintf(stderr, "        src: %u\n", ntoh16(hdr->src));
    fprintf(stderr, "        dst: %u\n", ntoh16(hdr->dst));
    total = ntoh16(hdr->len);
    fprintf(stderr, "        len: %u (payload: %u)\n", total, total - (uint16_t)sizeof(*hdr));
    fprintf(stderr, "        sum: 0x%04x\n", ntoh16(hdr->sum));
#ifdef HEXDUMP
    hexdump(stderr, data, len);
#endif
    funlockfile(stderr);
}

static void
udp_input(const struct ip_hdr *iphdr, const uint8_t *data, size_t len, struct ip_iface *iface)
{
    struct udp_hdr *hdr;
    uint16_t total;
    struct pseudo_hdr pseudo;
    uint16_t psum = 0;
    ip_endp_t src, dst;
    char endp1[IP_ENDP_STR_LEN];
    char endp2[IP_ENDP_STR_LEN];

    if (len < sizeof(*hdr))
    {
        errorf("too short");
        return;
    }
    hdr = (struct udp_hdr *)data;
    total = ntoh16(hdr->len);
    if (len < total)
    {
        errorf("length error: len=%zu, hdr->len=%u", len, total);
        return;
    }
    if (hdr->sum)
    {
        pseudo.src = iphdr->src;
        pseudo.dst = iphdr->dst;
        pseudo.zero = 0;
        pseudo.protocol = IP_PROTOCOL_UDP;
        pseudo.len = hton16(total);
        psum = ~cksum16((uint16_t *)&pseudo, sizeof(pseudo), 0);
        if (cksum16((uint16_t *)hdr, len, psum) != 0)
        {
            errorf("checksum error");
            return;
        }
    }
    src.addr = iphdr->src;
    src.port = hdr->src;
    dst.addr = iphdr->dst;
    dst.port = hdr->dst;
    debugf("%s => %s, len=%zu, dev=%s",
        ip_endp_ntop(src, endp1, sizeof(endp1)),
        ip_endp_ntop(dst, endp2, sizeof(endp2)),
        len, NET_IFACE(iface)->dev->name);
    udp_print(data, len);
}

int
udp_init(void)
{
    if (ip_protocol_register(IP_PROTOCOL_UDP, udp_input) == -1)
    {
        errorf("ip_protocol_register() failure");
        return -1;
    }
    return 0;
}

/*
 * User Commands
 */

int
udp_cmd_open(void)
{
}

int
udp_cmd_close(int desc)
{
}

int
udp_cmd_bind(int desc, ip_endp_t local)
{
}
