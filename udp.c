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
#include "icmp.h"

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
    return indexof(pcbs, pcb);
}

static struct udp_pcb *
udp_pcb_get(int desc)
{
    struct udp_pcb *pcb;

    if (desc < 0 || countof(pcbs) <= (size_t)desc)
    {
        /* out of range */
        return NULL;
    }
    pcb = &pcbs[desc];
    if (pcb->state != UDP_PCB_STATE_OPEN)
    {
        return NULL;
    }
    return pcb;
}

static struct udp_pcb *
udp_pcb_alloc(void)
{
    struct udp_pcb *pcb;

    for (pcb = pcbs; pcb < tailof(pcbs); pcb++)
    {
        if (pcb->state == UDP_PCB_STATE_FREE)
        {
            pcb->state = UDP_PCB_STATE_OPEN;
            return pcb;
        }
    }
    return NULL;
}

static void
udp_pcb_release(struct udp_pcb *pcb)
{
    struct queue_entry *entry;

    pcb->state = UDP_PCB_STATE_FREE;
    pcb->local.addr = IP_ADDR_ANY;
    pcb->local.port = 0;
    while (1)
    {
        /* Discard the entries in the queue. */
        entry = queue_pop(&pcb->queue);
        if (!entry)
        {
            break;
        }
        debugf("free queue entry");
        memory_free(entry);
    }
}

static struct udp_pcb *
udp_pcb_select(ip_endp_t key)
{
    struct udp_pcb *pcb;

    for (pcb = pcbs; pcb < tailof(pcbs); pcb++)
    {
        if (pcb->state == UDP_PCB_STATE_OPEN)
        {
            if (pcb->local.port == key.port)
            {
                if (pcb->local.addr == key.addr || pcb->local.addr == IP_ADDR_ANY || key.addr == IP_ADDR_ANY)
                {
                    return pcb;
                }
            }
        }
    }
    return NULL;
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
    struct udp_pcb *pcb;
    uint16_t iphdrlen;
    struct udp_queue_entry *entry;

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
    lock_acquire(&lock);
    pcb = udp_pcb_select(dst);
    if (!pcb) {
        /* port is in use */
        lock_release(&lock);
        iphdrlen = (iphdr->vhl & 0x0f) << 4;
        icmp_output(ICMP_TYPE_DEST_UNREACH, ICMP_CODE_PORT_UNREACH, 0, (uint8_t *)iphdr, iphdrlen + 8, iface->unicast, iphdr->src);
        return;
    }
    entry = memory_alloc(sizeof(*entry) + (len - sizeof(*hdr)));
    if (!entry)
    {
        lock_release(&lock);
        errorf("memory_alloc() failure");
        return;
    }
    entry->remote = src;
    entry->len = len - sizeof(*hdr);
    memcpy(entry+1, hdr+1, entry->len);
    if (!queue_push(&pcb->queue, (struct queue_entry *)entry))
    {
        lock_release(&lock);
        errorf("queue_puch() failure");
        return;
    }
    debugf("queue_push: success, desc=%d, num=%d", udp_pcb_desc(pcb), pcb->queue.num);
    lock_release(&lock);
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
    struct udp_pcb *pcb;
    int desc;

    lock_acquire(&lock);
    pcb = udp_pcb_alloc();
    if (!pcb)
    {
        errorf("udp_pcb_alloc() failure");
        lock_release(&lock);
        return -1;
    }
    desc = udp_pcb_desc(pcb);
    lock_release(&lock);
    debugf("desc=%d", desc);
    return desc;
}

int
udp_cmd_close(int desc)
{
    struct udp_pcb *pcb;

    lock_acquire(&lock);
    pcb = udp_pcb_get(desc);
    if (!pcb)
    {
        errorf("pcb not found, desc=%d", desc);
        lock_release(&lock);
        return -1;
    }
    debugf("desc=%d", desc);
    udp_pcb_release(pcb);
    lock_release(&lock);
    return 0;
}

int
udp_cmd_bind(int desc, ip_endp_t local)
{
    struct udp_pcb *pcb, *exist;
    char endp1[IP_ENDP_STR_LEN];
    char endp2[IP_ENDP_STR_LEN];

    lock_acquire(&lock);
    pcb = udp_pcb_get(desc);
    if (!pcb)
    {
        errorf("pcb not found, desc=%d", desc);
        lock_release(&lock);
        return -1;
    }
    exist = udp_pcb_select(local);
    if (exist)
    {
        errorf("already in use, desc=%d, want=%s, exist=%s",
            desc, ip_endp_ntop(local, endp1, sizeof(endp1)),
            ip_endp_ntop(exist->local, endp2, sizeof(endp2)));
        lock_release(&lock);
        return -1;
    }
    pcb->local = local;
    debugf("desc=%d, %s",
        desc, ip_endp_ntop(pcb->local, endp1, sizeof(endp1)));
    lock_release(&lock);
    return 0;
}
