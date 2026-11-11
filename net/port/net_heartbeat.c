#include "net_heartbeat.h"
#include "lwip/timeouts.h"
#include <stddef.h>
static int enabled;
static uint32_t beats;
static void beat(void *arg)
{
    (void)arg;
    if(!enabled)return;
    ++beats;
    sys_timeout(500,beat,NULL);
}
void ax_heartbeat_start(void)
{
    /* One chain per interface, even when setup is repeated in one title. */
    ax_heartbeat_stop();
    enabled=1;sys_timeout(500,beat,NULL);
}
void ax_heartbeat_stop(void)
{
    enabled=0;sys_untimeout(beat,NULL);
}
uint32_t ax_heartbeat_count(void){return beats;}
void ax_heartbeat_forget(void){enabled=0;beats=0;}
