#ifndef SOCK_H
#define SOCK_H

#include <stddef.h>
#include <stdint.h>

#include "ip.h"

#define PF_UNSPEC  0
#define PF_INET    2
#define PF_INET6  10

#define AF_UNSPEC PF_UNSPEC
#define AF_INET   PF_INET
#define AF_INET6  PF_INET6

#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define IPPROTO_TCP 0
#define IPPROTO_UDP 0

#define INADDR_ANY IP_ADDR_ANY

struct in_addr {
    uint32_t s_addr;
};

struct sockaddr {
    unsigned short sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    unsigned short sin_family;
    uint16_t sin_port;
    struct in_addr sin_addr;
};

#define IFNAMSIZ 16

extern int
sock_open(int domain, int type, int protocol);
extern int
sock_close(int desc);
extern ssize_t
sock_recvfrom(int desc, void *buf, size_t n, struct sockaddr *addr, int *addrlen);
extern ssize_t
sock_sendto(int desc, const void *buf, size_t n, const struct sockaddr *addr, int addrlen);
extern int
sock_bind(int desc, const struct sockaddr *addr, int addrlen);
extern int
sock_listen(int desc, int backlog);
extern int
sock_accept(int desc, struct sockaddr *addr, int *addrlen);
extern int
sock_connect(int desc, const struct sockaddr *addr, int addrlen);
extern ssize_t
sock_recv(int desc, void *buf, size_t n);
extern ssize_t
sock_send(int desc, const void *buf, size_t n);

#endif
