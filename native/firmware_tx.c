#include "firmware_tx.h"
#include <string.h>

static RtlIosFirmwareTx *active_tx;
static IosAddress active_context,active_controller;
static uint32_t be32(const unsigned char *p)
{return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put32(unsigned char *p,uint32_t v)
{p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static int overlaps(uintptr_t a,uint32_t an,uintptr_t b,uint32_t bn)
{return a<=b?b-a<an:a-b<bn;}
int rtl_ios_firmware_tx_init(RtlIosFirmwareTx *t,const RtlIosUhsBinding *b,
                            const RtlIosFirmwareTxHost *h)
{
    if(!t||t==active_tx||!b||!h||!b->interface_id||!b->bulk_async||
       !h->alloc_dma||!h->free_dma||!h->pending||!h->tx_done||!h->drain)return -1;
    RtlIosUhsBinding binding=*b;RtlIosFirmwareTxHost host=*h;
    memset(t,0,sizeof(*t));t->binding=binding;t->host=host;t->next_cookie=1;
    return 0;
}
int rtl_ios_firmware_tx_submit(RtlIosFirmwareTx *t,void *batch,IosAddress address,
                               uint32_t allocation)
{
    if(!t||t->busy||t->closing||t->completed_batch||!t->next_cookie||!batch||
       !address||(address&3u)||allocation<sizeof(IosEthTxBatch)||
       allocation>UINT32_MAX-address||(uintptr_t)batch>UINTPTR_MAX-allocation)return -1;
    unsigned index=8;
    for(unsigned i=0;i<8;++i){
        RtlIosFirmwareTxSlot *s=&t->slots[i];
        if(!s->pending){if(index==8)index=i;continue;}
        if(overlaps(address,allocation,s->batch_address,s->allocation)||
           overlaps((uintptr_t)batch,allocation,(uintptr_t)s->batch,s->allocation)||
           overlaps(address,allocation,s->dma_address,16384)||
           overlaps((uintptr_t)batch,allocation,(uintptr_t)s->dma,16384))return -1;
    }
    if(index==8)return -1;
    t->busy=1;
    void *dma=NULL;IosAddress dma_address=0;
    int rc=t->host.alloc_dma(t->host.user,16384,&dma,&dma_address);
    if(rc||!dma||!dma_address||(dma_address&63u)||((uintptr_t)dma&63u)||
       dma_address>UINT32_MAX-16384u||(uintptr_t)dma>UINTPTR_MAX-16384u||
       overlaps((uintptr_t)dma,16384,(uintptr_t)batch,allocation)||
       overlaps(dma_address,16384,address,allocation)){
        if(dma)t->host.free_dma(t->host.user,dma,dma_address);
        t->busy=0;return -1;
    }
    for(unsigned i=0;i<8;++i)if(t->slots[i].pending){
        RtlIosFirmwareTxSlot *s=&t->slots[i];
        if(overlaps(dma_address,16384,s->dma_address,16384)||
           overlaps((uintptr_t)dma,16384,(uintptr_t)s->dma,16384)||
           overlaps(dma_address,16384,s->batch_address,s->allocation)||
           overlaps((uintptr_t)dma,16384,(uintptr_t)s->batch,s->allocation)){
            t->host.free_dma(t->host.user,dma,dma_address);t->busy=0;return -1;
        }
    }
    size_t written=0;
    if(rtl_ios_repack_tx(batch,allocation,address,8,dma,16384,&written)){
        t->host.free_dma(t->host.user,dma,dma_address);t->busy=0;return -1;
    }
    RtlIosFirmwareTxSlot *s=&t->slots[index];
    *s=(RtlIosFirmwareTxSlot){.batch=batch,.dma=dma,.batch_address=address,
        .dma_address=dma_address,.allocation=allocation,.submitted=(uint32_t)written,
        .cookie=t->next_cookie++,.pending=1};
    RtlIosUhsBinding tagged=t->binding;tagged.completion_cookie=s->cookie;
    rc=rtl_ios_uhs_submit(&tagged,0,address,dma_address,(uint32_t)written);
    if(rc<0){t->host.free_dma(t->host.user,dma,dma_address);memset(s,0,sizeof(*s));}
    else t->host.pending(t->host.user,1);
    t->busy=0;return rc;
}
int rtl_ios_firmware_tx_complete(void *opaque,const RtlIosUhsCompletion *m,
                                 int status,uint32_t done)
{
    RtlIosFirmwareTx *t=opaque;
    if(!t||t->busy||t->completed_batch||!m||m->kind!=2||
       m->interface_id!=t->binding.interface_id)return -1;
    unsigned index=8;
    for(unsigned i=0;i<8;++i){
        RtlIosFirmwareTxSlot *s=&t->slots[i];
        if(s->pending&&s->batch_address==m->batch&&s->dma_address==m->buffer&&
           s->cookie==m->cookie){index=i;break;}
    }
    if(index==8)return -1;
    RtlIosFirmwareTxSlot s=t->slots[index];memset(&t->slots[index],0,sizeof(s));
    t->busy=1;t->host.pending(t->host.user,-1);
    int result=status<0?status:done==s.submitted?(int)done:-1;
    put32((unsigned char*)s.batch+offsetof(IosEthTxBatch,status),(uint32_t)result);
    t->host.free_dma(t->host.user,s.dma,s.dma_address);
    t->completed_batch=s.batch;t->completed_address=s.batch_address;
    t->busy=0;return 0;
}
void rtl_ios_firmware_tx_finish(void *opaque)
{
    RtlIosFirmwareTx *t=opaque;
    if(!t||t->busy||!t->completed_batch)return;
    RtlIosFirmwareTxHost h=t->host;
    void *batch=t->completed_batch;IosAddress address=t->completed_address;
    t->completed_batch=NULL;t->completed_address=0;
    h.tx_done(h.user,batch,address); /* May submit next TX. */
    h.drain(h.user); /* May destroy t; no access below. */
}
int rtl_ios_firmware_tx_drained(const RtlIosFirmwareTx *t)
{
    if(!t||t->busy||t->completed_batch)return 0;
    for(unsigned i=0;i<8;++i)if(t->slots[i].pending)return 0;
    return 1;
}
int rtl_ios_firmware_tx_close(RtlIosFirmwareTx *t)
{
    if(!t||t->busy)return -1;
    t->closing=1;return rtl_ios_firmware_tx_drained(t)?0:-2;
}
int rtl_ios_firmware_tx_attach(RtlIosFirmwareTx *t,IosAddress context,IosAddress controller)
{
    if(!t||active_tx||!t->host.alloc_dma||t->busy||t->closing||!context||
       (context&3u)||!controller||(controller&3u))return -1;
    active_tx=t;active_context=context;active_controller=controller;return 0;
}
int rtl_ios_firmware_tx_detach(RtlIosFirmwareTx *t)
{
    if(!t||active_tx!=t||!t->closing||!rtl_ios_firmware_tx_drained(t))return -1;
    active_tx=NULL;active_context=active_controller=0;return 0;
}
int rtl_ios_firmware_send_async(IosAddress context,IosAddress controller,void *batch)
{
    if(!active_tx||context!=active_context||controller!=active_controller||!batch||
       (uintptr_t)batch>UINT32_MAX)return -1;
    IosAddress address=(IosAddress)(uintptr_t)batch;
    uint32_t allocation=sizeof(IosEthTxBatch)+16384u;
    if((address&3u)||address>UINT32_MAX-allocation)return -1;
    IosAddress ops=be32((const unsigned char*)(uintptr_t)controller);
    if(!ops||(ops&3u)||ops>UINT32_MAX-sizeof(IosEthDriverOps)||
       be32((const unsigned char*)(uintptr_t)(ops+8))!=16384||
       be32((const unsigned char*)(uintptr_t)(ops+12))!=8)return -1;
    return rtl_ios_firmware_tx_submit(active_tx,batch,address,allocation);
}
