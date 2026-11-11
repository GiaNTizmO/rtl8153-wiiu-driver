/* ARM chip core: same register sequence as PPC, independent of lwIP, WUT,
 * nsysnet hooks and per-title PPC UHS clients. */
#include "rtl8153_ios_core.h"
#include <string.h>
typedef RtlIosDevice Rtl8153;
#define RX_BUFFER_SIZE 32768
#define WHBLogPrintf(...) ((void)0)
/* Single owner, matching IOS-NET's USB Ethernet worker. */
static RtlIosDevice *initializing;
static void sleep_ms(int ms)
{
    if(!initializing)return;
    RtlIosTransport *t=&initializing->transport;
    if(t->cancelled&&t->cancelled(t->user)){initializing->io_error=1;return;}
    if(t->sleep_ms)t->sleep_ms(t->user,(unsigned)ms);
}
static uint32_t le32(const uint8_t *p)
{return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int control(RtlIosDevice *d,int read,uint8_t req,uint16_t value,
                   uint16_t index,uint16_t length,void *data)
{
    RtlIosTransport *t=&d->transport;
    if(!data||!length||length>sizeof(d->control_dma)||!t->control)return -1;
    if(t->cancelled&&t->cancelled(t->user))return -1;
    memset(d->control_dma,0,sizeof(d->control_dma));
    if(!read)memcpy(d->control_dma,data,length);
    int rc=t->control(t->user,read,req,value,index,d->control_dma,length);
    d->last_control=rc;
    if(rc!=(int)length)return -1;
    if(read)memcpy(data,d->control_dma,length);
    return 0;
}
#define rtl8153_read_mac rtl_ios_debug_read_mac
#define rtl8153_read_phy rtl_ios_debug_read_phy
#define rtl8153_last_control rtl_ios_debug_last_control
#include "../driver/rtl8153_chip.inc"

int rtl_ios_init(RtlIosDevice *d,const RtlIosTransport *t)
{
    if(!d||!t||!t->control||!t->sleep_ms||initializing)return -1;
    memset(d,0,sizeof(*d));d->transport=*t;d->tx_maxpacket=512;
    initializing=d;
    int rc=rtl_chip_init(d);
    initializing=NULL;
    return rc;
}
int rtl_ios_get_link(RtlIosDevice *d,unsigned *mbps,int *full_duplex)
{
    if(mbps)*mbps=0;
    if(full_duplex)*full_duplex=0;
    if(!d||d->io_error)return -1;
    uint16_t v=ure_read_2(d,URE_PLA_PHYSTATUS,URE_MCU_TYPE_PLA);
    if(d->io_error)return -1;
    if(!(v&URE_PHYSTATUS_LINK))return 0;
    if(mbps)*mbps=(v&URE_PHYSTATUS_1000MBPS)?1000:(v&URE_PHYSTATUS_100MBPS)?100:10;
    if(full_duplex)*full_duplex=!!(v&URE_PHYSTATUS_FDX);
    return 1;
}
int rtl_ios_enable(RtlIosDevice *d)
{
    if(!d||d->io_error)return -1;
    uint8_t multicast[8];memset(multicast,0xff,sizeof(multicast));
    URE_CLRBIT_2(d,URE_PLA_FMC,URE_MCU_TYPE_PLA,URE_FMC_FCR_MCU_EN);
    URE_SETBIT_2(d,URE_PLA_FMC,URE_MCU_TYPE_PLA,URE_FMC_FCR_MCU_EN);
    ure_write_mem(d,URE_PLA_MAR,URE_MCU_TYPE_PLA|URE_BYTE_EN_DWORD,multicast,8);
    uint32_t v=ure_read_4(d,URE_PLA_RCR,URE_MCU_TYPE_PLA);
    ure_write_4(d,URE_PLA_RCR,URE_MCU_TYPE_PLA,
        (v&~URE_RCR_ACPT_ALL)|URE_RCR_APM|URE_RCR_AB|URE_RCR_AM);
    URE_SETBIT_1(d,URE_PLA_CR,URE_MCU_TYPE_PLA,URE_CR_RE|URE_CR_TE);
    if(d->chip_b)ure_write_1(d,URE_USB_UPT_RXDMA_OWN,URE_MCU_TYPE_USB,URE_OWN_UPDATE|URE_OWN_CLEAR);
    URE_CLRBIT_2(d,URE_PLA_MISC_1,URE_MCU_TYPE_PLA,URE_RXDY_GATED_EN);
    return d->io_error?-1:0;
}
int rtl_ios_disable(RtlIosDevice *d)
{
    if(!d||d->io_error)return -1;
    URE_SETBIT_2(d,URE_PLA_MISC_1,URE_MCU_TYPE_PLA,URE_RXDY_GATED_EN);
    URE_CLRBIT_1(d,URE_PLA_CR,URE_MCU_TYPE_PLA,URE_CR_RE|URE_CR_TE);
    return d->io_error?-1:0;
}
