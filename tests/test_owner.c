#include "../native/transfer_owner.h"
#include <assert.h>
#include <stdio.h>
static RtlIosUhsCompletion messages[10];static int fail;
static int bulk(void *u,uint32_t h,uint8_t ep,unsigned dir,void *data,
 uint32_t bytes,const RtlIosUhsCompletion *m,int q)
{
    (void)u;(void)h;(void)ep;(void)dir;(void)data;(void)bytes;(void)q;
    if(fail)return -33;
    unsigned slot=(m->batch-0x12500000)/0x1000;assert(slot<10);messages[slot]=*m;return 0;
}
int main(void)
{
    RtlIosOwner o;RtlIosUhsBinding b={.uhs=(void *)1,.interface_id=7,
      .completion_queue=1,.bulk_in=0x81,.bulk_out=2,.bulk_async=bulk};
    assert(rtl_ios_owner_init(&o,7)==0&&rtl_ios_owner_drained(&o));
    for(unsigned i=0;i<10;++i){
        assert(rtl_ios_owner_prepare(&o,i,i<2,0x12500000+i*0x1000,
                                     0x12600000+i*0x10000,16384)==0);
    }
    assert(rtl_ios_owner_prepare(&o,0,1,0x12500000,0x12600000,16384)<0);
    fail=1;assert(rtl_ios_owner_submit(&o,0,&b,16384)==-33);
    assert(o.slots[0].state==RTL_SLOT_PREPARED);fail=0;
    for(unsigned i=0;i<10;++i)assert(rtl_ios_owner_submit(&o,i,&b,16384)==0);
    assert(rtl_ios_owner_release(&o,0)<0);
    assert(rtl_ios_owner_submit(&o,0,&b,16384)<0);
    RtlIosUhsCompletion wrong=messages[0];wrong.interface_id++;
    assert(rtl_ios_owner_complete(&o,&wrong,0,100)<0);
    wrong=messages[0];wrong.buffer+=64;assert(rtl_ios_owner_complete(&o,&wrong,0,100)<0);
    /* Complete out of order. The native stack holds RX0 through shutdown. */
    assert(rtl_ios_owner_complete(&o,&messages[1],0,50)==1);
    assert(rtl_ios_owner_complete(&o,&messages[0],0,100)==0);
    assert(rtl_ios_owner_complete(&o,&messages[0],0,100)<0);
    assert(rtl_ios_owner_deliver(&o,0)==0);
    assert(rtl_ios_owner_submit(&o,0,&b,16384)<0);
    assert(rtl_ios_owner_complete(&o,&messages[2],0,16383)==2&&o.slots[2].result<0);
    assert(rtl_ios_owner_complete(&o,&messages[3],0,16385)==3&&o.slots[3].result<0);
    rtl_ios_owner_close(&o);assert(!rtl_ios_owner_drained(&o));
    assert(rtl_ios_owner_deliver(&o,1)<0);
    assert(rtl_ios_owner_submit(&o,1,&b,16384)<0);
    for(unsigned i=4;i<10;++i){
        assert(rtl_ios_owner_release(&o,i)<0);
        assert(rtl_ios_owner_complete(&o,&messages[i],-64,0)==(int)i);
    }
    for(unsigned i=1;i<10;++i)assert(rtl_ios_owner_release(&o,i)==0);
    assert(!rtl_ios_owner_drained(&o)); /* still held by native input */
    assert(rtl_ios_owner_release(&o,0)==0&&rtl_ios_owner_drained(&o));
    assert(rtl_ios_owner_prepare(&o,0,1,0x12500000,0x12600000,16384)<0);
    rtl_ios_owner_init(&o,7);
    assert(rtl_ios_owner_prepare(&o,0,1,0x12500000,0x12600000,16384)==0);
    assert(rtl_ios_owner_prepare(&o,1,1,0x12501000,0x12600040,16384)<0);
    assert(rtl_ios_owner_prepare(&o,1,1,0x12500000,0x12610000,16384)<0);
    assert(rtl_ios_owner_prepare(&o,2,1,0x12502000,0x12620000,16384)<0);
    assert(rtl_ios_owner_prepare(&o,2,0,0x12502000,0xffffffc0,16384)<0);
    assert(rtl_ios_owner_submit(&o,0,&b,16384)==0);
    RtlIosUhsCompletion stale=messages[0];
    assert(rtl_ios_owner_complete(&o,&messages[0],0,100)==0);
    assert(rtl_ios_owner_release(&o,0)==0);
    assert(rtl_ios_owner_prepare(&o,0,1,0x12500000,0x12600000,16384)==0);
    assert(rtl_ios_owner_submit(&o,0,&b,16384)==0);
    assert(messages[0].cookie!=stale.cookie);
    assert(rtl_ios_owner_complete(&o,&stale,0,100)<0);
    assert(rtl_ios_owner_complete(&o,&messages[0],0,100)==0);
    assert(rtl_ios_owner_release(&o,0)==0);
    o.next_cookie=0;
    assert(rtl_ios_owner_prepare(&o,0,1,0x12500000,0x12600000,16384)==0);
    assert(rtl_ios_owner_submit(&o,0,&b,16384)<0);
    puts("native ownership: ten slots, failed submit, out-of-order/duplicate callbacks, short TX, cancellation and retained RX passed");
}
