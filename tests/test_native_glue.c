/* Reuse the RTL8153B register model so open reaches real RX submissions. */
#define main core_register_fixture_main
#include "test_ios_core.c"
#undef main
#include "../native/native_glue.h"
#include "../native/completion_tokens.h"
#include <stdlib.h>

typedef struct {
    unsigned allocs, frees, fail_alloc, submits, fail_submit, deliveries, tx_done;
    int reject_rx, tx_status, cancel_result;
    unsigned cancels;
    void *ptrs[32];
    RtlIosUhsCompletion meta[32];
    uint32_t lengths[32];
} GlueMock;
static int alloc_mem(void *u, size_t n, size_t a, void **p, IosAddress *addr)
{
    GlueMock *m = u;
    if (++m->allocs == m->fail_alloc) return -1;
    assert(m->allocs < 32 && a == 64);
    *p = aligned_alloc(a, (n+a-1)&~(a-1)); assert(*p);
    m->ptrs[m->allocs] = *p;
    *addr = 0x13000000u + m->allocs * 0x10000u;
    return 0;
}
static void free_mem(void *u, void *p, IosAddress a)
{
    GlueMock *m = u; unsigned i = (a - 0x13000000u) / 0x10000u;
    assert(i < 32 && m->ptrs[i] == p); m->ptrs[i] = NULL;
    ++m->frees; free(p);
}
static int deliver(void *u, void *b, IosAddress a, uint32_t cookie)
{
    GlueMock *m = u; assert(b && a && cookie); ++m->deliveries; return m->reject_rx ? -1 : 0;
}
static int link_cb(void *u, const IosEthLink *l) { (void)u; (void)l; return 0; }
static void tx_done(void *u, void *b, IosAddress a, int status)
{
    GlueMock *m = u; assert(b && a); ++m->tx_done; m->tx_status = status;
}
static int control(void *u, uint32_t h, uint8_t req, uint8_t type, uint16_t v,
                   uint16_t idx, void *data, uint16_t n, uint32_t timeout)
{
    (void)u; (void)h; (void)timeout;
    int rc = transfer(regs, !!(type & 0x80), req, v, idx, data, n);
    return rc < 0 ? rc : 0;
}
static int bulk(void *u, uint32_t h, uint8_t ep, unsigned dir, void *p,
                uint32_t n, const RtlIosUhsCompletion *meta, int q)
{
    GlueMock *m = u; (void)h; (void)ep; (void)dir; (void)p; (void)q;
    unsigned i = m->submits++; assert(i < 32);
    m->meta[i] = *meta; m->lengths[i] = n;
    return m->submits == m->fail_submit ? -77 : 0;
}
static RtlIosNativeHost host(GlueMock *m)
{
    RtlIosNativeHost h = {m, alloc_mem, free_mem, deliver, link_cb, tx_done}; return h;
}
static int cancel_endpoints(void *u,uint32_t handle,uint32_t op,uint32_t mask,
                            uint32_t a4,uint32_t a5)
{
    GlueMock *m=u;
    assert(handle==2 && op==2 && mask==((1u<<17)|(1u<<2)) && !a4 && !a5);
    ++m->cancels;return m->cancel_result;
}
static RtlIosUhsBinding binding(GlueMock *m)
{
    RtlIosUhsBinding b = {.uhs=m, .interface_handle=2, .interface_id=7,
        .completion_queue=1, .bulk_in=0x81, .bulk_out=2, .control_sync=control,
        .bulk_async=bulk, .user=regs, .sleep_ms=pause_ms, .cancelled=is_cancelled};
    return b;
}
static void rx_frame(RtlIosNativeDriver *d, unsigned i)
{
    uint8_t *p = d->slots[i].dma_ptr;
    memset(p, 0, 48); p[0] = 18; /* 14 bytes Ethernet + 4 FCS + padding. */
    memset(p+24, 0xa5, 14);
}
static void finish_close(RtlIosNativeDriver *d, GlueMock *m)
{
    assert(rtl_ios_native_close(d) == -2);
    for (unsigned i=0; i<2; ++i) {
        RtlIosOwnedSlot *s = &d->owner.slots[i];
        if (s->state == RTL_SLOT_PENDING) {
            RtlIosUhsCompletion c = {.interface_id=d->owner.interface_id, .kind=s->kind,
                                    .batch=s->batch, .buffer=s->dma, .cookie=s->cookie};
            assert(rtl_ios_native_complete(d, &c, -17, 0) == 0);
        }
    }
    assert(rtl_ios_native_close(d) == 0);
    for (unsigned i=0; i<32; ++i) assert(!m->ptrs[i]);
}
static void put32(uint8_t *p, uint32_t v)
{ p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static int token_delivery(void *u,const RtlIosUhsCompletion *m,int status,uint32_t done)
{ return rtl_ios_native_complete(u,m,status,done); }
int main(void)
{
    /* Every partial allocation is released, including batch-before-DMA. */
    for (unsigned f=1; f<=4; ++f) {
        setup(); GlueMock m={.fail_alloc=f}; RtlIosNativeDriver d={0};
        RtlIosNativeHost h=host(&m); RtlIosUhsBinding b=binding(&m);
        assert(rtl_ios_native_open(&d,&b,&h)==-1 && d.state==RTL_NATIVE_CLOSED);
        assert(m.frees==f-1);
    }
    setup(); GlueMock m={0}; RtlIosNativeDriver d={0};
    RtlIosNativeHost h=host(&m); RtlIosUhsBinding b=binding(&m);
    assert(rtl_ios_native_open(&d,&b,&h)==0 && m.submits==2);
    rx_frame(&d,0);
    assert(rtl_ios_native_complete_pair(&d,m.meta[0].kind,m.meta[0].batch,
                                        m.meta[0].buffer,0,48)==0);
    assert(m.deliveries==1 && m.submits==2 && d.owner.slots[0].state==RTL_SLOT_DELIVERED);
    assert(rtl_ios_native_complete(&d,&m.meta[0],0,48)<0);
    assert(rtl_ios_native_release_rx(&d,m.meta[0].batch,m.meta[0].cookie+1)<0);
    assert(rtl_ios_native_release_rx(&d,m.meta[0].batch,m.meta[0].cookie)==0);
    assert(m.submits==3 && d.owner.slots[0].state==RTL_SLOT_PENDING);
    assert(rtl_ios_native_release_rx(&d,m.meta[0].batch,m.meta[0].cookie)<0);
    /* Short TX must complete exactly once with normalized failure. */
    uint8_t tx[sizeof(IosEthTxBatch)+22]={0}; uint32_t ta=0x14000000;
    put32(tx+offsetof(IosEthTxBatch,count),1);
    put32(tx+offsetof(IosEthTxBatch,transfer_bytes),22);
    put32(tx+offsetof(IosEthTxBatch,frames),ta+sizeof(IosEthTxBatch));
    put32(tx+offsetof(IosEthTxBatch,frames)+4,14);
    assert(rtl_ios_native_send_async(&d,tx,ta,sizeof(tx))==0);
    assert(rtl_ios_native_complete(&d,&m.meta[3],0,m.lengths[3]-1)<0);
    assert(m.tx_done==1 && m.tx_status<0);
    assert(rtl_ios_native_complete(&d,&m.meta[3],0,m.lengths[3])<0 && m.tx_done==1);
    assert(rtl_ios_native_send_async(&d,tx,ta,sizeof(tx))==0);
    assert(rtl_ios_native_complete(&d,&m.meta[4],0,m.lengths[4])==0);
    assert(m.tx_done==2 && m.tx_status==0);
    /* A retained RX blocks close even after all USB requests finish. */
    rx_frame(&d,0);
    assert(rtl_ios_native_complete(&d,&m.meta[2],0,48)==0);
    assert(rtl_ios_native_close(&d)==-2 && m.deliveries==2);
    assert(rtl_ios_native_complete(&d,&m.meta[1],0,0)==0);
    assert(m.deliveries==2 && rtl_ios_native_close(&d)==-2);
    assert(rtl_ios_native_release_rx(&d,m.meta[2].batch,m.meta[2].cookie)==0);
    assert(rtl_ios_native_close(&d)==0 && m.frees==m.allocs);
    /* Submission failure during open retains the first pending transfer. */
    setup(); memset(&m,0,sizeof(m)); memset(&d,0,sizeof(d)); m.fail_submit=2;
    assert(rtl_ios_native_open(&d,&b,&h)==-2 && !m.frees);
    finish_close(&d,&m);
    /* A rejected input batch never remains owned by the stack. */
    setup(); memset(&m,0,sizeof(m)); memset(&d,0,sizeof(d)); m.reject_rx=1;
    assert(rtl_ios_native_open(&d,&b,&h)==0); rx_frame(&d,0);
    assert(rtl_ios_native_complete(&d,&m.meta[0],0,48)<0 && m.frees==2);
    finish_close(&d,&m);
    /* Oversized RX must use the owner's normalized failure and never deliver. */
    setup(); memset(&m,0,sizeof(m)); memset(&d,0,sizeof(d));
    assert(rtl_ios_native_open(&d,&b,&h)==0); rx_frame(&d,0);
    assert(rtl_ios_native_complete(&d,&m.meta[0],0,32769)<0);
    assert(!m.deliveries && m.frees==2); finish_close(&d,&m);
    /* Release after stack ownership cannot leave PREPARED memory on submit error. */
    setup(); memset(&m,0,sizeof(m)); memset(&d,0,sizeof(d));
    assert(rtl_ios_native_open(&d,&b,&h)==0); rx_frame(&d,0);
    assert(rtl_ios_native_complete(&d,&m.meta[0],0,48)==0); m.fail_submit=3;
    assert(rtl_ios_native_release_rx(&d,m.meta[0].batch,m.meta[0].cookie)<0);
    assert(d.owner.slots[0].state==RTL_SLOT_FREE && m.frees==2); finish_close(&d,&m);
    /* Cancel success is not completion. A failed cancel preserves every
     * pending slot/DMA allocation and is retryable. Retained RX blocks close. */
    setup(); memset(&m,0,sizeof(m)); memset(&d,0,sizeof(d));
    b.endpoint_admin=cancel_endpoints;
    assert(rtl_ios_native_open(&d,&b,&h)==0);rx_frame(&d,0);
    assert(rtl_ios_native_complete(&d,&m.meta[0],0,48)==0);
    m.cancel_result=-64;
    assert(rtl_ios_native_close(&d)==-64 && m.cancels==1 && !m.frees);
    assert(d.state==RTL_NATIVE_CLOSING && d.owner.slots[1].state==RTL_SLOT_PENDING);
    assert(rtl_ios_native_release_rx(&d,m.meta[0].batch,m.meta[0].cookie)==0);
    assert(m.frees==2 && rtl_ios_native_send_async(&d,tx,ta,sizeof(tx))<0);
    m.cancel_result=0;
    assert(rtl_ios_native_close(&d)==-2 && m.cancels==2 && m.frees==2);
    assert(rtl_ios_native_close(&d)==-2 && m.cancels==2 && m.frees==2);
    assert(rtl_ios_native_complete(&d,&m.meta[1],-64,0)==0 && m.frees==4);
    assert(rtl_ios_native_complete(&d,&m.meta[1],-64,0)<0);
    assert(rtl_ios_native_close(&d)==0 && m.frees==m.allocs && m.cancels==2);
    /* Partial-open failure must also cancel the first accepted submission. */
    setup(); memset(&m,0,sizeof(m)); memset(&d,0,sizeof(d));m.fail_submit=2;
    assert(rtl_ios_native_open(&d,&b,&h)==-2 && !m.cancels && !m.frees);
    assert(rtl_ios_native_close(&d)==-2 && m.cancels==1 && !m.frees);
    assert(rtl_ios_native_complete(&d,&m.meta[0],-64,0)==0);
    assert(rtl_ios_native_close(&d)==0 && m.frees==m.allocs);
    /* Real DMA reuse with independent per-request tokens. The downstream
     * mock records precisely the metadata that stock worker would forward. */
    setup();memset(&m,0,sizeof(m));memset(&d,0,sizeof(d));
    RtlIosCompletionTokens tokens;RtlIosUhsBinding wrapped;
    assert(rtl_ios_tokens_init(&tokens,&b,0x20000000,0x20020000,
                               token_delivery,&d,&wrapped)==0);
    assert(rtl_ios_native_open(&d,&wrapped,&h)==0);
    RtlIosUhsCompletion stale=m.meta[0];
    int outcome=123;
    for(unsigned round=0;round<20;++round){
        unsigned submitted=round?round+1:0;
        RtlIosUhsCompletion wire=m.meta[submitted];
        IosAddress real_dma=d.owner.slots[0].dma;
        uint32_t cookie=d.owner.slots[0].cookie;
        assert(wire.buffer!=real_dma && wire.batch==d.owner.slots[0].batch);
        assert(rtl_ios_tokens_complete(&tokens,0x20000004,0x20020000,1,
                                       wire.batch,wire.buffer,48,&outcome)<0);
        if(round)assert(rtl_ios_tokens_complete(&tokens,0x20000000,0x20020000,1,
                                                stale.batch,stale.buffer,48,&outcome)<0);
        rx_frame(&d,0);
        assert(rtl_ios_tokens_complete(&tokens,0x20000000,0x20020000,1,
                                       wire.batch,wire.buffer,48,&outcome)==0 && outcome==0);
        assert(rtl_ios_tokens_complete(&tokens,0x20000000,0x20020000,1,
                                       wire.batch,wire.buffer,48,&outcome)<0);
        assert(rtl_ios_native_release_rx(&d,wire.batch,cookie)==0);
        assert(d.owner.slots[0].dma==real_dma);
    }
    assert(m.deliveries==20 && !m.frees && !rtl_ios_tokens_drained(&tokens));
    assert(rtl_ios_native_send_async(&d,tx,ta,sizeof(tx))==0);
    RtlIosUhsCompletion short_tx=m.meta[22];
    assert(rtl_ios_tokens_complete(&tokens,0x20000000,0x20020000,2,
                                   short_tx.batch,short_tx.buffer,
                                   (int)m.lengths[22]-1,&outcome)==0 && outcome<0);
    assert(m.tx_done==1 && m.tx_status<0 && m.frees==1);
    assert(rtl_ios_native_send_async(&d,tx,ta,sizeof(tx))==0);
    assert(rtl_ios_tokens_complete(&tokens,0x20000000,0x20020000,2,
                                   short_tx.batch,short_tx.buffer,
                                   (int)m.lengths[22],&outcome)<0);
    assert(m.tx_done==1 && m.frees==1);
    RtlIosUhsCompletion good_tx=m.meta[23];
    assert(rtl_ios_tokens_complete(&tokens,0x20000000,0x20020000,2,
                                   good_tx.batch,good_tx.buffer,
                                   (int)m.lengths[23],&outcome)==0 && outcome==0);
    assert(m.tx_done==2 && m.tx_status==0 && m.frees==2);
    assert(rtl_ios_native_send_async(&d,tx,ta,sizeof(tx))==0);
    rtl_ios_tokens_close(&tokens);
    assert(rtl_ios_native_close(&d)==-2 && m.cancels==1 && m.frees==2);
    RtlIosUhsCompletion cancel_tx=m.meta[24];
    assert(rtl_ios_tokens_complete(&tokens,0x20000000,0x20020000,2,
                                   cancel_tx.batch,cancel_tx.buffer,-64,&outcome)==0 && outcome<0);
    assert(m.tx_done==3 && m.tx_status==-64 && m.frees==3);
    for(unsigned i=0;i<2;++i){
        unsigned index=i?1:21;
        RtlIosUhsCompletion wire=m.meta[index];
        assert(rtl_ios_tokens_complete(&tokens,0x20000000,0x20020000,1,
                                       wire.batch,wire.buffer,-64,&outcome)==0 && outcome==0);
    }
    assert(rtl_ios_tokens_drained(&tokens));
    assert(rtl_ios_native_close(&d)==0 && m.frees==m.allocs);
    puts("native glue: lifecycle/cancel/drain, 20 tokenized RX cycles, short/success/cancelled TX and stale callback rejection passed");
    return 0;
}
