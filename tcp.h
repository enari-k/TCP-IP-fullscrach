#ifndef TCP_H
#define TCP_H

#include <stddef.h>
#include <stdint.h>

#include "ip.h"

extern int
tcp_init(void);

extern int
tcp_cmd_open(ip_endp_t local, ip_endp_t remote, int active);
extern int
tcp_cmd_socket(void);
extern int
tcp_cmd_close(int desc);
extern int
tcp_cmd_connect(int desc, ip_endp_t remote);
extern int
tcp_cmd_bind(int desc, ip_endp_t local);
extern int
tcp_cmd_listen(int desc, int backlog);
extern int
tcp_cmd_accept(int desc, ip_endp_t *remote);
extern ssize_t
tcp_cmd_send(int desc, uint8_t *data, size_t len);
extern ssize_t
tcp_cmd_receive(int desc, uint8_t *buf, size_t size);

#endif
