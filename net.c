#include "platform.h"

#include "util.h"

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
