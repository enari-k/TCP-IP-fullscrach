#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#include "util.h"
#include "net.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "intr.h"

#include "driver/loopback.h"
#include "driver/ether_tap.h"

#include "test.h"

static volatile sig_atomic_t terminate;

static void
on_signal(int signum)
{
    (void)signum;
    terminate = 1;
    intr_raise(INTR_IRQ_USER);
}

static int
setup(void)
{
    struct sigaction sa = {0};
    struct net_device *dev;
    struct ip_iface *iface;

    sa.sa_handler = on_signal;
    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        errorf("sigaction() %s", strerror(errno));
        return -1;
    }
    infof("setup protocol stack...");
    if (net_init() == -1)
    {
        errorf("net_init() failure");
        return -1;
    }
    dev = loopback_init();
    if (!dev)
    {
        errorf("loopback_init() failure");
        return -1;
    }
    iface = ip_iface_alloc(LOOPBACK_IP_ADDR, LOOPBACK_NETMASK);
    if (!iface)
    {
        errorf("ip_iface_alloc() failure");
        return -1;
    }
    if (ip_iface_register(dev, iface) == -1)
    {
        errorf("ip_iface_register() failure");
        return -1;
    }
    dev = ether_tap_init(ETHER_TAP_NAME, ETHER_TAP_HW_ADDR);
    if (!dev)
    {
        errorf("ether_tap_init() failure");
        return -1;
    }
    iface = ip_iface_alloc(ETHER_TAP_IP_ADDR, ETHER_TAP_NETMASK);
    if (!iface)
    {
        errorf("ip_iface_alloc() failure");
        return -1;
    }
    if (ip_iface_register(dev, iface) == -1)
    {
        errorf("ip_iface_register() failure");
        return -1;
    }
    if (ip_route_set_default_gateway(iface, DEFAULT_GATEWAY) == -1)
    {
        errorf("ip_route_set_default_gateway() failure");
        return -1;
    }
    if (net_run() == -1)
    {
        errorf("net_run() failure");
        return -1;
    }
    infof("success");
    return 0;
}

static int
cleanup(void)
{
    infof("cleanup protocol stack...");
    if (net_shutdown() == -1)
    {
        errorf("net_shutdown() failure");
        return -1;
    }
    return 0;
}

static int
app_main(void)
{
    int desc;
    ip_endp_t local, remote;
    uint8_t buf[128];
    ssize_t n;

    ip_endp_pton("0.0.0.0:7", &local);
    ip_endp_pton("0.0.0.0:0", &remote);
    desc = tcp_cmd_open(local, remote, 0);
    if (desc == -1)
    {
        errorf("tcp_cmd_open() failure");
        return -1;
    }
    debugf("press Ctrl+C to terminate");
    while (!terminate)
    {
      n = tcp_cmd_receive(desc, buf, sizeof(buf));
      if (n <= 0)
      {
        break;
      }
      debugf("%zd bytes data received", n);
      hexdump(stderr, buf, n);
      tcp_cmd_send(desc, buf, n);
    }
    tcp_cmd_close(desc);
    debugf("terminate");
    return 0;
}

int main(void)
{
    int ret;

    if (setup() == -1)
    {
        errorf("setup() failure");
        return -1;
    }
    ret = app_main();
    if (cleanup() == -1)
    {
        errorf("cleanup() failure");
        return -1;
    }
    return ret;
}
