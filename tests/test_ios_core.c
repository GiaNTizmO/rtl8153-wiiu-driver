#include "../native/rtl8153_ios_core.h"
#include "../driver/rtl8153_regs.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint8_t regs[2][65536];
static uint16_t page;
static unsigned calls,fail_at,waits;
static int stop;
static int transfer(void *user,int read,uint8_t request,uint16_t addr,
                    uint16_t index,void *data,uint16_t len)
{
    assert(user==regs&&request==5&&!(addr&3)&&!(len&3)&&!((uintptr_t)data&63));
    if(++calls==fail_at)return -77;
    unsigned bank=!!(index&0x100);uint8_t *p=data;
    if(read){
        memcpy(p,regs[bank]+addr,len);
        if(bank&&addr==0xb420&&page==0xa000){p[0]=3;p[1]=0;}
    }else{
        unsigned en=index&255;
        for(unsigned i=0;i<len;++i)if((i<4?en&15:en>>4)&(1u<<(i&3)))regs[bank][addr+i]=p[i];
        if(bank&&addr==(URE_PLA_OCP_GPHY_BASE&~3u))page=regs[1][URE_PLA_OCP_GPHY_BASE]|((uint16_t)regs[1][URE_PLA_OCP_GPHY_BASE+1]<<8);
        if(bank&&addr==(URE_PLA_CR&~3u))regs[1][URE_PLA_CR]&=~URE_CR_RST;
        if(bank&&addr==(URE_PLA_OOB_CTRL&~3u))regs[1][URE_PLA_OOB_CTRL]|=URE_LINK_LIST_READY;
    }
    return len;
}
static void pause_ms(void *user,unsigned ms){assert(user==regs);waits+=ms;}
static int is_cancelled(void *user){assert(user==regs);return stop;}
static void setup(void)
{
    memset(regs,0,sizeof(regs));page=0;calls=fail_at=waits=stop=0;
    regs[1][URE_PLA_TCR1]=0x10;regs[1][URE_PLA_TCR1+1]=0x60;
    regs[1][URE_PLA_BOOT_CTRL]=URE_AUTOLOAD_DONE;regs[1][0xe001]=1;
    regs[1][URE_PLA_OOB_CTRL]=URE_LINK_LIST_READY;
    uint8_t mac[8]={0,0xe0,0x4c,0x68,7,0x2b};memcpy(regs[1]+URE_PLA_BACKUP,mac,8);
}
int main(void)
{
    RtlIosDevice d;
    RtlIosTransport t={regs,transfer,pause_ms,is_cancelled};
    setup();assert(rtl_ios_init(&d,&t)==0&&d.chip_b&&d.mac[5]==0x2b);
    unsigned total=calls,mbps;int full;
    assert(rtl_ios_get_link(&d,&mbps,&full)==0&&mbps==0);
    regs[1][URE_PLA_PHYSTATUS]=URE_PHYSTATUS_LINK|URE_PHYSTATUS_FDX|URE_PHYSTATUS_1000MBPS;
    assert(rtl_ios_get_link(&d,&mbps,&full)==1&&mbps==1000&&full);
    assert(rtl_ios_enable(&d)==0&&(regs[1][URE_PLA_CR]&12)==12);
    assert(rtl_ios_disable(&d)==0&&(regs[1][URE_PLA_CR]&12)==0);
    for(unsigned f=1;f<=total;++f){setup();fail_at=f;assert(rtl_ios_init(&d,&t)==-1&&d.io_error&&calls==f);}
    setup();stop=1;assert(rtl_ios_init(&d,&t)==-1&&calls==0);
    assert(rtl_ios_init(NULL,&t)==-1);
    puts("IOS core: aligned callback transport, RTL8153B init/link/enable/disable and every initialization transfer failure passed");
    return 0;
}
