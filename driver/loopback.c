#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "util.h"
#include "net.h"

#define LOOPBACK_MTU UINT16_MAX /* Maximum size of IP datagram */

static int
loopback_output(struct net_device *dev, uint16_t type, const uint8_t *data, size_t len, const void *dst)
{
}

static struct net_device_ops loopback_ops = {
    .output = loopback_output,
};

struct net_device *
loopback_init(void)
{
}
