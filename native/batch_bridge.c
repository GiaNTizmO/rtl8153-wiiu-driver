#include "batch_bridge.h"
#include "frame_adapter.h"
#include "ios_net_abi.h"
#include <string.h>

static uint32_t be32(const uint8_t *p)
{return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put_be32(uint8_t *p,uint32_t v)
{p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static int overlaps(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    /* Subtraction avoids end-pointer overflow. */
    return x<=y ? y-x<an : x-y<bn;
}
int rtl_ios_repack_tx(const void *allocation,size_t allocation_bytes,
                      uint32_t ios_address,unsigned native_header_bytes,
                      void *out,size_t capacity,size_t *written)
{
    if(!written)return -1;
    *written=0;
    if(!allocation||!out||allocation_bytes<sizeof(IosEthTxBatch)||
       (native_header_bytes!=4&&native_header_bytes!=8)||
       allocation_bytes>UINT32_MAX-ios_address||
       overlaps(allocation,allocation_bytes,out,capacity))return -1;
    const uint8_t *b=allocation;
    uint32_t count=be32(b+offsetof(IosEthTxBatch,count));
    uint32_t total=be32(b+offsetof(IosEthTxBatch,transfer_bytes));
    if(!count||count>128||total>16384||
       total>allocation_bytes-sizeof(IosEthTxBatch))return -1;
    const void *frames[128];size_t lengths[128];
    size_t pos=0;
    for(uint32_t i=0;i<count;++i){
        const uint8_t *v=b+offsetof(IosEthTxBatch,frames)+i*8;
        uint32_t address=be32(v),length=be32(v+4);
        if(length<14||length>1518)return -1;
        size_t step=native_header_bytes+((length+1u)&~(size_t)1);
        if(pos>total||step>total-pos)return -1;
        size_t offset=sizeof(IosEthTxBatch)+pos;
        if(address!=ios_address+offset)return -1;
        frames[i]=b+offset+native_header_bytes;lengths[i]=length;
        pos+=step;
    }
    if(pos!=total)return -1;
    return rtl_native_tx_batch(out,capacity,frames,lengths,count,written);
}
int rtl_ios_publish_rx(void *header,size_t header_bytes,uint32_t dma_address,
                       const void *dma,size_t dma_allocation_bytes,
                       size_t completed_bytes)
{
    if(!header||!dma||header_bytes<sizeof(IosEthRxBatch)||
       completed_bytes>dma_allocation_bytes||dma_allocation_bytes>32768||
       dma_allocation_bytes>UINT32_MAX-dma_address||
       overlaps(header,header_bytes,dma,dma_allocation_bytes))return -1;
    uint8_t *b=header;
    if(be32(b+offsetof(IosEthRxBatch,transfer_buffer))!=dma_address)return -1;
    RtlNativeFrame frames[128];size_t count=0;
    if(rtl_native_rx_views(dma,completed_bytes,frames,128,&count))return -1;
    /* Commit after bounds/descriptor validation; preserve opaque IPC fields. */
    for(size_t i=0;i<count;++i){
        uint8_t *v=b+offsetof(IosEthRxBatch,frames)+i*8;
        put_be32(v,dma_address+frames[i].offset);put_be32(v+4,frames[i].length);
    }
    put_be32(b+offsetof(IosEthRxBatch,status),0);
    put_be32(b+offsetof(IosEthRxBatch,count),(uint32_t)count);
    return 0;
}
