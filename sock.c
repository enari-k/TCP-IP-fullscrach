#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#include "util.h"
#include "ip.h"
#include "udp.h"
#include "tcp.h"
#include "sock.h"

struct sock {
    int used;
    int family;
    int type;
    int desc;
};

static lock_t lock = LOCK_INITIALIZER;
static struct sock socks[32];

static struct sock *
sock_alloc(void)
{
}

static int
sock_free(struct sock *s)
{
}

static struct sock *
sock_get(int desc)
{
}

int
sock_open(int domain, int type, int protocol)
{
}

int
sock_close(int desc)
{
}

ssize_t
sock_recvfrom(int desc, void *buf, size_t n, struct sockaddr *addr, int *addrlen)
{
}

ssize_t
sock_sendto(int desc, const void *buf, size_t n, const struct sockaddr *addr, int addrlen)
{
}

int
sock_bind(int desc, const struct sockaddr *addr, int addrlen)
{
}

int
sock_listen(int desc, int backlog)
{
}

int
sock_accept(int desc, struct sockaddr *addr, int *addrlen)
{
}

int
sock_connect(int desc, const struct sockaddr *addr, int addrlen)
{
}

ssize_t
sock_recv(int desc, void *buf, size_t n)
{
}

ssize_t
sock_send(int desc, const void *buf, size_t n)
{
}
