#include "frame_adapter.h"
#include "../driver/rtl8153_framing.h"
#include <string.h>
static uint32_t le32(const uint8_t *p)
{return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
int rtl_native_rx_views(const void *aggregate,size_t bytes,RtlNativeFrame *frames,
                        size_t max_frames,size_t *count)
{
    if(!aggregate||!frames||!count||bytes>32768||!max_frames)return -1;
    *count=0;
    const uint8_t *b=aggregate;
    size_t off=0;
    while(off<bytes){
        if(bytes-off<24){*count=0;return -1;}
        uint32_t status=le32(b+off);
        size_t wire=status&0x7fff;
        if(wire<18||wire>bytes-off-24){*count=0;return -1;}
        size_t padded=(wire+7)&~(size_t)7;
        size_t available=bytes-off-24;
        if(padded>available&&wire!=available){*count=0;return -1;}
        if(!(status&0x8000)&&wire<=1522){
            if(*count==max_frames){*count=0;return -1;}
            frames[*count]=(RtlNativeFrame){(uint32_t)(off+24),(uint32_t)(wire-4)};
            ++*count;
        }
        off+=24+(padded<=available?padded:wire);
    }
    return 0;
}
int rtl_native_tx_batch(void *out,size_t capacity,const void *const *frames,
                        const size_t *lengths,size_t count,size_t *written)
{
    if(!out||!frames||!lengths||!written||!count||count>128)return -1;
    *written=0;
    size_t total=0;
    for(size_t i=0;i<count;++i){
        if(!frames[i]||lengths[i]<14||lengths[i]>1518)return -1;
        total=(total+3)&~(size_t)3;
        size_t n=(lengths[i]<60?60:lengths[i])+8;
        if(total>capacity||n>capacity-total)return -1;
        total+=n;
    }
    /* Validate the complete batch before modifying caller storage. */
    uint8_t *p=out;
    size_t pos=0;
    for(size_t i=0;i<count;++i){
        size_t aligned=(pos+3)&~(size_t)3;
        memset(p+pos,0,aligned-pos);
        pos=aligned;
        int n=rtl8153_wrap(p+pos,capacity-pos,frames[i],lengths[i]);
        if(n<0)return -1;
        pos+=(size_t)n;
    }
    *written=pos;
    return 0;
}
