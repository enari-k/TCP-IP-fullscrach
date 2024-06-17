#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "platform.h"

#include "util.h"
#include "net.h"
#include "ip.h"
#include "icmp.h"
#include "arp.h"

#define IP_HDR_FLAG_MF 0x2000 /* more flagments flag */
#define IP_HDR_FLAG_DF 0x4000 /* don't flagment flag */
#define IP_HDR_FLAG_RF 0x8000 /* reserved */

#define IP_HDR_OFFSET_MASK 0x1fff

struct ip_protocol
{
    struct ip_protocol *next;
    uint8_t protocol;
    ip_protocol_handler_t handler;
};

struct ip_route {
    struct ip_route *next;
    ip_addr_t network;
    ip_addr_t netmask;
    ip_addr_t nexthop;
    struct ip_iface *iface;
};

const ip_addr_t IP_ADDR_ANY       = 0x00000000; /* 0.0.0.0 */
const ip_addr_t IP_ADDR_BROADCAST = 0xffffffff; /* 255.255.255.255 */

/*
 * NOTE: if you want to add/delete the entries after net_run(),
 *       you need to protect these lists with a mutex.
 */
static struct ip_iface *ifaces;
static struct ip_protocol *protocols;
static struct ip_route *routes;

int ip_addr_pton(const char *p, ip_addr_t *n)
{
    char *sp, *ep;
    int idx;
    long ret;

    sp = (char *)p;
    for (idx = 0; idx < 4; idx++)
    {
        ret = strtol(sp, &ep, 10);
        if (ret < 0 || ret > 255)
        {
            return -1;
        }
        if (ep == sp)
        {
            return -1;
        }
        if ((idx == 3 && *ep != '\0') || (idx != 3 && *ep != '.'))
        {
            return -1;
        }
        ((uint8_t *)n)[idx] = ret;
        sp = ep + 1;
    }
    return 0;
}

char *
ip_addr_ntop(ip_addr_t n, char *p, size_t size)
{
    uint8_t *u8;

    u8 = (uint8_t *)&n;
    snprintf(p, size, "%d.%d.%d.%d", u8[0], u8[1], u8[2], u8[3]);
    return p;
}

/*
 * NOTE: must not be call after net_run()
 */
static struct ip_route *
ip_route_add(ip_addr_t network, ip_addr_t netmask, ip_addr_t nexthop, struct ip_iface *iface)
{
}

static struct ip_route *
ip_route_lookup(ip_addr_t dst)
{
}

/*
 * NOTE: must not be call after net_run()
 */
int
ip_route_set_default_gateway(struct ip_iface *iface, const char *gateway)
{
}

struct ip_iface *
ip_route_get_iface(ip_addr_t dst)
{
}

struct ip_iface *
ip_iface_alloc(const char *unicast, const char *netmask)
{
    struct ip_iface *iface;

    iface = memory_alloc(sizeof(*iface));
    if (!iface)
    {
        errorf("memory_alloc() failure");
        return NULL;
    }
    NET_IFACE(iface)->family = NET_IFACE_FAMILY_IP;
    if (ip_addr_pton(unicast, &iface->unicast) == -1)
    {
        errorf("ip_addr_pton() failure ,addr=%s", unicast);
        memory_free(iface);
        return NULL;
    }
    if (ip_addr_pton(netmask, &iface->netmask) == -1)
    {
        errorf("ip_addr_pton() failure, addr=%s", netmask);
        memory_free(iface);
        return NULL;
    }
    iface->broadcast = (iface->unicast & iface->netmask) | ~iface->netmask;
    return iface;
}
/*
 * NOTE: must not be call after net_run()
 */
int ip_iface_register(struct net_device *dev, struct ip_iface *iface)
{
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];
    char addr3[IP_ADDR_STR_LEN];

    infof("dev=%s, %s, %s, %s", dev->name,
          ip_addr_ntop(iface->unicast, addr1, sizeof(addr1)),
          ip_addr_ntop(iface->netmask, addr2, sizeof(addr2)),
          ip_addr_ntop(iface->broadcast, addr3, sizeof(addr3)));
    if (net_device_add_iface(dev, NET_IFACE(iface)) == -1)
    {
        errorf("net_device_add_iface() failure");
        return -1;
    }
    iface->next = ifaces;
    ifaces = iface;
    return 0;
}

struct ip_iface *
ip_iface_select(ip_addr_t addr)
{
    struct ip_iface *entry;

    for (entry = ifaces; entry; entry = entry->next)
    {
        if (entry->unicast == addr)
        {
            break;
        }
    }
    return entry;
}

