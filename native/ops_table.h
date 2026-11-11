#ifndef RTL8153_IOS_OPS_TABLE_H
#define RTL8153_IOS_OPS_TABLE_H
#include <stddef.h>
#include <stdint.h>
#include "ios_net_abi.h"
typedef struct {
    uint32_t controller_bytes,tx_capacity,tx_header_bytes,tx_pending;
    uint32_t rx_capacity,rx_pending;
    IosAddress open,close,get_link,send_async,receive_async,completion,post_link_interrupt;
} RtlIosOpsSpec;
int rtl_ios_build_ops(void *out,size_t bytes,const RtlIosOpsSpec *spec);
int rtl_ios_validate_ops(const void *table,size_t bytes,uint32_t expected_vid_pid,
                         uint32_t image_start,uint32_t image_end);
#endif
