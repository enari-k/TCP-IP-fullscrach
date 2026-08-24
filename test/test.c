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
    int desc, new_desc;
    ip_endp_t local, remote;
    char ep[IP_ENDP_STR_LEN];
    uint8_t buf[128];
    ssize_t n;

    ip_endp_pton("0.0.0.0:7", &local);
    desc = tcp_cmd_socket();
    if (desc == -1)
    {
        errorf("tcp_cmd_socket() failure");
        return -1;
    }
    if (tcp_cmd_bind(desc, local) == -1)
    {
        errorf("tcp_cmd_bind() failure");
        tcp_cmd_close(desc);
        return -1;
    }
    if (tcp_cmd_listen(desc, 1) == -1)
    {
        errorf("tcp_cmd_listen() failure");
        tcp_cmd_close(desc);
        return -1;
    }
    new_desc = tcp_cmd_accept(desc, &remote);
    if (new_desc == -1)
    {
        errorf("tcp_cmd_accept() failure");
        return -1;
    }
    debugf("connection from %s, desc=%d", ip_endp_ntop(remote, ep, sizeof(ep)), new_desc);
    debugf("press Ctrl+C to terminate");
    while (!terminate)
    {
      n = tcp_cmd_receive(new_desc, buf, sizeof(buf));
      if (n <= 0)
      {
        warnf("connection close by remote");
        break;
      }
      debugf("%zd bytes data received", n);
      hexdump(stderr, buf, n);
      tcp_cmd_send(new_desc, buf, n);
    }
    tcp_cmd_close(new_desc);
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
    sleep(1);
    if (cleanup() == -1)
    {
        errorf("cleanup() failure");
        return -1;
    }
    return ret;
}
