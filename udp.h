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

#endif