/*
 * NOTE: must not be call after net_run()
 */
int ip_protocol_register(uint8_t protocol, ip_protocol_handler_t handler)
{
    struct ip_protocol *entry;

    for (entry = protocols; entry; entry = entry->next)
    {
        if (entry->protocol == protocol)
        {
            errorf("already exists, protocl=%u", protocol);
            return -1;
        }
    }
    entry = memory_alloc(sizeof(*entry));
    if (!entry)
    {
        errorf("memory_alloc() failure");
        return -1;
    }
    entry->protocol = protocol;
    entry->handler = handler;
    entry->next = protocols;
    protocols = entry;
    infof("success, protocol=%u", protocol);
    return 0;
}

static void
ip_print(const uint8_t *data, size_t len)
{
    struct ip_hdr *hdr;
    uint8_t v, hl, hlen;
    uint16_t total, offset;
    char addr[IP_ADDR_STR_LEN];

    flockfile(stderr);
    hdr = (struct ip_hdr *)data;
    v = hdr->vhl >> 4;
    hl = hdr->vhl & 0x0f;
    hlen = hl << 2;
    fprintf(stderr, "       vhl: 0x%02x [v: %u, hl: %u (%u)]\n", hdr->vhl, v, hl, hlen);
    fprintf(stderr, "       tos: 0x%02x\n", hdr->tos);
    total = ntoh16(hdr->total);
    fprintf(stderr, "       total: %u (payload: %u)\n", total, total - hlen);
    fprintf(stderr, "          id: %u\n", ntoh16(hdr->id));
    offset = ntoh16(hdr->offset);
    fprintf(stderr, "        offset: 0x%04x [flags=%x, offset=%u]\n",
            offset, offset >> 13, offset & IP_HDR_OFFSET_MASK);
    fprintf(stderr, "       ttl: %u\n", hdr->ttl);
    fprintf(stderr, "  protocol: %u\n", hdr->protocol);
    fprintf(stderr, "       sum: 0x%04x\n", ntoh16(hdr->sum));
    fprintf(stderr, "       src: %s\n", ip_addr_ntop(hdr->src, addr, sizeof(addr)));
    fprintf(stderr, "       dst: %s\n", ip_addr_ntop(hdr->dst, addr, sizeof(addr)));
#ifdef HEXDUMP
    hexdump(stderr, data, len);
#endif
    funlockfile(stderr);
}

static void
ip_input(const uint8_t *data, size_t len, struct net_device *dev)
{
    struct ip_hdr *hdr;
    uint8_t v;
    uint16_t hlen, total, offset;
    struct ip_iface *iface;
    char addr[IP_ADDR_STR_LEN];
    struct ip_protocol *proto;

    debugf("dev=%s, len=%zu", dev->name, len);
    if (len < IP_HDR_SIZE_MIN)
    {
        errorf("too short");
        return;
    }
    hdr = (struct ip_hdr *)data;
    v = hdr->vhl >> 4;
    if (v != IP_VERSION_IPV4)
    {
        errorf("ip version error: v=%u", v);
        return;
    }
    hlen = (hdr->vhl & 0x0f) << 2;
    if (len < hlen)
    {
        errorf("header length error: len=%zu < hlen=%u", len, hlen);
        return;
    }
    if (cksum16((uint16_t *)hdr, hlen, 0) != 0)
    {
        errorf("chechsum error");
        return;
    }
    total = ntoh16(hdr->total);
    if (len < total)
    {
        errorf("total length error: len=%zu < total=%u", len, total);
        return;
    }
    offset = ntoh16(hdr->offset);
    if (offset & IP_HDR_FLAG_MF || offset & IP_HDR_OFFSET_MASK)
    {
        errorf("fragments does not support");
        return;
    }
    iface = (struct ip_iface *)net_device_get_iface(dev, NET_IFACE_FAMILY_IP);
    if (!iface)
    {
        /* ignore */
        return;
    }
    if (hdr->dst != iface->unicast)
    {
        if (hdr->dst != iface->broadcast && hdr->dst != IP_ADDR_BROADCAST)
        {
            /* ignore: for other host */
            return;
        }
    }
    debugf("permit, dev=%s, iface=%s", dev->name, ip_addr_ntop(iface->unicast, addr, sizeof(addr)));
    ip_print(data, total);
    for (proto = protocols; proto; proto = proto->next)
    {
        if (proto->protocol == hdr->protocol)
        {
            proto->handler(hdr, data + hlen, total - hlen, iface);
            return;
        }
    }
    /* unsupported protocol */
    if (hlen + 8 <= total)
    {
        /*
         * It should not be sent in response to ICMP error messages,
         * but ICMP is always registered and will not reach this point.
         */
        icmp_output(ICMP_TYPE_DEST_UNREACH, ICMP_CODE_PROTO_UNREACH, 0, data, hlen + 8,
                    iface->unicast, hdr->src);
    }
}

