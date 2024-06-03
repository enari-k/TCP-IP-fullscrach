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

struct ether_tap {
    char name[IFNAMSIZ];
    int fd;
    unsigned int irq;
};

#define PRIV(x) ((struct ether_tap *)x->priv)

static int
ether_tap_set_default_addr(struct net_device *dev)
{
}

static int
ether_tap_open(struct net_device *dev)
{
}

static int
ether_tap_close(struct net_device *dev)
{
}

int
ether_tap_output(struct net_device *dev, uint16_t type, const uint8_t *buf, size_t len, const void *dst)
{
}

static int
ether_tap_input(struct net_device *dev, uint8_t *frame, size_t flen)
{
}

static void
ether_tap_isr(unsigned int irq, void *arg)
{
}

static struct net_device_ops ether_tap_ops = {
    .open = ether_tap_open,
    .close = ether_tap_close,
    .output = ether_tap_output,
};

struct net_device *
ether_tap_init(const char *name, const char *addr)
{
}
