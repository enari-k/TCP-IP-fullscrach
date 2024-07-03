#ifndef TCP_H
#define TCP_H

#include "ip.h"

extern int
tcp_init(void);

extern int
tcp_cmd_open(ip_endp_t local, ip_endp_t remote, int active);
extern int
tcp_cmd_close(int desc);

#endif
