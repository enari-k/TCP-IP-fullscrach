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
#include "intr.h"

#include "driver/loopback.h"
#include "driver/ether_tap.h"

#include "test.h"
#include "sock.h"

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

static void
conn_main(int soc)
{
    uint8_t buf[128];
    ssize_t n;

    while (!terminate)
    {
        n = sock_recv(soc, buf, sizeof(buf));
        if (n == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            errorf("sock_recv() failure");
            break;
        }
        if (n == 0)
        {
            debugf("connection closed");
            break;
        }
        infof("%zu bytes received", n);
        hexdump(stderr, buf, n);
        if (sock_send(soc, buf, n) == -1)
        {
            errorf("sock_send() failure");
            break;
        }
    }
    sock_close(soc);
}

#define TEST_LISTEN_BACKLOG 8

static pthread_mutex_t worker_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t worker_cond = PTHREAD_COND_INITIALIZER;
static int active_workers = 0;

static void *
conn_thread_main(void *arg)
{
    int soc = (int)(intptr_t)arg;

    debugf("connection worker start, soc=%d", soc);

    /*
     * conn_main() is responsible for handling and closing
     * the accepted socket.
     */
    conn_main(soc);

    pthread_mutex_lock(&worker_mutex);
    active_workers--;
    debugf("connection worker end, soc=%d, active=%d",
           soc, active_workers);
    pthread_cond_broadcast(&worker_cond);
    pthread_mutex_unlock(&worker_mutex);

    return NULL;
}

static int
app_main(void)
{
    int soc, acc, remote_len;
    int ret = 0;
    int err;
    struct sockaddr_in local, remote;
    char addr[IP_ADDR_STR_LEN];
    pthread_t thread;
    pthread_attr_t attr;

    memset(&local, 0, sizeof(local));
    memset(&remote, 0, sizeof(remote));

    soc = sock_open(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (soc == -1)
    {
        errorf("sock_open() failure");
        return -1;
    }

    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = hton16(7);

    if (sock_bind(soc, (struct sockaddr *)&local, sizeof(local)) == -1)
    {
        errorf("sock_bind() failure");
        sock_close(soc);
        return -1;
    }

    /*
     * backlog=1 だと複数接続テストには小さすぎるため、
     * とりあえず8にしておく。
     */
    if (sock_listen(soc, TEST_LISTEN_BACKLOG) == -1)
    {
        errorf("sock_listen() failure");
        sock_close(soc);
        return -1;
    }

    if (pthread_attr_init(&attr) != 0)
    {
        errorf("pthread_attr_init() failure");
        sock_close(soc);
        return -1;
    }

    if (pthread_attr_setdetachstate(
            &attr, PTHREAD_CREATE_DETACHED) != 0)
    {
        errorf("pthread_attr_setdetachstate() failure");
        pthread_attr_destroy(&attr);
        sock_close(soc);
        return -1;
    }

    debugf("server started, press Ctrl + C to terminate");

    while (!terminate)
    {
        remote_len = sizeof(remote);

        acc = sock_accept(
            soc,
            (struct sockaddr *)&remote,
            &remote_len);

        if (acc == -1)
        {
            if (errno == EINTR)
            {
                if (terminate)
                {
                    debugf("sock_accept() interrupted by termination");
                    break;
                }

                warnf("sock_accept() interrupted");
                continue;
            }

            errorf("sock_accept() failure");
            ret = -1;
            break;
        }

        debugf("connection accepted, soc=%d, remote=%s:%u",
               acc,
               ip_addr_ntop(
                   remote.sin_addr.s_addr,
                   addr,
                   sizeof(addr)),
               ntoh16(remote.sin_port));

        pthread_mutex_lock(&worker_mutex);
        active_workers++;

        debugf("starting connection worker, soc=%d, active=%d",
               acc, active_workers);

        pthread_mutex_unlock(&worker_mutex);

        err = pthread_create(
            &thread,
            &attr,
            conn_thread_main,
            (void *)(intptr_t)acc);

        if (err != 0)
        {
            errorf("pthread_create() failure, err=%d", err);

            pthread_mutex_lock(&worker_mutex);
            active_workers--;
            pthread_mutex_unlock(&worker_mutex);

            sock_close(acc);
            ret = -1;
            break;
        }
    }

    pthread_attr_destroy(&attr);

    /*
     * 新規connectionの受付を停止する。
     */
    sock_close(soc);

    /*
     * Ctrl+CによってTCP taskもinterruptされ、
     * conn_main()が終了することを期待する。
     *
     * ここで止まるなら、それ自体がshutdown処理の
     * 次のバグを発見したことになる。
     */
    pthread_mutex_lock(&worker_mutex);

    while (active_workers > 0)
    {
        debugf("waiting for connection workers, active=%d",
               active_workers);

        pthread_cond_wait(&worker_cond, &worker_mutex);
    }

    pthread_mutex_unlock(&worker_mutex);

    debugf("terminate");

    return ret;
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
