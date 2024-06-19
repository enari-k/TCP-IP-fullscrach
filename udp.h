#ifndef UDP_H
#define UDP_H

#include <stddef.h>
#include <stdint.h>

#include "ip.h"

extern int
udp_init(void);

extern int
udp_cmd_open(void);
extern int
udp_cmd_close(int desc);
extern int
udp_cmd_bind(int desc, ip_endp_t local);
extern ssize_t
udp_cmd_recvfrom(int desc, uint8_t *buf, size_t size, ip_endp_t *remote);
extern ssize_t
udp_cmd_sendto(int desc, uint8_t *data, size_t len, ip_endp_t remote);

#endif
