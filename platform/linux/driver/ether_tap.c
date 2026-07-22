#define _GNU_SOURCE /* for F_SETSIG */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#include "platform.h"

#include "util.h"
#include "net.h"
#include "ether.h"

#include "driver/ether_tap.h"

#define CLONE_DEVICE "/dev/net/tun"

#define ETHER_TAP_IRQ (INTR_IRQ_BASE)

struct ether_tap
{
    char name[IFNAMSIZ];
    int fd;
    unsigned int irq;
};

#define PRIV(x) ((struct ether_tap *)x->priv)

static int
ether_tap_set_default_addr(struct net_device *dev)
{
    int soc;
    struct ifreq ifr = {};

    soc = socket(AF_INET, SOCK_DGRAM, 0);
    if (soc == -1)
    {
        errorf("socket: %s, dev=%s", strerror(errno), dev->name);
        return -1;
    }
    strncpy(ifr.ifr_name, PRIV(dev)->name, sizeof(ifr.ifr_name) - 1);
    if (ioctl(soc, SIOCGIFHWADDR, &ifr) == -1)
    {
        errorf("ioctl(soc, SIOCGIFHWADDR): %s, dev=%s", strerror(errno), dev->name);
        close(soc);
        return -1;
    }
    memcpy(dev->addr, ifr.ifr_hwaddr.sa_data, ETHER_ADDR_LEN);
    close(soc);
    return 0;
}

static int
ether_tap_open(struct net_device *dev)
{
    struct ether_tap *tap;
    struct ifreq ifr = {0};
    int val;
    char addr[ETHER_ADDR_STR_LEN];

    tap = PRIV(dev);
    tap->fd = open(CLONE_DEVICE, O_RDWR);
    if (tap->fd == -1)
    {
        errorf("open: %s, dev=%s", strerror(errno), dev->name);
        return -1;
    }
    strncpy(ifr.ifr_name, tap->name, sizeof(ifr.ifr_name) - 1);
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (ioctl(tap->fd, TUNSETIFF, &ifr) == -1)
    {
        errorf("ioctl(TUNSETIFF): %s, dev=%s", strerror(errno), dev->name);
        close(tap->fd);
        return -1;
    }
    if (fcntl(tap->fd, F_SETOWN, getpid()) == -1)
    {
        errorf("fcntl(F_SETOWN): %s, dev=%s", strerror(errno), dev->name);
        close(tap->fd);
        return -1;
    }
    val = fcntl(tap->fd, F_GETFL, 0);
    if (val == -1)
    {
        errorf("fcntl(F_GETFL): %s, dev=%s", strerror(errno), dev->name);
        close(tap->fd);
        return -1;
    }
    if (fcntl(tap->fd, F_SETFL, val | O_ASYNC | O_NONBLOCK) == -1)
    {
        errorf("fcntl(F_SETFL): %s, dev=%s", strerror(errno), dev->name);
        close(tap->fd);
        return -1;
    }
    if (fcntl(tap->fd, F_SETSIG, tap->irq) == -1)
    {
        errorf("fcntl(F_SETSIG): %s, dev=%s", strerror(errno), dev->name);
        close(tap->fd);
        return -1;
    }
    if (memcmp(dev->addr, ETHER_ADDR_ANY, ETHER_ADDR_LEN) == 0)
    {
        if (ether_tap_set_default_addr(dev) == -1)
        {
            errorf("ether_tap_addr() failure, dev=%s", dev->name);
            close(tap->fd);
            return -1;
        }
    }
    infof("dev=%s, addr=%s", dev->name, ether_addr_ntop(dev->addr, addr, sizeof(addr)));
    return 0;
}

static int
ether_tap_close(struct net_device *dev)
{
    infof("dev=%s", dev->name);
    return close(PRIV(dev)->fd);
}

