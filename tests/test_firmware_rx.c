#include "../native/firmware_rx.h"
#include "../native/completion_tokens.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    RtlIosFirmwareRx rx;
    RtlIosCompletionTokens tokens;
    RtlIosUhsBinding wrapped;
    void *dma[16];IosAddress addr[16];
    RtlIosUhsCompletion wire[16];
    unsigned allocs,frees,submits,deliveries,discards,drains;
    int pending,fail_alloc,fail_submit,destroy_on_drain;
    unsigned events[128],events_count;
} Fixture;
static void event(Fixture *f,unsigned n){assert(f->events_count<128);f->events[f->events_count++]=n;}
static void put32(void *v,uint32_t n)
{unsigned char *p=v;p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static uint32_t get32(const void *v)
{const unsigned char *p=v;return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static int alloc_dma(void *u,uint32_t n,void **ptr,IosAddress *a)
{
    Fixture *f=u;assert(n==16384);if(f->fail_alloc)return -1;
    unsigned i=f->allocs++;assert(i<16);
    *ptr=aligned_alloc(64,n);assert(*ptr);*a=0x22000000+i*0x10000;
    f->dma[i]=*ptr;f->addr[i]=*a;event(f,1);return 0;
}
static void free_dma(void *u,void *p,IosAddress a)
{
    Fixture *f=u;unsigned i=(a-0x22000000)/0x10000;
    assert(i<f->allocs&&f->dma[i]==p);free(p);f->dma[i]=NULL;++f->frees;event(f,4);
}
static void pending(void *u,int delta)
{
    Fixture *f=u;assert(delta==1||delta==-1);f->pending+=delta;
    assert(f->pending>=0&&f->pending<=2);event(f,delta==1?2:3);
}
static void rx_done(void *u,void *batch,IosAddress a,uint32_t pacing)
{
    Fixture *f=u;assert(a==0x23000000||a==0x23010000);assert(!pacing);
    assert(f->frees==f->allocs-(unsigned)f->pending);
    uint32_t status=get32((char*)batch+0x3c),count=get32((char*)batch+0x38);
    if(!status){assert(count==1);assert(get32((char*)batch+0x40)==a+0x460+24);
        assert(get32((char*)batch+0x44)==14);}
    else assert(!count);
    /* Firmware/native input may take/free this memory after handoff. */
    ++f->deliveries;event(f,5);free(batch);
}
static void discard(void *u,void *batch,IosAddress a)
{Fixture *f=u;assert(a==0x23000000||a==0x23010000);++f->discards;event(f,6);free(batch);}
static void drain(void *u)
{
    Fixture *f=u;++f->drains;event(f,7);
    assert(rtl_ios_tokens_drained(&f->tokens)==(f->pending==0));
    if(f->destroy_on_drain){
        assert(!f->pending && f->frees==f->allocs);
        assert(rtl_ios_firmware_rx_detach(&f->rx)==0);
        assert(rtl_ios_tokens_detach(&f->tokens)==0);
        free(f); /* Completion layers must never touch f after final drain. */
    }
}
static int control(void *u,uint32_t h,uint8_t r,uint8_t t,uint16_t v,
                   uint16_t i,void *p,uint16_t n,uint32_t timeout)
{(void)u;(void)h;(void)r;(void)t;(void)v;(void)i;(void)p;(void)n;(void)timeout;return 0;}
static int bulk(void *u,uint32_t h,uint8_t ep,unsigned direction,void *p,
                 uint32_t n,const RtlIosUhsCompletion *m,int queue)
{
    Fixture *f=u;assert(h==2&&ep==1&&direction==2&&n==16384&&queue==71);
    assert((uintptr_t)p>=0x22000000&&(uintptr_t)p<0x23000000);
    assert(m->buffer && m->buffer!=(uintptr_t)p);
    f->wire[f->submits++]=*m;return f->fail_submit?-64:0;
}
static void init(Fixture *f)
{
    memset(f,0,sizeof(*f));
    RtlIosUhsBinding b={.uhs=f,.interface_id=7,.interface_handle=2,
        .completion_queue=71,.bulk_in=0x81,.bulk_out=2,.control_sync=control,.bulk_async=bulk};
    RtlIosFirmwareRxHost h={f,alloc_dma,free_dma,pending,rx_done,discard,drain};
    assert(rtl_ios_tokens_init(&f->tokens,&b,0x1000,0x2000,
        rtl_ios_firmware_rx_complete,&f->rx,&f->wrapped)==0);
    assert(rtl_ios_firmware_rx_init(&f->rx,&f->wrapped,&h)==0);
    assert(rtl_ios_tokens_set_after(&f->tokens,rtl_ios_firmware_rx_finish,&f->rx)==0);
}
static void *batch(IosAddress a)
{
    void *b=aligned_alloc(64,0x4480);assert(b);memset(b,0xa5,0x4480);
    put32((char*)b+0x444,a+0x460);return b;
}
static void frame(Fixture *f,unsigned i)
{unsigned char *p=f->dma[i];memset(p,0,48);p[0]=18;memset(p+24,0x42,14);}
static int complete(Fixture *f,unsigned i,int result)
{
    int outcome=123;RtlIosUhsCompletion m=f->wire[i];
    int rc=rtl_ios_tokens_complete(&f->tokens,0x1000,0x2000,1,m.batch,m.buffer,result,&outcome);
    if(rc==0)assert(outcome==0);
    return rc;
}
int main(void)
{
    Fixture f;init(&f);void *b=batch(0x23000000),*b2=batch(0x23010000);
    unsigned char before[0x448];memcpy(before,b,sizeof(before));
    assert(rtl_ios_firmware_rx_submit(&f.rx,b,0x23000000,0x4480,(char*)b+0x460,16384)==0);
    assert(memcmp(before,b,sizeof(before))==0&&f.pending==1);
    assert(rtl_ios_firmware_rx_submit(&f.rx,b,0x23000000,0x4480,(char*)b+0x460,16384)<0);
    assert(rtl_ios_firmware_rx_submit(&f.rx,b2,0x23010000,0x4480,(char*)b2+0x460,16384)==0);
    assert(f.pending==2);frame(&f,0);frame(&f,1);
    assert(complete(&f,1,48)==0&&f.deliveries==1&&f.pending==1);
    assert(complete(&f,1,48)<0&&f.deliveries==1);
    assert(complete(&f,0,48)==0&&f.deliveries==2&&f.pending==0);
    assert(f.frees==2&&f.drains==2&&rtl_ios_firmware_rx_drained(&f.rx));
    unsigned end=f.events_count;
    assert(f.events[end-4]==3&&f.events[end-3]==4&&f.events[end-2]==5&&f.events[end-1]==7);
    /* Allocation/submit errors preserve the firmware batch, no pending increment. */
    init(&f);b=batch(0x23000000);memcpy(before,b,sizeof(before));f.fail_alloc=1;
    assert(rtl_ios_firmware_rx_submit(&f.rx,b,0x23000000,0x4480,(char*)b+0x460,16384)<0);
    f.fail_alloc=0;f.fail_submit=1;
    assert(rtl_ios_firmware_rx_submit(&f.rx,b,0x23000000,0x4480,(char*)b+0x460,16384)==-64);
    assert(!memcmp(before,b,sizeof(before))&&!f.pending&&f.allocs==f.frees);
    assert(complete(&f,0,48)<0);free(b);
    /* Invalid DMA view/capacity must fail before allocation. */
    init(&f);b=batch(0x23000000);
    assert(rtl_ios_firmware_rx_submit(&f.rx,b,0x23000000,0x4480,(char*)b+0x440,16384)<0);
    assert(rtl_ios_firmware_rx_submit(&f.rx,b,0x23000000,0x4480,(char*)b+0x460,32768)<0);
    assert(!f.allocs);free(b);
    /* USB error, oversized completion and malformed aggregate each hand back
     * one failed original batch; no automatic resubmit or dangling bounce. */
    for(unsigned test=0;test<3;++test){
        init(&f);b=batch(0x23000000);
        assert(rtl_ios_firmware_rx_submit(&f.rx,b,0x23000000,0x4480,(char*)b+0x460,16384)==0);
        memset(f.dma[0],0,16384);
        assert(complete(&f,0,test==0?-64:test==1?16385:12)==0);
        assert(f.deliveries==1&&!f.pending&&f.frees==1&&f.drains==1);
    }
    init(&f);b=batch(0x23000000);
    assert(rtl_ios_firmware_rx_submit(&f.rx,b,0x23000000,0x4480,(char*)b+0x460,16384)==0);
    assert(rtl_ios_firmware_rx_close(&f.rx)==-2&&!f.frees);
    assert(complete(&f,0,-64)==0&&f.discards==1&&!f.deliveries);
    assert(rtl_ios_firmware_rx_close(&f.rx)==0&&f.frees==1&&!f.pending);
    /* Final drain may detach and destroy all controller-owned state while
     * returning through token completion. ASan must catch any later access. */
    Fixture *heap=malloc(sizeof(*heap));assert(heap);init(heap);
    assert(rtl_ios_tokens_attach(&heap->tokens)==0);
    assert(rtl_ios_firmware_rx_attach(&heap->rx,0x1000,0x2000)==0);
    b=batch(0x23000000);
    assert(rtl_ios_firmware_rx_submit(&heap->rx,b,0x23000000,0x4480,(char*)b+0x460,16384)==0);
    RtlIosUhsCompletion last=heap->wire[0];
    assert(rtl_ios_firmware_rx_close(&heap->rx)==-2);
    rtl_ios_tokens_close(&heap->tokens);heap->destroy_on_drain=1;
    assert(rtl_ios_tokens_callback(0x1000,0x2000,1,last.batch,last.buffer,-64)==0);
    puts("firmware RX: caller-owned 32-byte-aligned inline batch, aligned bounce, token completion, two out-of-order RX, failure ownership, pending/free/delivery/drain order and close passed");
}
