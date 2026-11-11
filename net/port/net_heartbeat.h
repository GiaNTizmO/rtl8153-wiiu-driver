#ifndef RTL_NET_HEARTBEAT_H
#define RTL_NET_HEARTBEAT_H
#include <stdint.h>
/* All calls, including the callback, require the lwIP core lock. */
void ax_heartbeat_start(void);
void ax_heartbeat_stop(void);
uint32_t ax_heartbeat_count(void);
/* After the old tcpip thread is dead and lwIP pools/list heads are reset. */
void ax_heartbeat_forget(void);
#endif
