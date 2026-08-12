#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <errno.h>

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
    struct sched_task task;
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

    pcb->state = UDP_PCB_STATE_CLOSING;
    if (sched_task_destroy(&pcb->task) != 0)
    {
        debugf("pending, desc=%d", udp_pcb_desc(pcb));
        sched_task_wakeup(&pcb->task);
        return;
    }
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
    debugf("success, desc=%d", udp_pcb_desc(pcb));
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
    sched_task_wakeup(&pcb->task);
    lock_release(&lock);
}

static ssize_t
udp_output(ip_endp_t src, ip_endp_t dst, const uint8_t *data, size_t len)
{
    uint8_t buf[IP_PAYLOAD_SIZE_MAX];
    struct udp_hdr *hdr;
    struct pseudo_hdr pseudo;
    uint16_t total, psum = 0;
    char endp1[IP_ENDP_STR_LEN];
    char endp2[IP_ENDP_STR_LEN];

    if (IP_PAYLOAD_SIZE_MAX < sizeof(*hdr) + len)
    {
        errorf("too long");
        return -1;
    }
    hdr = (struct udp_hdr *)buf;
    hdr->src = src.port;
    hdr->dst = dst.port;
    total = sizeof(*hdr) + len;
    hdr->len = hton16(total);
    hdr->sum = 0;
    memcpy(hdr+1, data, len);
    pseudo.src = src.addr;
    pseudo.dst = dst.addr;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTOCOL_UDP;
    pseudo.len = hton16(total);
    psum = ~cksum16((uint16_t *)&pseudo, sizeof(pseudo), 0);
    hdr->sum = cksum16((uint16_t *)hdr, total, psum);
    if (!hdr->sum)
    {
        hdr->sum = 0xffff;
    }
    debugf("%s => %s, len=%zu",
         ip_endp_ntop(src, endp1, sizeof(endp1)),
         ip_endp_ntop(dst, endp2, sizeof(endp2)),
         total);
    udp_print(buf, total);
    if (ip_output(IP_PROTOCOL_UDP, buf, total, src.addr, dst.addr) == -1)
    {
        errorf("ip_output() failure");
        return -1;
    }
    return len;
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

ssize_t
udp_cmd_recvfrom(int desc, uint8_t *buf, size_t size, ip_endp_t *remote)
{
    struct udp_pcb *pcb;
    struct udp_queue_entry *entry;
    ssize_t len;

    lock_acquire(&lock);
    pcb = udp_pcb_get(desc);
    if (!pcb)
    {
        errorf("pcb not found, desc=%d", desc);
        lock_release(&lock);
        return -1;
    }
    while(1)
    {
        entry = (struct udp_queue_entry *)queue_pop(&pcb->queue);
        if (entry)
        {
            debugf("queue_pop: success, desc=%d, num=%d", desc, pcb->queue.num);
            break;
        }
        debugf("queue_pop: empty, desc=%d, sleep task...", desc);
        if (sched_task_sleep(&pcb->task, &lock, NULL) == -1)
        {
            debugf("interrupted");
            if (pcb->state == UDP_PCB_STATE_CLOSING)
            {
                debugf("closed");
                udp_pcb_release(pcb);
            }
            lock_release(&lock);
            errno = EINTR;
            return -1;
        }
        debugf("task wakeup");
        if (pcb->state == UDP_PCB_STATE_CLOSING)
        {
            debugf("closed");
            udp_pcb_release(pcb);
            lock_release(&lock);
            return -1;
        }
    }
    lock_release(&lock);
    if (remote)
    {
        *remote = entry->remote;
    }
    len = MIN(size, entry->len); /* truncate */
    memcpy(buf, entry+1, len);
    memory_free(entry);
    return len;
}

ssize_t
udp_cmd_sendto(int desc, uint8_t *data, size_t len, ip_endp_t remote)
{
    struct udp_pcb *pcb;
    ip_endp_t local;
    uint32_t p;
    struct ip_iface *iface;
    char addr[IP_ADDR_STR_LEN];

    lock_acquire(&lock);
    pcb = udp_pcb_get(desc);
    if (!pcb)
    {
        errorf("pcb not found, desc=%d", desc);
        lock_release(&lock);
        return -1;
    }
    local = pcb->local;
    if (!local.port)
    {
        for (p = IP_ENDP_DYNAMIC_PORT_MIN; p <= IP_ENDP_DYNAMIC_PORT_MAX; p++)
        {
            local.port = hton16(p);
            if (!udp_pcb_select(local))
            {
                pcb->local.port = local.port; /* save dynamic source port*/
                debugf("dinamic assign local port, port=%d", ntoh16(pcb->local.port));
                break;
            }
        }
        if (IP_ENDP_DYNAMIC_PORT_MAX < p)
        {
            debugf("failed to dinamic assign local port, addr=%s", ip_addr_ntop(local.addr, addr, sizeof(addr)));
            lock_release(&lock);
            return -1;
        }
    }
    if (local.addr == IP_ADDR_ANY)
    {
        iface = ip_route_get_iface(remote.addr);
        if (!iface)
        {
            errorf("iface not found that can reach foreign address, addr=%s", ip_addr_ntop(remote.addr, addr, sizeof(addr)));
            lock_release(&lock);
            return -1;
        }
        local.addr = iface->unicast;
        debugf("select local address, addr=%s", ip_addr_ntop(local.addr, addr, sizeof(addr)));
    }
    lock_release(&lock);
    return udp_output(local, remote, data, len);
}
