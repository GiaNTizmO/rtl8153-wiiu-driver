#include "../native/ios_net_abi.h"
#include "../native/frame_adapter.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static size_t rx(uint8_t *p,size_t size,int bad)
{
    size_t wire=size+4,n=24+((wire+7)&~(size_t)7);
    memset(p,0,n);p[0]=wire;p[1]=(wire>>8)|(bad?0x80:0);memset(p+24,0x81,size);
    return n;
}
int main(void)
{
    uint8_t data[32768],a[61],b[1518],out[4096];
    RtlNativeFrame views[128];size_t count,bytes;
    size_t n=rx(data,61,0);n+=rx(data+n,60,1);n+=rx(data+n,1518,0);
    assert(rtl_native_rx_views(data,n,views,128,&count)==0&&count==2);
    assert(views[0].offset==24&&views[0].length==61);
    assert(views[1].length==1518&&data[views[1].offset]==0x81);
    assert(rtl_native_rx_views(data,n,views,1,&count)==-1&&count==0);
    assert(rtl_native_rx_views(data,n-10,views,128,&count)==-1&&count==0);
    memset(a,0x11,sizeof(a));memset(b,0x22,sizeof(b));
    const void *frames[]={a,b};size_t lengths[]={61,1518};
    assert(rtl_native_tx_batch(out,sizeof(out),frames,lengths,2,&bytes)==0&&bytes==1598);
    assert(out[0]==61&&out[3]==0xc0&&out[69]==0&&out[70]==0&&out[71]==0);
    assert(out[72]==0xee&&out[73]==5&&out[75]==0xc0&&out[80]==0x22);
    memset(out,0xaa,sizeof(out));
    assert(rtl_native_tx_batch(out,1597,frames,lengths,2,&bytes)==-1&&bytes==0&&out[0]==0xaa);
    lengths[1]=1519;assert(rtl_native_tx_batch(out,sizeof(out),frames,lengths,2,&bytes)==-1);
    puts("native frame adapter: IOS ABI sizes, zero-copy RX, CRC/FCS, bounds and aligned multi-frame TX passed");
}
