#include "firmware_tx_binding.h"

static int alloc_dma(void *u,uint32_t n,void **p,IosAddress *a)
{
    RtlIosFirmwareTxBinding *b=u;
    return b->memory.alloc_dma(b->memory.user,n,p,a);
}
static void free_dma(void *u,void *p,IosAddress a)
{
    RtlIosFirmwareTxBinding *b=u;b->memory.free_dma(b->memory.user,p,a);
}
static void pending(void *u,int delta)
{
    RtlIosFirmwareTxBinding *b=u;
    if(!b->common.verified||(delta!=1&&delta!=-1))return;
    typedef void (*Counter)(IosAddress,IosAddress,uint32_t,int);
    ((Counter)(uintptr_t)0x123b6f28u)(b->common.context,b->common.controller,2,delta);
}
static void tx_done(void *u,void *batch,IosAddress address)
{
    RtlIosFirmwareTxBinding *b=u;
    if(!b->common.verified||!batch||(uintptr_t)batch!=address)return;
    typedef int (*Done)(IosAddress,IosAddress,void*);
    ((Done)(uintptr_t)0x123b8034u)(b->common.context,b->common.controller,batch);
}
static void drain(void *u)
{
    RtlIosFirmwareTxBinding *b=u;
    void (*fn)(void*)=b->memory.drain;void *user=b->memory.user;
    fn(user); /* May destroy b. */
}
int rtl_ios_firmware_tx_bind(RtlIosFirmwareTxBinding *b,IosAddress context,
                             IosAddress controller,
                             int (*read)(void*,IosAddress,void*,uint32_t),
                             void *user,RtlIosFirmwareTxHost *out)
{
    if(!b||!read||!out)return -1;
    unsigned char bytes[64];uint32_t hash=2166136261u;
    if(read(user,0x123b8034u,bytes,sizeof(bytes)))return -1;
    for(unsigned i=0;i<sizeof(bytes);++i)hash=(hash^bytes[i])*16777619u;
    if(hash!=0xe2eb4f4bu)return -1;
    /* Verify common functions into temporary storage so mismatch leaves both
     * caller outputs unchanged, then repair the stored host's self pointer. */
    RtlIosFirmwareTxBinding next;
    if(rtl_ios_firmware_rx_bind(&next.common,context,controller,read,user,&next.memory))return -1;
    *b=next;b->memory.user=&b->common;
    *out=(RtlIosFirmwareTxHost){b,alloc_dma,free_dma,pending,tx_done,drain};
    return 0;
}
