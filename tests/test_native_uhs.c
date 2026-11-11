#include "../native/uhs_transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int stop,result,calls,bulk_calls,admin_calls;static unsigned slept;
static uint8_t expected_type;
static int sync_control(void *uhs,uint32_t handle,uint8_t req,uint8_t type,
 uint16_t value,uint16_t index,void *data,uint16_t bytes,uint32_t timeout)
{
    assert(uhs==(void *)0x1234&&handle==0x3456&&req==5&&type==expected_type);
    assert(value==0xe600&&index==0x133&&bytes==8&&timeout==1000000);
    assert(((uintptr_t)data&63)==0);++calls;return result;
}
static int bulk(void *uhs,uint32_t handle,uint8_t ep,unsigned direction,
 void *data,uint32_t bytes,const RtlIosUhsCompletion *m,int queue)
{
    assert(uhs==(void *)0x1234&&handle==0x3456&&queue==71);
    assert(m->interface_id==2&&m->batch==0x124e3000&&m->buffer==0x124f0000&&!m->cookie);
    assert(data==(void *)0x124f0000);
    if(m->kind==1)assert(ep==1&&direction==2&&bytes==32768);
    else assert(m->kind==2&&ep==2&&direction==1&&bytes==512);
    ++bulk_calls;return result;
}
static void delay(void *u,unsigned ms){assert(u==(void *)0x5678);slept+=ms;}
static int admin(void *u,uint32_t handle,uint32_t op,uint32_t mask,uint32_t a4,uint32_t a5)
{
    assert(u==(void *)0x1234 && handle==0x3456);
    assert(op==2 && mask==((1u<<17)|(1u<<2)) && !a4 && !a5);
    ++admin_calls;return result;
}
static int cancelled(void *u){assert(u==(void *)0x5678);return stop;}
int main(void)
{
    RtlIosUhsBinding b={.uhs=(void *)0x1234,.interface_handle=0x3456,
      .interface_id=2,.completion_queue=71,.bulk_in=0x81,.bulk_out=2,
      .control_sync=sync_control,.bulk_async=bulk,.user=(void *)0x5678,
      .sleep_ms=delay,.cancelled=cancelled};
    RtlIosTransport t;uint8_t data[128] __attribute__((aligned(64)))={0};
    assert(rtl_ios_uhs_transport(&b,&t)==0);
    expected_type=0xc0;result=0;
    assert(t.control(t.user,1,5,0xe600,0x133,data,8)==8);
    expected_type=0x40;result=8;
    assert(t.control(t.user,0,5,0xe600,0x133,data,8)==8);
    result=7;assert(t.control(t.user,0,5,0xe600,0x133,data,8)<0);
    result=-17;assert(t.control(t.user,0,5,0xe600,0x133,data,8)==-17);
    int before=calls;stop=1;assert(t.control(t.user,0,5,0xe600,0x133,data,8)<0);stop=0;
    assert(t.control(t.user,0,5,0xe600,0x133,data+1,8)<0);
    assert(t.control(t.user,0,5,0xe600,0x133,data,65)<0&&calls==before);
    t.sleep_ms(t.user,13);assert(slept==13);
    result=0;
    assert(rtl_ios_uhs_submit(&b,1,0x124e3000,0x124f0000,32768)==0);
    assert(rtl_ios_uhs_submit(&b,0,0x124e3000,0x124f0000,512)==0);
    result=-29;assert(rtl_ios_uhs_submit(&b,0,0x124e3000,0x124f0000,512)==-29);
    before=bulk_calls;
    assert(rtl_ios_uhs_submit(&b,0,0x124e3000,0x124f0000,16385)<0);
    assert(rtl_ios_uhs_submit(&b,1,0x124e3000,0xffffffc0,32768)<0);
    assert(rtl_ios_uhs_submit(&b,1,0x124e3001,0x124f0000,32768)<0);
    assert(rtl_ios_uhs_submit(&b,1,0x124e3000,0x124f0001,32768)<0);
    b.bulk_in=1;assert(rtl_ios_uhs_submit(&b,1,0x124e3000,0x124f0000,32768)<0);
    b.bulk_in=0x80;assert(rtl_ios_uhs_submit(&b,1,0x124e3000,0x124f0000,32768)<0);
    b.bulk_in=0x91;assert(rtl_ios_uhs_submit(&b,1,0x124e3000,0x124f0000,32768)<0);
    b.bulk_in=0x81;stop=1;assert(rtl_ios_uhs_submit(&b,1,0x124e3000,0x124f0000,32768)<0);
    assert(bulk_calls==before);
    /* Cancellation must bypass the stop predicate, preserve errors, validate
     * endpoints before shifting, and never synthesize completion. */
    b.endpoint_admin=admin;result=-64;
    assert(rtl_ios_uhs_cancel(&b)==-64 && admin_calls==1 && bulk_calls==before);
    result=0;assert(rtl_ios_uhs_cancel(&b)==0 && admin_calls==2);
    b.bulk_out=0x82;assert(rtl_ios_uhs_cancel(&b)<0 && admin_calls==2);
    b.bulk_out=2;b.bulk_in=0x80;
    assert(rtl_ios_uhs_cancel(&b)<0 && admin_calls==2);
    b.bulk_in=0x91;assert(rtl_ios_uhs_cancel(&b)<0 && admin_calls==2);
    b.bulk_in=0x81;b.endpoint_admin=NULL;assert(rtl_ios_uhs_cancel(&b)<0);
    puts("native UHS: control ABI, DMA alignment, short/error status, cancellation and async metadata passed (mock only)");
}
