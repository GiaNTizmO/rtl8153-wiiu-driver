#include "../native/completion_bridge.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    RtlIosCompletionBridge b; uint32_t a,c; unsigned lease;
    assert(rtl_ios_bridge_init(&b)==0);
    assert(rtl_ios_bridge_arm(&b,1,0x20000000,0x21000000,&a)==0);
    assert(rtl_ios_bridge_arm(&b,2,0x20001000,0x21010000,&c)==1);
    assert(a && c && a!=c);
    assert(rtl_ios_bridge_arm(&b,1,0x20000000,0x21000000,&a)<0);
    assert(rtl_ios_bridge_complete(&b,1,0x20000000,0x21000040,0,&lease)<0);
    assert(!rtl_ios_bridge_drained(&b));
    assert(rtl_ios_bridge_complete(&b,1,0x20000000,0x21000000,48,&lease)==0);
    assert(lease==0 && b.leases[lease].result==48);
    assert(rtl_ios_bridge_complete(&b,1,0x20000000,0x21000000,48,&lease)<0);
    assert(rtl_ios_bridge_release(&b,lease)==0 && rtl_ios_bridge_release(&b,lease)<0);
    assert(rtl_ios_bridge_arm(&b,1,0x20000000,0x21000000,&a)<0);
    assert(rtl_ios_bridge_reset(&b)<0);
    assert(rtl_ios_bridge_complete(&b,2,0x20001000,0x21010000,-64,&lease)==0);
    assert(rtl_ios_bridge_release(&b,lease)==0 && rtl_ios_bridge_drained(&b));
    assert(rtl_ios_bridge_reset(&b)==0);
    assert(rtl_ios_bridge_arm(&b,1,0x20000000,0x21000000,&a)==0 && a!=c);
    assert(rtl_ios_bridge_arm(&b,3,0x20002000,0x21020000,&c)<0);
    assert(rtl_ios_bridge_arm(&b,1,0x20000001,0x21030000,&c)<0);
    assert(rtl_ios_bridge_complete(&b,1,0x20000000,0x21000000,0,&lease)==0);
    assert(!rtl_ios_bridge_drained(&b) && rtl_ios_bridge_reset(&b)<0);
    assert(rtl_ios_bridge_release(&b,lease)==0 && rtl_ios_bridge_reset(&b)==0);
    puts("completion bridge: exact callback matching, duplicate/stale rejection, one-shot leases and drain-gated reset passed");
}
