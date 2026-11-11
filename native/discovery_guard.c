#include "discovery_guard.h"
#include <string.h>
static int valid(uint32_t a,uint32_t s,uint32_t e){return !(a&3)&&a>=s&&a<e;}
int rtl_ios_discovery_prepare(RtlIosDiscoveryTxn *t,void *u,RtlIosRead32 r,RtlIosWrite32 w,uint32_t slot,uint32_t old,uint32_t next,uint32_t start,uint32_t end)
{
 if(!t||!r||!w||!valid(slot,start,end)||!valid(next,start,end)||!old||old==next||slot>end-8)return -1;
 memset(t,0,sizeof(*t));t->user=u;t->read32=r;t->write32=w;t->discovery_slot=slot;t->old_ops=old;t->new_ops=next;t->range_start=start;t->range_end=end;
 if(r(u,slot,&t->backup[0])||r(u,slot+4,&t->backup[1])||t->backup[0]!=old||t->backup[1]!=0)return -1;
 t->prepared=1;return 0;
}
int rtl_ios_discovery_commit(RtlIosDiscoveryTxn *t)
{
 uint32_t a,b;if(!t||!t->prepared||t->committed)return -1;
 if(t->read32(t->user,t->discovery_slot,&a)||t->read32(t->user,t->discovery_slot+4,&b)||a!=t->old_ops||b!=0)return -1;
 if(t->write32(t->user,t->discovery_slot,t->new_ops)||t->write32(t->user,t->discovery_slot+4,1)){t->write32(t->user,t->discovery_slot,t->backup[0]);t->write32(t->user,t->discovery_slot+4,t->backup[1]);return -1;}
 if(t->read32(t->user,t->discovery_slot,&a)||t->read32(t->user,t->discovery_slot+4,&b)||a!=t->new_ops||b!=1){t->write32(t->user,t->discovery_slot,t->backup[0]);t->write32(t->user,t->discovery_slot+4,t->backup[1]);return -1;}
 t->committed=1;return 0;
}
int rtl_ios_discovery_rollback(RtlIosDiscoveryTxn *t){if(!t||!t->prepared)return -1;if(t->write32(t->user,t->discovery_slot,t->backup[0])||t->write32(t->user,t->discovery_slot+4,t->backup[1]))return -1;t->committed=0;return 0;}
