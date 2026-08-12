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

static void *
receiver(void *arg)
{
    int desc;
    uint8_t buf[128];
    ip_endp_t remote;
    ssize_t n;
    char endp[IP_ENDP_STR_LEN];

    debugf("running...");
    desc = *(int *)arg;
    while (!terminate)
    {
        n = udp_cmd_recvfrom(desc, buf, sizeof(buf), &remote);
        if (n == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            warnf("udp_cmd_recvfrom() failure");
            break;
        }
        infof("%d bytes data receive from %s",
              n, ip_endp_ntop(remote, endp, sizeof(endp)));
        hexdump(stderr, buf, n);
    }
    debugf("terminate");
    return NULL;
}

static int
app_main(void)
{
    int desc, err;
    ip_endp_t remote;
    pthread_t tid;
    uint8_t buf[128];
    ssize_t n;
    char endp[IP_ENDP_STR_LEN];

    desc = udp_cmd_open();
    if (desc == -1)
    {
        errorf("udp_open() failure");
        return -1;
    }
    err = pthread_create(&tid, NULL, receiver, (void *)&desc);
    if (err)
    {
        errorf("pthread_create() %s", strerror(err));
        udp_cmd_close(desc);
        return -1;
    }
    ip_endp_pton("192.0.2.1:10007", &remote);
    debugf("press Ctrl+C to terminate");
    while (!terminate)
    {
        if (!fgets((char *)buf, sizeof(buf), stdin))
        {
            break;
        }
        n = strlen((char *)buf);
        infof("%d bytes data send to %s",
              n, ip_endp_ntop(remote, endp, sizeof(endp)));
        hexdump(stderr, buf, n);
        if (udp_cmd_sendto(desc, buf, n, remote) == -1)
        {
            errorf("udp_cmd_sendto() failure");
            break;
        }
    }
    udp_cmd_close(desc);
    pthread_join(tid, NULL);
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
