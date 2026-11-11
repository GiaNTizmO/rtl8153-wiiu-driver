#include "../driver/rtl8153_framing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put32(unsigned char *p, unsigned v)
{ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static size_t packet(unsigned char *p, size_t n, unsigned char value, int crc_bad)
{
    size_t wire = n + 4, bytes = 24 + ((wire + 7) & ~(size_t)7);
    memset(p, 0, bytes);
    put32(p, (unsigned)wire | (crc_bad ? 0x8000 : 0));
    memset(p+24, value, n);
    memset(p+24+n, 0xcc, 4);
    return bytes;
}
int main(void)
{
    unsigned char agg[8192], out[1602], tx[1600], frame[1518];
    size_t a = packet(agg, 60, 0x11, 0);
    size_t b = packet(agg+a, 1514, 0x22, 0);
    size_t pos=0;
    memset(out, 0xee, sizeof(out));
    assert(rtl8153_unwrap(agg, a+b, &pos, out+1, 1600)==60 && pos==a);
    assert(out[0]==0xee && out[1]==0x11 && out[60]==0x11 && out[61]==0xee);
    assert(rtl8153_unwrap(agg, a+b, &pos, out+1, 1600)==1514 && pos==a+b);
    assert(out[1]==0x22 && out[1514]==0x22 && out[1515]==0xee);
    assert(rtl8153_unwrap(agg, a+b, &pos, out, sizeof(out))==0);
    a=packet(agg, 60, 0x11, 1); b=packet(agg+a, 61, 0x33, 0); pos=0;
    assert(rtl8153_unwrap(agg, a+b, &pos, out, sizeof(out))==61 && out[0]==0x33);
    a=packet(agg, 1601, 0x55, 0); b=packet(agg+a, 60, 0x66, 0); pos=0;
    assert(rtl8153_unwrap(agg,a+b,&pos,out,1600)==60 && out[0]==0x66);
    /* Header truncation, underflow and oversized length cannot read past USB data. */
    for (size_t n=1;n<24;n++) { pos=0; assert(rtl8153_unwrap(agg,n,&pos,out,1600)==-1 && pos==n); }
    memset(agg,0,24); pos=0;
    assert(rtl8153_unwrap(agg,24,&pos,out,1600)==-1);
    put32(agg,0x7fff); pos=0;
    assert(rtl8153_unwrap(agg,24,&pos,out,1600)==-1);
    a=packet(agg,61,0x77,0); pos=0;
    assert(rtl8153_unwrap(agg,24+61+4,&pos,out,1600)==61);
    pos=a+1; assert(rtl8153_unwrap(agg,a,&pos,out,1600)==-1);
    memset(frame,0x42,sizeof(frame));
    assert(rtl8153_wrap(tx,sizeof(tx),frame,1518)==1526);
    assert(tx[0]==0xee && tx[1]==5 && tx[2]==0 && tx[3]==0xc0);
    assert(tx[4]==0 && tx[7]==0 && memcmp(tx+8,frame,1518)==0);
    memset(tx,0xff,sizeof(tx));
    assert(rtl8153_wrap(tx,sizeof(tx),frame,14)==68 && tx[0]==60);
    for (unsigned i=22;i<68;i++) assert(tx[i]==0);
    assert(rtl8153_wrap(tx,67,frame,14)==-1);
    assert(rtl8153_wrap(tx,sizeof(tx),frame,13)==-1);
    assert(rtl8153_wrap(tx,sizeof(tx),frame,1519)==-1);
    /* Deterministic malformed-aggregate stress under ASan/UBSan. */
    unsigned seed=0x8153;
    for(unsigned k=0;k<20000;k++) {
        seed=seed*1664525u+1013904223u; size_t n=seed%sizeof(agg);
        for(size_t i=0;i<n;i++) { seed=seed*1664525u+1013904223u; agg[i]=seed>>24; }
        pos=0; unsigned calls=0; int rc;
        do { rc=rtl8153_unwrap(agg,n,&pos,out,sizeof(out)); assert(pos<=n); assert(++calls<=n/24+1); } while(rc>0);
    }
    puts("framing: aggregate ordering, FCS, padding, CRC, truncation, TX and 20000 malformed inputs passed");
}
