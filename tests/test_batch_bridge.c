#include "../native/batch_bridge.h"
#include "../native/ios_net_abi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put(uint8_t *p,uint32_t v)
{p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static uint32_t get(const uint8_t *p)
{return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static const uint32_t base=0x124e4000,dma_base=0x124f0000;
static uint8_t allocation[0x448+16384],out[32768],dma[32768],header[0x448];
static void tx(unsigned hdr,unsigned count,unsigned length)
{
    memset(allocation,0xa5,sizeof(allocation));
    put(allocation+0x38,count);put(allocation+0x3c,0);
    size_t n=0;
    for(unsigned i=0;i<count;++i){
        put(allocation+0x44+i*8,base+0x448+n);
        put(allocation+0x48+i*8,length);
        memset(allocation+0x448+n,0xcc,hdr);
        memset(allocation+0x448+n+hdr,(int)(0x10+i),length);
        n+=hdr+((length+1)&~1u);
    }
    put(allocation+0x40,(uint32_t)n);
}
static void tx_bad(void)
{
    size_t n=17;memset(out,0xac,sizeof(out));
    assert(rtl_ios_repack_tx(allocation,sizeof(allocation),base,8,out,sizeof(out),&n)<0);
    assert(n==0);
    for(size_t i=0;i<sizeof(out);++i)assert(out[i]==0xac);
}
static size_t rx(size_t offset,unsigned length,int bad)
{
    unsigned wire=length+4;size_t n=24+((wire+7)&~7u);
    memset(dma+offset,0,n);dma[offset]=wire;dma[offset+1]=(wire>>8)|(bad?0x80:0);
    memset(dma+offset+24,0x99,length);return n;
}
int main(void)
{
    size_t n;
    /* Odd payloads are packed by Nintendo on two-byte boundaries. Output
     * moves the second header to a four-byte boundary and pads runt frames. */
    tx(8,2,61);
    assert(rtl_ios_repack_tx(allocation,sizeof(allocation),base,8,out,sizeof(out),&n)==0);
    assert(n==141&&out[0]==61&&out[3]==0xc0&&out[8]==0x10);
    assert(out[69]==0&&out[70]==0&&out[71]==0&&out[72]==61&&out[80]==0x11);
    tx(4,2,15);
    assert(rtl_ios_repack_tx(allocation,sizeof(allocation),base,4,out,sizeof(out),&n)==0);
    assert(n==136&&out[0]==60&&out[8]==0x10&&out[23]==0&&out[76]==0x11);
    tx(8,2,61);put(allocation+0x44,base+0x449);tx_bad();
    tx(8,2,61);put(allocation+0x4c,0xfffffff0);tx_bad();
    tx(8,2,61);put(allocation+0x48,0xffffffff);tx_bad();
    tx(8,2,61);put(allocation+0x38,129);tx_bad();
    tx(8,2,61);put(allocation+0x40,139);tx_bad();
    tx(8,2,61);put(allocation+0x40,141);tx_bad();
    tx(8,2,61);
    assert(rtl_ios_repack_tx(allocation,sizeof(allocation),base,8,out,140,&n)<0&&n==0);
    assert(rtl_ios_repack_tx(allocation,sizeof(allocation),UINT32_MAX-4,8,out,sizeof(out),&n)<0);
    assert(rtl_ios_repack_tx(allocation,sizeof(allocation),base,8,allocation+0x448,16384,&n)<0);
    assert(rtl_ios_repack_tx(allocation,0x447,base,8,out,sizeof(out),&n)<0);
    assert(rtl_ios_repack_tx(allocation,sizeof(allocation),base,0,out,sizeof(out),&n)<0);
    tx(8,128,14);
    assert(rtl_ios_repack_tx(allocation,sizeof(allocation),base,8,out,sizeof(out),&n)==0&&n==128*68);

    memset(header,0xa5,sizeof(header));put(header+0x444,dma_base);
    size_t received=rx(0,61,0);size_t second=received;
    received+=rx(received,60,1);size_t third=received;received+=rx(received,1518,0);
    assert(rtl_ios_publish_rx(header,sizeof(header),dma_base,dma,sizeof(dma),received)==0);
    assert(get(header+0x38)==2&&get(header+0x3c)==0);
    assert(get(header+0x40)==dma_base+24&&get(header+0x44)==61);
    assert(get(header+0x48)==dma_base+third+24&&get(header+0x4c)==1518);
    assert(get(header+0x440)==0xa5a5a5a5&&get(header+0x444)==dma_base);
    for(unsigned i=0;i<0x38;++i)assert(header[i]==0xa5);
    uint8_t saved[sizeof(header)];memcpy(saved,header,sizeof(header));
    dma[second]=0xff;dma[second+1]=0x7f;
    assert(rtl_ios_publish_rx(header,sizeof(header),dma_base,dma,sizeof(dma),received)<0);
    assert(memcmp(saved,header,sizeof(header))==0);
    assert(rtl_ios_publish_rx(header,sizeof(header),dma_base+1,dma,sizeof(dma),0)<0);
    assert(rtl_ios_publish_rx(header,sizeof(header),dma_base,dma,sizeof(dma),sizeof(dma)+1)<0);
    assert(rtl_ios_publish_rx(header,sizeof(header)-1,dma_base,dma,sizeof(dma),0)<0);
    assert(rtl_ios_publish_rx(header,sizeof(header),UINT32_MAX-4,dma,sizeof(dma),0)<0);
    assert(rtl_ios_publish_rx(header,sizeof(header),dma_base,header,sizeof(header),0)<0);
    assert(rtl_ios_publish_rx(header,sizeof(header),dma_base,dma,sizeof(dma),0)==0);
    assert(get(header+0x38)==0&&get(header+0x3c)==0);
    puts("native batch bridge: distinct BE TX/RX layouts, inline bounds, repacking, pointer overflow, no partial commit passed");
}
