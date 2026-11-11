#include "../native/firmware_tx.h"
#include "../native/completion_tokens.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    RtlIosFirmwareTx tx;
    RtlIosCompletionTokens tokens;
    void *dma;IosAddress dma_address;
    RtlIosUhsCompletion wire;
    unsigned allocs,frees,submits,deliveries,drains;
    int pending,fail_submit,fail_alloc,last_status;
    uint32_t bytes;
} Fixture;
static void put(void *v,uint32_t n)
{unsigned char *p=v;p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static uint32_t get(const void *v)
{const unsigned char *p=v;return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static int alloc(void *u,uint32_t n,void **p,IosAddress *a)
{
    Fixture *f=u;assert(n==16384&&!f->dma);if(f->fail_alloc)return -1;
    *p=aligned_alloc(64,n);assert(*p);*a=0x22000000;
    f->dma=*p;f->dma_address=*a;++f->allocs;return 0;
}
static void release(void *u,void *p,IosAddress a)
{Fixture *f=u;assert(p==f->dma&&a==f->dma_address);free(p);f->dma=NULL;++f->frees;}
static void pending(void *u,int delta)
{Fixture *f=u;f->pending+=delta;assert(f->pending==0||f->pending==1);}
static void done(void *u,void *batch,IosAddress a)
{
    Fixture *f=u;assert(a==0x23000000&&!f->pending&&!f->dma);
    f->last_status=(int32_t)get((char*)batch+0x3c);++f->deliveries;free(batch);
}
static void drain(void *u){Fixture *f=u;assert(!f->dma&&!f->pending);++f->drains;}
static int control(void *u,uint32_t h,uint8_t r,uint8_t t,uint16_t v,
                   uint16_t i,void *p,uint16_t n,uint32_t timeout)
{(void)u;(void)h;(void)r;(void)t;(void)v;(void)i;(void)p;(void)n;(void)timeout;return 0;}
static int bulk(void *u,uint32_t h,uint8_t ep,unsigned dir,void *p,
                uint32_t n,const RtlIosUhsCompletion *m,int q)
{
    Fixture *f=u;assert(h==2&&ep==2&&dir==1&&q==71&&(uintptr_t)p==f->dma_address);
    assert(m->kind==2&&m->buffer!=f->dma_address);
    unsigned char *data=f->dma;
    /* Two odd length frames become four-byte-aligned RTL records. */
    assert(n==141&&data[0]==61&&data[8]==0x42&&data[72]==61&&data[80]==0x43);
    f->wire=*m;f->bytes=n;++f->submits;return f->fail_submit?-64:0;
}
static void init(Fixture *f)
{
    memset(f,0,sizeof(*f));RtlIosUhsBinding wrapped;
    RtlIosUhsBinding b={.uhs=f,.interface_id=7,.interface_handle=2,.bulk_in=0x81,
        .bulk_out=2,.completion_queue=71,.control_sync=control,.bulk_async=bulk};
    RtlIosFirmwareTxHost host={f,alloc,release,pending,done,drain};
    assert(rtl_ios_tokens_init(&f->tokens,&b,0x1000,0x2000,
        rtl_ios_firmware_tx_complete,&f->tx,&wrapped)==0);
    assert(rtl_ios_firmware_tx_init(&f->tx,&wrapped,&host)==0);
    assert(rtl_ios_tokens_set_after(&f->tokens,rtl_ios_firmware_tx_finish,&f->tx)==0);
}
static void *batch(void)
{
    char *b=calloc(1,0x4448);assert(b);put(b+0x38,2);put(b+0x40,140);
    for(unsigned i=0;i<2;++i){
        put(b+0x44+i*8,0x23000000+0x448+i*70);put(b+0x48+i*8,61);
        memset(b+0x448+i*70+8,0x42+i,61);
    }
    return b;
}
int main(void)
{
    for(unsigned test=0;test<4;++test){
        Fixture f;init(&f);void *b=batch();
        assert(rtl_ios_firmware_tx_submit(&f.tx,b,0x23000000,0x4448)==0&&f.pending==1&&!f.frees);
        if(test==3){assert(rtl_ios_firmware_tx_close(&f.tx)==-2&&!f.frees);rtl_ios_tokens_close(&f.tokens);}
        int result=test==0?(int)f.bytes:test==1?(int)f.bytes-1:-64,outcome=99;
        RtlIosUhsCompletion wire=f.wire;
        assert(rtl_ios_tokens_complete(&f.tokens,0x1000,0x2000,2,wire.batch,wire.buffer,result,&outcome)==0&&outcome==0);
        assert(f.deliveries==1&&f.frees==1&&f.drains==1&&!f.pending);
        assert(f.last_status==(test==0?141:test==1?-1:-64));
        assert(rtl_ios_tokens_complete(&f.tokens,0x1000,0x2000,2,wire.batch,wire.buffer,result,&outcome)<0);
        assert(f.deliveries==1&&rtl_ios_firmware_tx_drained(&f.tx));
    }
    Fixture f;init(&f);void *b=batch();char original[0x448];memcpy(original,b,sizeof(original));
    f.fail_alloc=1;
    assert(rtl_ios_firmware_tx_submit(&f.tx,b,0x23000000,0x4448)<0&&!f.allocs);
    f.fail_alloc=0;f.fail_submit=1;
    assert(rtl_ios_firmware_tx_submit(&f.tx,b,0x23000000,0x4448)==-64);
    assert(!f.pending&&!f.deliveries&&f.frees==f.allocs&&!memcmp(b,original,sizeof(original)));
    f.fail_submit=0;put((char*)b+0x44,0x23000449);
    assert(rtl_ios_firmware_tx_submit(&f.tx,b,0x23000000,0x4448)<0&&f.submits==1&&f.frees==f.allocs);
    free(b);
    puts("firmware TX: caller-owned batch, aligned repack, token metadata, success/short/cancel result, pending/free/done/drain ordering, failed submit ownership and duplicate rejection passed");
}