static int
ip_output_device(struct ip_iface *iface, const uint8_t *data, size_t len, ip_addr_t target)
{
    char addr[IP_ADDR_STR_LEN];
    uint8_t hwaddr[NET_DEVICE_ADDR_LEN] = {};
    int ret;

    ip_addr_ntop(target, addr, sizeof(addr));
    debugf("dev=%s, len=%zu, target=%s", NET_IFACE(iface)->dev->name, len, addr);
    if (NET_IFACE(iface)->dev->flags & NET_DEVICE_FLAG_NEED_ARP)
    {
        if (target == iface->broadcast || target == IP_ADDR_BROADCAST)
        {
            memcpy(hwaddr, NET_IFACE(iface)->dev->broadcast, NET_IFACE(iface)->dev->alen);
        }
        else
        {
            ret = arp_resolve(NET_IFACE(iface), target, hwaddr);
            if (ret != ARP_RESOLVE_FOUND) {
                return ret;
            }
        }
    }
    return net_device_output(NET_IFACE(iface)->dev, NET_PROTOCOL_TYPE_IP, data, len, hwaddr);
}

static ssize_t
ip_build_packet(uint8_t protocol, const uint8_t *data, size_t len, uint16_t id,
                uint16_t offset, ip_addr_t src, ip_addr_t dst, uint8_t *buf, size_t size)
{
    uint16_t hlen, total;
    struct ip_hdr *hdr;

    hlen = IP_HDR_SIZE_MIN;
    total = hlen + len;
    if (size < total)
    {
        return -1;
    }
    hdr = (struct ip_hdr *)buf;
    hdr->vhl = (IP_VERSION_IPV4 << 4) | (hlen >> 2);
    hdr->tos = 0;
    hdr->total = hton16(total);
    hdr->id = hton16(id);
    hdr->offset = hton16(offset);
    hdr->ttl = 0xff;
    hdr->protocol = protocol;
    hdr->sum = 0;
    hdr->src = src;
    hdr->dst = dst;
    hdr->sum = cksum16((uint16_t *)hdr, hlen, 0); /* don't convert byteoder*/
    memcpy(buf + hlen, data, len);
    ip_print(buf, total);
    return (ssize_t)total;
}

ssize_t
ip_output(uint8_t protocol, const uint8_t *data, size_t len, ip_addr_t src, ip_addr_t dst)
{
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];
    struct ip_iface *iface;
    uint16_t id;
    ssize_t plen;
    uint8_t buf[IP_TOTAL_SIZE_MAX];

    ip_addr_ntop(src, addr1, sizeof(addr1));
    ip_addr_ntop(src, addr2, sizeof(addr2));
    debugf("%s => %s, protocol=%d, len=%zu", addr1, addr2, protocol, len);
    if (src == IP_ADDR_ANY)
    {
        errorf("ip routing does not implement");
        return -1;
    }
    iface = ip_iface_select(src);
    if (!iface)
    {
        errorf("iface not found, src=%s", addr1);
        return -1;
    }
    if ((dst & iface->netmask) != (iface->unicast & iface->netmask) && dst != IP_ADDR_BROADCAST)
    {
        errorf("not reached, dst=%s", addr2);
        return -1;
    }
    if (NET_IFACE(iface)->dev->mtu < IP_HDR_SIZE_MIN + len)
    {
        errorf("too long, dev=%s, mtu=%u < %zu",
               NET_IFACE(iface)->dev->name, NET_IFACE(iface)->dev->mtu, IP_HDR_SIZE_MIN + len);
        return -1;
    }
    id = random16();
    plen = ip_build_packet(protocol, data, len, id, 0, iface->unicast, dst, buf, sizeof(buf));
    if (plen == -1)
    {
        errorf("ip_build_packet() failure");
        return -1;
    }
    if (ip_output_device(iface, buf, plen, dst) == -1)
    {
        errorf("ip_output_device() failure");
        return -1;
    }
    return plen;
}

int ip_init(void)
{
    if (net_protocol_register(NET_PROTOCOL_TYPE_IP, ip_input) == -1)
    {
        errorf("net_protocol_register() failure");
        return -1;
    }
    return 0;
}
