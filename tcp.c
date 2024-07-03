#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>

#include "platform.h"

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

#define TCP_PCB_SIZE 16

#define TCP_STATE_NONE 0
#define TCP_STATE_CLOSED 1
#define TCP_STATE_LISTEN 2
#define TCP_STATE_SYN_SENT 3
#define TCP_STATE_SYN_RECEIVED 4
#define TCP_STATE_ESTABLISHED 5
#define TCP_STATE_FIN_WAIT1 6
#define TCP_STATE_FIN_WAIT2 7
#define TCP_STATE_CLOSE_WAIT 8
#define TCP_STATE_CLOSING 9
#define TCP_STATE_LAST_ACK 10
#define TCP_STATE_TIME_WAIT 11

#define TCP_STATE_CHANGE(x, y)             \
    do                                     \
    {                                      \
        debugf("desc=%d, %s => %s",        \
               tcp_pcb_desc((x)),          \
               tcp_state_ntoa((x)->state), \
               tcp_state_ntoa((y)));       \
        (x)->state = (y);                  \
    } while (0);

struct pseudo_hdr
{
    uint32_t src;
    uint32_t dst;
    uint8_t zero;
    uint8_t protocol;
    uint16_t len;
};

struct tcp_hdr
{
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

struct snd_vars
{
    uint32_t nxt;
    uint32_t una;
    uint16_t wnd;
    uint16_t up;
    uint32_t wl1;
    uint32_t wl2;
};

struct rcv_vars
{
    uint32_t nxt;
    uint16_t wnd;
    uint16_t up;
};

struct tcp_pcb
{
    int state;
    ip_endp_t local;
    ip_endp_t remote;
    struct snd_vars snd;
    uint32_t iss;
    struct rcv_vars rcv;
    uint32_t irs;
    uint16_t mss;
    uint8_t buf[65535]; /* receive buffer */
    struct sched_task task;
};

struct seg_info
{
    uint32_t seq;
    uint32_t ack;
    uint16_t len;
    uint16_t wnd;
    uint16_t up;
};

static lock_t lock = LOCK_INITIALIZER; /* for PCBs*/
static struct tcp_pcb pcbs[TCP_PCB_SIZE];

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
    switch (opt)
    {
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

static char *
tcp_state_ntoa(int state)
{
    switch (state)
    {
    case TCP_STATE_NONE:
        return "NONE";
    case TCP_STATE_CLOSED:
        return "CLOSED";
    case TCP_STATE_LISTEN:
        return "LISTEN";
    case TCP_STATE_SYN_SENT:
        return "SYN_SENT";
    case TCP_STATE_SYN_RECEIVED:
        return "SYN_RECEIVED";
    case TCP_STATE_ESTABLISHED:
        return "ESTABLISHED";
    case TCP_STATE_FIN_WAIT1:
        return "FIN_WAIT1";
    case TCP_STATE_FIN_WAIT2:
        return "FIN_WAIT2";
    case TCP_STATE_CLOSE_WAIT:
        return "CLOSE_WAIT";
    case TCP_STATE_CLOSING:
        return "CLOSING";
    case TCP_STATE_LAST_ACK:
        return "LAST_ACK";
    case TCP_STATE_TIME_WAIT:
        return "TIME_WAIT";
    default:
        return "UNKNOWN";
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

/*
 * TCP Protocol Control Block (PCB)
 *
 * NOTE: TCP PCB functions must be called after locked
 */

static int
tcp_pcb_desc(struct tcp_pcb *pcb)
{
    return indexof(pcbs, pcb);
}

static struct tcp_pcb *
tcp_pcb_get(int desc)
{
    struct tcp_pcb *pcb;

    if (desc < 0 || (int)countof(pcbs) <= desc)
    {
        /* out range */
        return NULL;
    }
    pcb = &pcbs[desc];
    if (pcb->state == TCP_STATE_NONE)
    {
        return NULL;
    }
    return pcb;
}

static struct tcp_pcb *
tcp_pcb_alloc(void)
{
    struct tcp_pcb *pcb;

    for (pcb = pcbs; pcb < tailof(pcbs); pcb++)
    {
        if (pcb->state == TCP_STATE_NONE)
        {
            pcb->state = TCP_STATE_CLOSED;
            sched_task_init(&pcb->task);
            return pcb;
        }
    }
    return NULL;
}

static void
tcp_pcb_release(struct tcp_pcb *pcb)
{
    if (sched_task_destroy(&pcb->task) != 0)
    {
        debugf("pending, desc=%d", tcp_pcb_desc(pcb));
        sched_task_wakeup(&pcb->task);
        return;
    }
    memset(pcb, 0, sizeof(*pcb));
    debugf("success, desc=%d", tcp_pcb_desc(pcb));
}

static struct tcp_pcb *
tcp_pcb_select(ip_endp_t key1, ip_endp_t key2)
{
    struct tcp_pcb *pcb, *candidate = NULL;

    for (pcb = pcbs; pcb < tailof(pcbs); pcb++)
    {
        if (pcb->local.port != key1.port)
        {
            continue;
        }
        if (pcb->local.addr == key1.addr ||
            pcb->local.addr == IP_ADDR_ANY ||
            key1.addr != IP_ADDR_ANY)
        {
            if (pcb->state != TCP_STATE_LISTEN)
            {
                return pcb;
            }
            candidate = pcb;
        }
    }
    return candidate;
}

static ssize_t
tcp_output_segment(uint32_t seq, uint32_t ack, uint8_t flg, uint16_t wnd,
                   const uint8_t *data, size_t len, ip_endp_t local, ip_endp_t remote)
{
    uint8_t buf[IP_PAYLOAD_SIZE_MAX] = {0};
    struct tcp_hdr *hdr;
    uint8_t hlen;
    struct pseudo_hdr pseudo;
    uint16_t psum;
    uint16_t total;
    char ep1[IP_ENDP_STR_LEN];
    char ep2[IP_ENDP_STR_LEN];

    hdr = (struct tcp_hdr *)buf;
    hdr->src = local.port;
    hdr->dst = remote.port;
    hdr->seq = hton32(seq);
    hdr->ack = hton32(ack);
    hlen = sizeof(*hdr);
    hdr->off = (hlen >> 2) << 4;
    hdr->flg = flg;
    hdr->wnd = hton16(wnd);
    hdr->sum = 0;
    hdr->up = 0;
    memcpy((uint8_t *)hdr + hlen, data, len);
    pseudo.src = local.addr;
    pseudo.dst = remote.addr;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTOCOL_TCP;
    total = hlen + len;
    pseudo.len = hton16(total);
    psum = ~cksum16((uint16_t *)&pseudo, sizeof(pseudo), 0);
    hdr->sum = cksum16((uint16_t *)hdr, total, psum);
    debugf("%s => %s, len=%zu",
           ip_endp_ntop(local, ep1, sizeof(ep1)),
           ip_endp_ntop(remote, ep2, sizeof(ep2)),
           total);
    tcp_print(buf, total);
    if (ip_output(IP_PROTOCOL_TCP, buf, total, local.addr, remote.addr) == -1)
    {
        return -1;
    }
    return len;
}

static ssize_t
tcp_output(struct tcp_pcb *pcb, uint8_t flg, const uint8_t *data, size_t len)
{
}

/* rfc793 - section 3.9 [Event Processing > SEGMENT ARRIVES] */
static void
tcp_segment_arrives(struct seg_info *seg, uint8_t flags, const uint8_t *data, size_t len,
                    ip_endp_t local, ip_endp_t remote)
{
    struct tcp_pcb *pcb;

    pcb = tcp_pcb_select(local, remote);
    if (!pcb || pcb->state == TCP_STATE_CLOSED)
    {
        debugf("PCB is %s", pcb ? "closed" : "not found");
        if (TCP_FLG_ISSET(flags, TCP_FLG_RST))
        {
            return;
        }
        if (!TCP_FLG_ISSET(flags, TCP_FLG_ACK))
        {
            tcp_output_segment(0, seg->seq + seg->len, TCP_FLG_RST | TCP_FLG_ACK, 0,
                               NULL, 0, local, remote);
        }
        else
        {
            tcp_output_segment(seg->ack, 0, TCP_FLG_RST, 0, NULL, 0, local, remote);
        }
        return;
    }
    switch (pcb->state) {
    case TCP_STATE_LISTEN:
        /*
         * 1st check for an RST
         */

        /*
         * 2nd check for an ACK
         */

        /*
         * 3rd check for an SYN
        */

        /*
         * 4th other text or control
         */

        /* drop segment */
        return;
    case TCP_STATE_SYN_SENT:
        /*
         * 1st check the ACK bit
         */

        /*
         * 2nd check the RST bit
         */

        /*
         * 3rd check security and precedence (ignore)
         */

        /*
         * 4th check the SYN bit
         */

        /*
         * 5th, if neither of the SYN or RST bits is set then drop the segment and return
         */

        /* drop segment */
        return;
    }
    /*
     * Otherwise
     */

    /*
     * 1st check sequence number
     */

    /*
     * 2nd check the RST bit
     */

    /*
     * 3rd check security and precedence (ignore)
     */

    /*
     * 4th check the SYN bit
     */

    /*
     * 5th check the ACK field
     */

    /*
     * 6th, check the URG bit (ignore)
     */

    /*
     * 7th, process the segment text
     */

    /*
     * 8th, check the FIN bit
     */
    return;
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
    uint8_t hlen;
    struct seg_info seg;

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
    hlen = (hdr->off >> 4) << 2;
    seg.seq = ntoh32(hdr->seq);
    seg.ack = ntoh32(hdr->ack);
    seg.len = len - hlen;
    if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_SYN))
    {
        seg.len++; /* SYN flag consumes one sequence number */
    }
    if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_FIN))
    {
        seg.len++; /* SYN flag consumes one sequence number */
    }
    seg.wnd = ntoh16(hdr->wnd);
    seg.up = ntoh16(hdr->up);
    lock_acquire(&lock);
    tcp_segment_arrives(&seg, hdr->flg, (uint8_t *)hdr + hlen, len - hlen, dst, src);
    lock_release(&lock);
    return;
}

int tcp_init(void)
{
    if (ip_protocol_register(IP_PROTOCOL_TCP, tcp_input) == -1)
    {
        errorf("ip_protocol_register() failure");
        return -1;
    }
    return 0;
}


/*
 * TCP User Command
 */

int
tcp_cmd_open(ip_endp_t local, ip_endp_t remote, int active)
{
}

int
tcp_cmd_close(int desc)
{
}
