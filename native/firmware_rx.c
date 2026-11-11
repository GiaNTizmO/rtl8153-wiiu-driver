#include "firmware_rx.h"
#include <string.h>

static RtlIosFirmwareRx *active_rx;
static IosAddress active_context,active_controller;

static uint32_t be32(const unsigned char *p)
{return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put32(unsigned char *p,uint32_t v)
{p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static int overlap(uintptr_t a,uint32_t an,uintptr_t b,uint32_t bn)
{return a<=b?b-a<an:a-b<bn;}
int rtl_ios_firmware_rx_init(RtlIosFirmwareRx *r,const RtlIosUhsBinding *b,
                            const RtlIosFirmwareRxHost *h)
{
    if(!r||r==active_rx||!b||!h||!h->alloc_dma||!h->free_dma||!h->pending||!h->rx_done||
       !h->discard||!h->drain||!b->interface_id||!b->bulk_async)return -1;
    RtlIosUhsBinding copy=*b;RtlIosFirmwareRxHost host=*h;
    memset(r,0,sizeof(*r));r->binding=copy;r->host=host;r->next_cookie=1;
    return 0;
}
int rtl_ios_firmware_rx_submit(RtlIosFirmwareRx *r,void *batch,
                               IosAddress address,uint32_t allocation,
                               void *inline_ptr,uint32_t capacity)
{
    if(!r||r->busy||r->needs_drain||r->closing||!r->next_cookie||!batch||!address||(address&3u)||
       !inline_ptr||capacity!=IOS_ETH_RX_TRANSFER_BYTES||allocation<sizeof(IosEthRxBatch)||
       allocation>UINT32_MAX-address||(uintptr_t)batch>UINTPTR_MAX-allocation)return -1;
    uint32_t inline_address=be32((unsigned char*)batch+IOS_ETH_RX_BATCH_BUFFER);
    uint32_t offset=inline_address-address;
    if(inline_address<address||offset<sizeof(IosEthRxBatch)||offset>allocation||
       capacity>allocation-offset||(inline_address&31u)||
       (uintptr_t)batch>UINTPTR_MAX-offset||
       (uintptr_t)inline_ptr!=(uintptr_t)batch+offset)return -1;
    unsigned index=2;
    for(unsigned i=0;i<2;++i){
        RtlIosFirmwareRxSlot *s=&r->slots[i];
        if(!s->pending){if(index==2)index=i;continue;}
        if(overlap(address,allocation,s->batch_address,s->allocation)||
           overlap((uintptr_t)batch,allocation,(uintptr_t)s->batch,s->allocation)||
           overlap(address,allocation,s->dma_address,s->capacity)||
           overlap((uintptr_t)batch,allocation,(uintptr_t)s->dma,s->capacity))return -1;
    }
    if(index==2)return -1;
    RtlIosFirmwareRxSlot *s=&r->slots[index];
    r->busy=1;
    void *dma=NULL;IosAddress dma_address=0;
    int rc=r->host.alloc_dma(r->host.user,capacity,&dma,&dma_address);
    if(rc||!dma||!dma_address||(dma_address&63u)||((uintptr_t)dma&63u)||
       capacity>UINT32_MAX-dma_address||(uintptr_t)dma>UINTPTR_MAX-capacity||
       overlap((uintptr_t)dma,capacity,(uintptr_t)batch,allocation)||
       overlap(dma_address,capacity,address,allocation)){
        if(dma)r->host.free_dma(r->host.user,dma,dma_address);
        r->busy=0;return -1;
    }
    for(unsigned i=0;i<2;++i)if(r->slots[i].pending&&
       (overlap(dma_address,capacity,r->slots[i].dma_address,r->slots[i].capacity)||
        overlap((uintptr_t)dma,capacity,(uintptr_t)r->slots[i].dma,r->slots[i].capacity)||
        overlap(dma_address,capacity,r->slots[i].batch_address,r->slots[i].allocation)||
        overlap((uintptr_t)dma,capacity,(uintptr_t)r->slots[i].batch,r->slots[i].allocation))){
        r->host.free_dma(r->host.user,dma,dma_address);r->busy=0;return -1;
    }
    *s=(RtlIosFirmwareRxSlot){.batch=batch,.inline_buffer=inline_ptr,.dma=dma,
        .batch_address=address,.inline_address=inline_address,.dma_address=dma_address,
        .capacity=capacity,.cookie=r->next_cookie++,.allocation=allocation,.pending=1};
    RtlIosUhsBinding tagged=r->binding;tagged.completion_cookie=s->cookie;
    rc=rtl_ios_uhs_submit(&tagged,1,address,dma_address,capacity);
    if(rc<0){r->host.free_dma(r->host.user,dma,dma_address);memset(s,0,sizeof(*s));}
    else r->host.pending(r->host.user,1);
    r->busy=0;return rc;
}
int rtl_ios_firmware_rx_complete(void *opaque,const RtlIosUhsCompletion *m,
                                 int status,uint32_t done)
{
    RtlIosFirmwareRx *r=opaque;
    if(!r||r->busy||r->needs_drain||!m||m->kind!=1||m->interface_id!=r->binding.interface_id)return -1;
    unsigned index=2;
    for(unsigned i=0;i<2;++i){
        RtlIosFirmwareRxSlot *s=&r->slots[i];
        if(s->pending&&m->batch==s->batch_address&&m->buffer==s->dma_address&&
           m->cookie==s->cookie){index=i;break;}
    }
    if(index==2)return -1;
    RtlIosFirmwareRxSlot s=r->slots[index];
    memset(&r->slots[index],0,sizeof(r->slots[index]));r->busy=1;
    RtlIosFirmwareRxHost host=r->host;
    host.pending(host.user,-1);
    int rc=status<0?status:0;
    if(!rc&&done>s.capacity)rc=-1;
    if(!r->closing&&!rc){
        memcpy(s.inline_buffer,s.dma,done);
        if(rtl_ios_publish_rx(s.batch,sizeof(IosEthRxBatch),s.inline_address,
                               s.inline_buffer,s.capacity,done))rc=-1;
    }
    if(rc){
        put32((unsigned char*)s.batch+offsetof(IosEthRxBatch,count),0);
        put32((unsigned char*)s.batch+offsetof(IosEthRxBatch,status),(uint32_t)rc);
    }
    host.free_dma(host.user,s.dma,s.dma_address);
    r->completed_batch=s.batch;r->completed_address=s.batch_address;
    r->completed_closing=r->closing;
    r->busy=0;
    r->needs_drain=1;
    return 0; /* Message consumed even if USB/frame validation failed. */
}
void rtl_ios_firmware_rx_finish(void *opaque)
{
    RtlIosFirmwareRx *r=opaque;
    if(!r||r->busy||!r->needs_drain)return;
    RtlIosFirmwareRxHost host=r->host;
    void *batch=r->completed_batch;IosAddress address=r->completed_address;
    int closing=r->completed_closing;
    r->completed_batch=NULL;r->completed_address=0;
    r->needs_drain=0;
    if(closing)host.discard(host.user,batch,address);
    else host.rx_done(host.user,batch,address,0);
    host.drain(host.user); /* May destroy r: no access below. */
}
int rtl_ios_firmware_rx_close(RtlIosFirmwareRx *r)
{
    if(!r||r->busy)return -1;
    r->closing=1;
    return rtl_ios_firmware_rx_drained(r)?0:-2;
}
int rtl_ios_firmware_rx_drained(const RtlIosFirmwareRx *r)
{
    if(!r||r->busy||r->needs_drain)return 0;
    return !r->slots[0].pending&&!r->slots[1].pending;
}
int rtl_ios_firmware_rx_attach(RtlIosFirmwareRx *r,IosAddress context,IosAddress controller)
{
    if(!r||active_rx||!r->host.alloc_dma||r->busy||r->closing||
       !context||(context&3u)||!controller||(controller&3u))return -1;
    active_rx=r;active_context=context;active_controller=controller;return 0;
}
int rtl_ios_firmware_rx_detach(RtlIosFirmwareRx *r)
{
    if(!r||r!=active_rx||!r->closing||!rtl_ios_firmware_rx_drained(r)||r->needs_drain)return -1;
    active_rx=NULL;active_context=active_controller=0;return 0;
}
int rtl_ios_firmware_receive_async(IosAddress context,IosAddress controller,void *batch)
{
    if(!active_rx||context!=active_context||controller!=active_controller||!batch||
       (uintptr_t)batch>UINT32_MAX)return -1;
    IosAddress address=(IosAddress)(uintptr_t)batch;
    if((address&3u)||address>UINT32_MAX-(0x467u+IOS_ETH_RX_TRANSFER_BYTES))return -1;
    IosAddress ops=be32((const unsigned char*)(uintptr_t)controller);
    if(!ops||(ops&3u)||ops>UINT32_MAX-sizeof(IosEthDriverOps))return -1;
    if(be32((const unsigned char*)(uintptr_t)(ops+0x14))!=IOS_ETH_RX_TRANSFER_BYTES||
       be32((const unsigned char*)(uintptr_t)(ops+0x18))!=0)return -1;
    uint32_t dma=be32((unsigned char*)batch+IOS_ETH_RX_BATCH_BUFFER);
    return rtl_ios_firmware_rx_submit(active_rx,batch,address,
        0x467u+IOS_ETH_RX_TRANSFER_BYTES,(void*)(uintptr_t)dma,IOS_ETH_RX_TRANSFER_BYTES);
}
