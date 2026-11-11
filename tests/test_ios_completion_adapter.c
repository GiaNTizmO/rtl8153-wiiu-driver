#include "../native/ios_completion_adapter.h"
#include <assert.h>
#include <stdio.h>
typedef struct { unsigned calls,lease; unsigned kind; IosAddress batch,buffer; int result; } Seen;
static int dispatch(void *u,unsigned kind,IosAddress batch,IosAddress buffer,int result,unsigned lease)
{
    Seen *s=u; ++s->calls; s->lease=lease; s->kind=kind; s->batch=batch; s->buffer=buffer; s->result=result; return 0;
}
static int reject(void *u,unsigned k,IosAddress b,IosAddress d,int r,unsigned l)
{ (void)u;(void)k;(void)b;(void)d;(void)r;(void)l; return -1; }
int main(void)
{
    RtlIosCompletionAdapter a; Seen s={0}; uint32_t q0,q1; unsigned lease;
    assert(rtl_ios_completion_adapter_init(&a,dispatch,&s)==0);
    assert(rtl_ios_completion_adapter_bind(&a,0x1288d020,0x124c6080)==0);
    assert(rtl_ios_completion_adapter_bind(&a,0x1288d020,0x124c6080)<0);
    assert(rtl_ios_completion_adapter_arm(&a,1,0x20000000,0x21000000,&q0)==0);
    assert(rtl_ios_completion_adapter_arm(&a,2,0x20001000,0x21010000,&q1)==1);
    assert(q0 && q1 && q0!=q1);
    assert(rtl_ios_completion_adapter_callback(&a,0x1288d021,0x124c6080,1,0x20000000,0x21000000,0,&lease)<0);
    assert(rtl_ios_completion_adapter_callback(&a,0x1288d020,0x124c6080,1,0x20000000,0x21000000,48,&lease)==0);
    assert(s.calls==1 && s.lease==lease && s.result==48);
    assert(rtl_ios_completion_adapter_callback(&a,0x1288d020,0x124c6080,1,0x20000000,0x21000000,48,&lease)<0);
    assert(rtl_ios_completion_adapter_release(&a,lease)==0);
    assert(rtl_ios_completion_adapter_reset(&a)<0); /* TX still armed. */
    assert(rtl_ios_completion_adapter_callback(&a,0x1288d020,0x124c6080,2,0x20001000,0x21010000,-64,&lease)==0);
    assert(rtl_ios_completion_adapter_release(&a,lease)==0);
    assert(rtl_ios_completion_adapter_reset(&a)==0);
    assert(rtl_ios_completion_adapter_arm(&a,1,0x20000000,0x21000000,&q0)==0);
    assert(rtl_ios_completion_adapter_cancel(&a,0)==0);
    assert(rtl_ios_completion_adapter_cancel(&a,0)<0);
    assert(rtl_ios_completion_adapter_callback(&a,0x1288d020,0x124c6080,1,0x20000000,0x21000000,48,&lease)<0);
    assert(s.calls==2 && rtl_ios_completion_adapter_reset(&a)==0);
    Seen rejected={0}; RtlIosCompletionAdapter b;
    assert(rtl_ios_completion_adapter_init(&b,reject,&rejected)==0);
    assert(rtl_ios_completion_adapter_bind(&b,0x1000,0x2000)==0);
    assert(rtl_ios_completion_adapter_arm(&b,1,0x3000,0x4000,&q0)==0);
    assert(rtl_ios_completion_adapter_callback(&b,0x1000,0x2000,1,0x3000,0x4000,0,&lease)<0);
    assert(rtl_ios_completion_adapter_reset(&b)==0);
    puts("IOS completion adapter: exact context/controller ABI, callback dispatch, duplicate rejection, cancel/release and reset gates passed");
}
