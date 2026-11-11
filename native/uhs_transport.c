#include "uhs_transport.h"
#include <string.h>
static int cancelled(RtlIosUhsBinding *b)
{return b->cancelled&&b->cancelled(b->user);}
static int control(void *opaque,int read,uint8_t request,uint16_t value,
                    uint16_t index,void *data,uint16_t bytes)
{
    RtlIosUhsBinding *b=opaque;
    if(!b||!b->uhs||!b->control_sync||!data||
       ((uintptr_t)data&63)||!bytes||bytes>64||cancelled(b))return -1;
    /* 123bdbd4: request is r2, bmRequestType r3, then wValue/wIndex,
     * DMA pointer, wLength and timeout. Vendor device request: c0/40. */
    int rc=b->control_sync(b->uhs,b->interface_handle,request,read?0xc0:0x40,
                           value,index,data,bytes,1000000);
    if(rc<0)return rc;
    /* IOS ioctl success is status 0. Accept a byte-count success as well,
     * but never turn a positive short transfer into a full transfer. Native
     * status 0 exposes no actual length; hardware validation is still needed. */
    return rc==0||rc==bytes?(int)bytes:-1;
}
static void sleep_ms(void *opaque,unsigned ms)
{RtlIosUhsBinding *b=opaque;b->sleep_ms(b->user,ms);}
static int is_cancelled(void *opaque)
{return cancelled(opaque);}
int rtl_ios_uhs_transport(RtlIosUhsBinding *b,RtlIosTransport *out)
{
    if(!b||!out||!b->uhs||!b->control_sync||!b->sleep_ms)return -1;
    *out=(RtlIosTransport){b,control,sleep_ms,is_cancelled};return 0;
}
int rtl_ios_uhs_submit(RtlIosUhsBinding *b,int read,IosAddress batch,
                       IosAddress dma,uint32_t bytes)
{
    if(!b||!b->uhs||!b->bulk_async||b->completion_queue<0||
       !batch||(batch&3)||!dma||(dma&63)||!bytes||
       bytes>(read?32768u:16384u)||bytes>UINT32_MAX-dma||cancelled(b))return -1;
    uint8_t ep=read?b->bulk_in:b->bulk_out;
    if(!(ep&15)||(ep&0x70)||!!(ep&0x80)!=!!read)return -1;
    RtlIosUhsCompletion metadata={read?1u:2u,b->interface_id,batch,dma,b->completion_cookie};
    return b->bulk_async(b->uhs,b->interface_handle,ep&15,read?2u:1u,
                         (void *)(uintptr_t)dma,bytes,&metadata,b->completion_queue);
}
int rtl_ios_uhs_cancel(RtlIosUhsBinding *b)
{
    if(!b||!b->uhs||!b->endpoint_admin)return -1;
    uint8_t in=b->bulk_in,out=b->bulk_out;
    if(!(in&15)||(in&0x70)||!(in&0x80)||
       !(out&15)||(out&0xf0))return -1;
    uint32_t mask=(1u<<(16u+(in&15)))|(1u<<(out&15));
    return b->endpoint_admin(b->uhs,b->interface_handle,2,mask,0,0);
}
