/* Exercise the production register/initialization code against a deterministic
 * USB device model. This validates transactions, not physical chip behavior. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../driver/rtl8153_regs.h"
#define RX_BUFFER_SIZE (32 * 1024)
typedef struct Rtl8153 {
    int io_error, chip_b;
    uint16_t chip, tx_maxpacket;
    int32_t last_control;
    uint8_t mac[6];
} Rtl8153;
static uint8_t regs[2][65536];
static unsigned calls, fail_at, writes, delays;
static uint16_t phy_page;
static uint32_t le32(const uint8_t *p)
{ return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void sleep_ms(int ms) { delays += ms; }
static void log_discard(const char *fmt, ...) { (void)fmt; }
#define WHBLogPrintf log_discard
static int control(Rtl8153 *s, int read, uint8_t cmd, uint16_t addr,
                   uint16_t index, uint16_t size, void *data)
{
    calls++;
    assert(cmd==5 && !(addr&3) && !(size&3) && size && size<=64);
    assert((unsigned)addr+size<=65536);
    if (calls==fail_at) { s->last_control=-123; return -1; }
    unsigned bank=!!(index&0x100); uint8_t *p=data;
    if (read) {
        memcpy(p,regs[bank]+addr,size);
        /* OCP window for PHY power status (a420) and ALDPS readiness. */
        if(bank && addr==0xb420 && phy_page==0xa000) { p[0]=3; p[1]=0; }
    } else {
        writes++;
        unsigned en=index&0xff;
        for(unsigned i=0;i<size;i++) {
            unsigned mask=i<4 ? en&15 : en>>4;
            if(mask&(1u<<(i&3))) regs[bank][addr+i]=p[i];
        }
        if(bank && addr==(URE_PLA_OCP_GPHY_BASE&~3u))
            phy_page=regs[1][URE_PLA_OCP_GPHY_BASE]|((uint16_t)regs[1][URE_PLA_OCP_GPHY_BASE+1]<<8);
        /* Reset self-clears; OOB link-list ready is hardware maintained. */
        if(bank && addr==(URE_PLA_CR&~3u)) regs[1][URE_PLA_CR]&=~URE_CR_RST;
        if(bank && addr==(URE_PLA_OOB_CTRL&~3u)) regs[1][URE_PLA_OOB_CTRL]|=URE_LINK_LIST_READY;
    }
    s->last_control=size; return 0;
}
#include "../driver/rtl8153_chip.inc"

static void reset(uint16_t version)
{
    memset(regs,0,sizeof(regs)); calls=fail_at=writes=delays=0; phy_page=0;
    regs[1][URE_PLA_TCR1]=version; regs[1][URE_PLA_TCR1+1]=version>>8;
    regs[1][URE_PLA_BOOT_CTRL]=URE_AUTOLOAD_DONE;
    regs[1][0xe001]=1;
    regs[1][URE_PLA_OOB_CTRL]=URE_LINK_LIST_READY;
    const uint8_t mac[8]={0,0xe0,0x4c,0x68,7,0x2b,0,0};
    memcpy(regs[1]+URE_PLA_BACKUP,mac,8);
}
int main(void)
{
    Rtl8153 s={0}; reset(0x5c20);
    ure_write_1(&s,0x1237,URE_MCU_TYPE_PLA,0xab);
    assert(regs[1][0x1237]==0xab && regs[1][0x1236]==0);
    ure_write_2(&s,0x1236,URE_MCU_TYPE_PLA,0xcdef);
    assert(regs[1][0x1236]==0xef && regs[1][0x1237]==0xcd);
    assert(ure_read_2(&s,0x1236,URE_MCU_TYPE_PLA)==0xcdef);
    ure_write_4(&s,0x1234,URE_MCU_TYPE_USB,0x12345678);
    assert(ure_read_4(&s,0x1234,URE_MCU_TYPE_USB)==0x12345678);
    assert(regs[0][0x1234]==0x78 && regs[1][0x1234]==0);
    uint16_t variants[]={0x5c00,0x5c10,0x5c20,0x5c30,0x6000,0x6010};
    for(unsigned v=0;v<sizeof(variants)/sizeof(variants[0]);v++) {
        reset(variants[v]); s=(Rtl8153){.tx_maxpacket=512};
        assert(rtl_chip_init(&s)==0 && !s.io_error);
        assert(memcmp(s.mac,regs[1]+URE_PLA_BACKUP,6)==0);
        assert(memcmp(s.mac,regs[1]+URE_PLA_IDR,6)==0);
        assert(s.chip_b==(v>=4));
        unsigned total=calls;
        /* Every possible control-transfer failure must stop initialization. */
        for(unsigned f=1;f<=total;f++) {
            reset(variants[v]); fail_at=f; s=(Rtl8153){.tx_maxpacket=512};
            assert(rtl_chip_init(&s)==-1 && s.io_error && calls==f);
        }
    }
    reset(0x7420); s=(Rtl8153){0};
    assert(rtl_chip_init(&s)==-1 && writes==0);
    reset(0x5c20); memset(regs[1]+URE_PLA_BACKUP,0xff,8); s=(Rtl8153){0};
    assert(rtl_chip_init(&s)==-1);
    reset(0x5c20); regs[1][URE_PLA_BOOT_CTRL]=0; s=(Rtl8153){0};
    assert(rtl_chip_init(&s)==-1 && delays>=10000);
    uint16_t unused;
    assert(rtl8153_read_phy(&s,32,&unused)==-1);
    (void)rtl8153_read_mac; (void)rtl8153_last_control;
    puts("chip: six silicon revisions, endian/byte enables, invalid MAC/revision, timeout and every control failure passed");
}
