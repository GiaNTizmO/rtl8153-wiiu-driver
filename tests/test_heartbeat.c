#include "../net/port/net_heartbeat.h"
#include "lwip/timeouts.h"
#include "lwip/memp.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static unsigned live,peak;static u32_t now;
u32_t sys_now(void){return now;}
void etharp_tmr(void){} /* Compiled cyclic table entry; test does not arm it. */
void *memp_malloc(memp_t type)
{
    assert(type==MEMP_SYS_TIMEOUT);++live;if(live>peak)peak=live;
    /* Same 16-entry limit as the console build. */
    assert(live<=16);return calloc(1,sizeof(struct sys_timeo));
}
void memp_free(memp_t type,void *p)
{assert(type==MEMP_SYS_TIMEOUT&&live&&p);--live;free(p);}
int main(void)
{
    ax_heartbeat_forget();
    for(unsigned i=0;i<1000;++i){
        ax_heartbeat_start();assert(live==1);
        ax_heartbeat_start();assert(live==1);
        now+=500;sys_check_timeouts();assert(live==1);
        now+=35000;sys_check_timeouts();assert(live==1);
        ax_heartbeat_stop();assert(live==0);
        now+=500;sys_check_timeouts();assert(live==0);
    }
    assert(ax_heartbeat_count()==2000&&peak==1);
    ax_heartbeat_forget();assert(ax_heartbeat_count()==0);
    puts("heartbeat: real lwIP timeout list, 1000 repeated recovery cycles, at most one live timer, no leak passed");
}
