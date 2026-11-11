#include "firmware_rx_binding.h"

enum { ALLOC=0x123e4394u, FREE=0x123e439cu, PENDING=0x123b6f28u,
       RX_DONE=0x123b8174u, DRAIN=0x123b936cu };
typedef void *(*Alloc)(uint32_t,uint32_t,uint32_t);
typedef int (*Free)(uint32_t,void*);
typedef void (*Pending)(IosAddress,IosAddress,uint32_t,int);
typedef int (*Done)(IosAddress,IosAddress,void*,uint32_t);
typedef int (*Drain)(IosAddress,IosAddress);

static int alloc_dma(void *user,uint32_t bytes,void **ptr,IosAddress *address)
{
    RtlIosFirmwareRxBinding *b=user;
    if(!b||!b->verified||!bytes||!ptr||!address)return -1;
    void *p=((Alloc)(uintptr_t)ALLOC)(0xcaff,bytes,64);
    if(!p)return -1;
    if((uintptr_t)p>UINT32_MAX||((uintptr_t)p&63u)){
        if(((Free)(uintptr_t)FREE)(0xcaff,p)<0)b->free_error=1;
        return -1;
    }
    *ptr=p;*address=(IosAddress)(uintptr_t)p;return 0;
}
static void free_dma(void *user,void *ptr,IosAddress address)
{
    RtlIosFirmwareRxBinding *b=user;
    if(!b||!b->verified||!ptr||(uintptr_t)ptr!=address)return;
    if(((Free)(uintptr_t)FREE)(0xcaff,ptr)<0)b->free_error=1;
}
static void pending(void *user,int delta)
{
    RtlIosFirmwareRxBinding *b=user;
    if(!b||!b->verified||(delta!=1&&delta!=-1))return;
    ((Pending)(uintptr_t)PENDING)(b->context,b->controller,1,delta);
}
static void rx_done(void *user,void *batch,IosAddress address,uint32_t pacing)
{
    RtlIosFirmwareRxBinding *b=user;
    if(!b||!b->verified||!batch||(uintptr_t)batch!=address)return;
    ((Done)(uintptr_t)RX_DONE)(b->context,b->controller,batch,pacing);
}
static void drain(void *user)
{
    RtlIosFirmwareRxBinding *b=user;
    if(!b||!b->verified||b->free_error)return;
    IosAddress context=b->context,controller=b->controller;
    ((Drain)(uintptr_t)DRAIN)(context,controller);
    /* Controller may have been freed. No access to b below. */
}
int rtl_ios_firmware_rx_bind(RtlIosFirmwareRxBinding *b,IosAddress context,
                             IosAddress controller,
                             int (*read)(void*,IosAddress,void*,uint32_t),
                             void *user,RtlIosFirmwareRxHost *out)
{
    if(!b||!out||!read||!context||(context&3u)||!controller||(controller&3u))return -1;
    const uint32_t addresses[]={ALLOC,FREE,PENDING,RX_DONE,DRAIN};
    const uint32_t hashes[]={0x49d246fdu,0xffc5e905u,0xfa516adcu,0xb63875c7u,0x9bf895f5u};
    for(unsigned i=0;i<5;++i){
        unsigned char bytes[64];uint32_t hash=2166136261u;
        if(read(user,addresses[i],bytes,sizeof(bytes)))return -1;
        for(unsigned j=0;j<sizeof(bytes);++j)hash=(hash^bytes[j])*16777619u;
        if(hash!=hashes[i])return -1;
    }
    *b=(RtlIosFirmwareRxBinding){.context=context,.controller=controller,
        .read=read,.read_user=user,.verified=1};
    *out=(RtlIosFirmwareRxHost){b,alloc_dma,free_dma,pending,rx_done,free_dma,drain};
    return 0;
}