int ether_tap_output(struct net_device *dev, uint16_t type, const uint8_t *buf, size_t len, const void *dst)
{
    uint8_t frame[ETHER_FRAME_SIZE_MAX] = {};
    struct ether_hdr *hdr;
    size_t flen, pad = 0;

    hdr = (struct ether_hdr *)frame;
    memcpy(hdr->dst, dst, ETHER_ADDR_LEN);
    memcpy(hdr->src, dev->addr, ETHER_ADDR_LEN);
    hdr->type = hton16(type);
    memcpy(hdr + 1, buf, len);
    if (len < ETHER_PAYLOAD_SIZE_MIN)
    {
        pad = ETHER_PAYLOAD_SIZE_MIN - len;
    }
    flen = sizeof(*hdr) + len + pad;
    debugf("dev=%s, type=0x%04x, len=%zu", dev->name, type, flen);
    ether_print(frame, flen);
    if (write(PRIV(dev)->fd, frame, flen) == -1)
    {
        errorf("write: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int
ether_tap_input(struct net_device *dev, uint8_t *frame, size_t flen)
{
    struct ether_hdr *hdr;
    uint16_t type;

    if (flen < (ssize_t)sizeof(*hdr))
    {
        errorf("too short");
        return -1;
    }
    hdr = (struct ether_hdr *)frame;
    if (memcmp(dev->addr, hdr->dst, ETHER_ADDR_LEN) != 0)
    {
        if (memcmp(ETHER_ADDR_BROADCAST, hdr->dst, ETHER_ADDR_LEN) != 0)
        {
            /* for other host*/
            return -1;
        }
    }
    type = ntoh16(hdr->type);
    debugf("dev=%s, type=0x%04x, len=%zd", dev->name, type, flen);
    ether_print(frame, flen);
    return net_input(type, (uint8_t *)(hdr + 1), flen - sizeof(*hdr), dev);
}

static void
ether_tap_isr(unsigned int irq, void *arg)
{
    struct net_device *dev;
    uint8_t buf[ETHER_FRAME_SIZE_MAX];
    ssize_t n;

    (void)irq;
    dev = (struct net_device *)arg;
    while (1)
    {
        n = read(PRIV(dev)->fd, buf, sizeof(buf));
        if (n == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            errorf("read: %s, dev=%s", strerror(errno), dev->name);
            return;
        }
        ether_tap_input(dev, buf, n);
    }
    return;
}

static struct net_device_ops ether_tap_ops = {
    .open = ether_tap_open,
    .close = ether_tap_close,
    .output = ether_tap_output,
};

struct net_device *
ether_tap_init(const char *name, const char *addr)
{
    struct net_device *dev;
    struct ether_tap *tap;

    infof("name=%s, addr=%s", name, addr ? addr : "(none)");
    dev = net_device_alloc();
    if (!dev)
    {
        errorf("net_device_alloc() failure");
        return NULL;
    }
    dev->type = NET_DEVICE_TYPE_ETHERNET;
    dev->mtu = ETHER_PAYLOAD_SIZE_MAX;
    dev->flags = (NET_DEVICE_FLAG_BROADCAST | NET_DEVICE_FLAG_NEED_ARP);
    dev->hlen = ETHER_HDR_SIZE;
    dev->alen = ETHER_ADDR_LEN;
    memcpy(dev->broadcast, ETHER_ADDR_BROADCAST, ETHER_ADDR_LEN);
    if (addr)
    {
        if (ether_addr_pton(addr, dev->addr) == -1)
        {
            errorf("invalid address, addr=%s", addr);
            return NULL;
        }
    }
    dev->ops = &ether_tap_ops;
    tap = memory_alloc(sizeof(*tap));
    if (!tap)
    {
        errorf("memory_alloc() failure");
        return NULL;
    }
    strncpy(tap->name, name, sizeof(tap->name) - 1);
    tap->fd = -1;
    tap->irq = ETHER_TAP_IRQ;
    dev->priv = tap;
    if (net_device_register(dev) == -1)
    {
        errorf("net_device_register() failure");
        memory_free(tap);
        return NULL;
    }
    if (intr_register(tap->irq, ether_tap_isr, INTR_IRQ_SHARED, dev) == -1)
    {
        errorf("intr_register() failure");
        return NULL;
    }
    infof("success, dev=%s, irq=%d", dev->name, tap->irq);
    return dev;
}
