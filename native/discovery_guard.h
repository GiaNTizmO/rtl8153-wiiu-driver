#ifndef RTL8153_DISCOVERY_GUARD_H
#define RTL8153_DISCOVERY_GUARD_H
#include <stdint.h>
typedef int (*RtlIosRead32)(void*,uint32_t,uint32_t*);
typedef int (*RtlIosWrite32)(void*,uint32_t,uint32_t);
typedef struct {void *user;RtlIosRead32 read32;RtlIosWrite32 write32;uint32_t discovery_slot,old_ops,new_ops,range_start,range_end,backup[2];int prepared,committed;} RtlIosDiscoveryTxn;
int rtl_ios_discovery_prepare(RtlIosDiscoveryTxn*,void*,RtlIosRead32,RtlIosWrite32,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
int rtl_ios_discovery_commit(RtlIosDiscoveryTxn*);
int rtl_ios_discovery_rollback(RtlIosDiscoveryTxn*);
#endif
