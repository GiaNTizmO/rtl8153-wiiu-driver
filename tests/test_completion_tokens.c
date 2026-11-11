#include "../native/completion_tokens.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static RtlIosUhsCompletion wire,received;
static unsigned submits,deliveries,done;
static int submit_result,status;
static int bulk(void *u,uint32_t h,uint8_t ep,unsigned dir,void *data,
                uint32_t n,const RtlIosUhsCompletion *m,int q)
{
    assert(u==(void*)1 && h==2 && ep==1 && dir==2 && n==512 && q==3);
    assert(data==(void*)0x21000000 && m->buffer!=(uintptr_t)data);
    ++submits;wire=*m;return submit_result;
}
static int control(void *u,uint32_t h,uint8_t r,uint8_t t,uint16_t v,
                   uint16_t i,void *d,uint16_t n,uint32_t timeout)
{(void)u;(void)h;(void)r;(void)t;(void)v;(void)i;(void)d;(void)n;(void)timeout;return 0;}
static int deliver(void *u,const RtlIosUhsCompletion *m,int rc,uint32_t bytes)
{
    assert(u==(void*)2);++deliveries;received=*m;status=rc;done=bytes;
    return rc<0?-7:0;
}
int main(void)
{
    RtlIosCompletionTokens tokens;
    RtlIosUhsBinding base={.uhs=(void*)1,.interface_handle=2,.interface_id=7,
        .completion_queue=3,.bulk_in=0x81,.bulk_out=2,.bulk_async=bulk,.control_sync=control};
    RtlIosUhsBinding wrapped;
    assert(rtl_ios_tokens_init(&tokens,&base,0x1000,0x2000,deliver,(void*)2,&wrapped)==0);
    wrapped.completion_cookie=42;
    assert(rtl_ios_tokens_callback(0x1000,0x2000,1,0x20000000,1,48)<0);
    assert(rtl_ios_tokens_attach(&tokens)==0 && rtl_ios_tokens_attach(&tokens)<0);
    assert(rtl_ios_tokens_init(&tokens,&base,0x1000,0x2000,deliver,(void*)2,&wrapped)<0);
    submit_result=-64;
    assert(rtl_ios_uhs_submit(&wrapped,1,0x20000000,0x21000000,512)==-64);
    uint32_t failed_token=wire.buffer;
    assert(rtl_ios_tokens_drained(&tokens));
    int outcome=123;
    assert(rtl_ios_tokens_complete(&tokens,0x1000,0x2000,1,wire.batch,wire.buffer,48,&outcome)<0);
    assert(outcome==123 && !deliveries);
    submit_result=0;
    assert(rtl_ios_uhs_submit(&wrapped,1,0x20000000,0x21000000,512)==0);
    assert(wire.buffer!=failed_token && !rtl_ios_tokens_drained(&tokens));
    assert(rtl_ios_tokens_detach(&tokens)<0);
    assert(rtl_ios_tokens_complete(&tokens,0x1000,0x2004,1,wire.batch,wire.buffer,48,&outcome)<0);
    assert(rtl_ios_tokens_complete(&tokens,0x1000,0x2000,2,wire.batch,wire.buffer,48,&outcome)<0);
    assert(rtl_ios_tokens_complete(&tokens,0x1000,0x2000,1,wire.batch+4,wire.buffer,48,&outcome)<0);
    assert(!deliveries);
    assert(rtl_ios_tokens_callback(0x1000,0x2000,1,wire.batch,wire.buffer,48)==0);
    assert(deliveries==1 && status==0 && done==48);
    assert(received.buffer==0x21000000 && received.batch==0x20000000 && received.cookie==42);
    assert(rtl_ios_tokens_callback(0x1000,0x2000,1,wire.batch,wire.buffer,48)==0 && deliveries==1);
    assert(rtl_ios_tokens_complete(&tokens,0x1000,0x2000,1,wire.batch,wire.buffer,48,&outcome)<0);
    /* Last token is consumed without wrapping into a previous generation. */
    tokens.next_token=UINT32_MAX;
    assert(rtl_ios_uhs_submit(&wrapped,1,0x20000000,0x21000000,512)==0 && wire.buffer==UINT32_MAX);
    unsigned before=submits;
    assert(rtl_ios_uhs_submit(&wrapped,1,0x20001000,0x21010000,512)<0 && submits==before);
    assert(rtl_ios_tokens_complete(&tokens,0x1000,0x2000,1,wire.batch,wire.buffer,-64,&outcome)==0);
    assert(outcome==-7 && deliveries==2 && status==-64 && !done && rtl_ios_tokens_drained(&tokens));
    rtl_ios_tokens_close(&tokens);
    assert(rtl_ios_uhs_submit(&wrapped,1,0x20000000,0x21000000,512)<0);
    assert(submits==before);
    assert(rtl_ios_tokens_detach(&tokens)==0 && rtl_ios_tokens_detach(&tokens)<0);
    puts("completion tokens: failed-submit retirement, mismatch/duplicate rejection, signed result, original cookie/DMA restoration, no token wrap and close passed");
}
