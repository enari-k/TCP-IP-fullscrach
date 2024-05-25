#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "platform.h"

#include "util.h"
#include "net.h"

/*
 * NOTE: if you want to add/delete the entries after net_run(),
 *       you need to protect these lists with a lock.
 */
static struct net_device *devices;

struct net_device *
net_device_alloc(void)
{
}

/*
 * NOTE: must not be call after net_run()
 */
int
net_device_register(struct net_device *dev)
{
}

static int
net_device_open(struct net_device *dev)
{
}

static int
net_device_close(struct net_device *dev)
{
}

int
net_device_output(struct net_device *dev, uint16_t type, const uint8_t *data, size_t len, const void *dst)
{
}

int net_init(void)
{
    infof("initializa...");
    if (platform_init() == -1)
    {
        errorf("platform_init() failure");
        return -1;
    }
    infof("success");
    return 0;
}

int net_run(void)
{
    infof("startup...");
    if (platform_run() == -1)
    {
        errorf("platform_run() failure");
        return -1;
    }
    infof("success");
    return 0;
}

int net_shutdown(void)
{
    infof("shutting down...");
    if (platform_shutdown() == -1)
    {
        warnf("platform_shutdown() failure");
    }
    infof("success");
    return 0;
}
