#define RTL8153_MOCK_UHS 1
#include "../driver/rtl8153.c"
#include <assert.h>

static uint8_t regs[2][65536];
static uint16_t page;
static unsigned opened,closed,acquired,released,cancelled,submitted,fail_submission;
static int ep_fail,patch_fail,no_device,control_fail,bulk_short,patch_calls;
static int client_fail,acquire_fail;
static int query_calls;
static unsigned selected_controller;
static int stop_requested;
static int should_stop(void) { return stop_requested; }
static struct { void *user; const void *buffer; size_t size; AxUhsBulkAsyncCallback cb; } pending[3];
void DCFlushRange(void *p,size_t n) { assert(p && n); }
void DCInvalidateRange(void *p,size_t n) { assert(p && n); }
void OSSleepTicks(uint64_t n) { (void)n; }
OSTime OSGetTime(void) { return 0; }
void OSInitEvent(OSEvent *e,int s,int mode) { (void)mode; e->signalled=s; }
void OSSignalEvent(OSEvent *e) { e->signalled=1; }
int OSWaitEventWithTimeout(OSEvent *e,uint64_t t) { (void)t; int s=e->signalled; e->signalled=0; return s; }
void rtl_log(const char *fmt,...) { (void)fmt; }
int iosu_patch_apply(char *why,unsigned n) { patch_calls++; snprintf(why,n,"mock"); return patch_fail?-1:0; }
int32_t UhsClientOpen(UhsHandle *h,UhsConfig *c)
{ (void)h; selected_controller=c->controller_num; opened++; return client_fail?-1:0; }
int32_t UhsClientClose(UhsHandle *h) { (void)h; closed++; return 0; }
int32_t UhsQueryInterfaces(UhsHandle *h,UhsInterfaceFilter *f,UhsInterfaceProfile *p,int max)
{
    (void)h; assert(f->vid==0xbda && f->pid==0x8153 && max>=2); query_calls++;
    if(no_device) return 0;
    memset(p,0,2*sizeof(*p));
    /* First profile has no bulk endpoints; second uses sparse arrays. */
    p[1].if_handle=0x8153;
    p[1].in_endpoints[3]=(UhsEndpointDescriptor){7,0x82,2,512};
    p[1].in_endpoints[7]=(UhsEndpointDescriptor){7,0x83,3,16};
    p[1].out_endpoints[5]=(UhsEndpointDescriptor){7,1,2,2}; /* raw USB LE: 512 */
    return 2;
}
int32_t UhsAcquireInterface(UhsHandle *h,uint32_t id,void *a,void *b)
{ (void)h;(void)a;(void)b;assert(id==0x8153);acquired++;return acquire_fail?-1:0; }
int32_t UhsReleaseInterface(UhsHandle *h,uint32_t id,bool no_reacquire)
{ (void)h;(void)no_reacquire;assert(id==0x8153);released++;return 0; }
int32_t UhsSubmitControlRequest(UhsHandle *h,uint32_t id,void *data,uint8_t cmd,uint8_t type,
    uint16_t addr,uint16_t index,uint16_t len,int32_t timeout)
{
    (void)h; (void)timeout; assert(id==0x8153 && cmd==5 && !(addr&3) && !(len&3));
    if(control_fail) return -123;
    unsigned bank=!!(index&0x100); uint8_t *p=data;
    if(type==0xc0) {
        memcpy(p,regs[bank]+addr,len);
        if(bank && addr==0xb420 && page==0xa000) {p[0]=3;p[1]=0;}
    } else {
        assert(type==0x40); unsigned en=index&0xff;
        for(unsigned i=0;i<len;i++) if((i<4?en&15:en>>4)&(1u<<(i&3))) regs[bank][addr+i]=p[i];
        if(bank && addr==(URE_PLA_OCP_GPHY_BASE&~3u)) page=regs[1][URE_PLA_OCP_GPHY_BASE]|((uint16_t)regs[1][URE_PLA_OCP_GPHY_BASE+1]<<8);
        if(bank && addr==(URE_PLA_CR&~3u)) regs[1][URE_PLA_CR]&=~URE_CR_RST;
        if(bank && addr==(URE_PLA_OOB_CTRL&~3u)) regs[1][URE_PLA_OOB_CTRL]|=URE_LINK_LIST_READY;
    }
    return len;
}
int32_t UhsAdministerEndpoint(UhsHandle *h,uint32_t id,int op,uint32_t mask,uint32_t count,uint32_t size)
{
    (void)h;assert(id==0x8153);
    if(op==UHS_ADMIN_EP_ENABLE) {assert(mask==(1u<<18)||mask==2);assert(count && size);return ep_fail?-99:0;}
    if(op==UHS_ADMIN_EP_CANCEL) cancelled++;
    return 0;
}
int32_t UhsSubmitBulkRequest(UhsHandle *h,uint32_t id,uint8_t ep,int dir,void *buf,int32_t len,int32_t timeout)
{
    (void)h;(void)timeout;assert(id==0x8153 && ep==1 && dir==1 && len>=68);
    assert(((uint8_t*)buf)[3]==0xc0);return bulk_short?len-1:len;
}
int32_t UhsSubmitBulkRequestAsync(UhsHandle *h,uint32_t id,uint8_t ep,int dir,const void *buf,size_t len,
    void *user,AxUhsBulkAsyncCallback cb)
{
    (void)h;assert(id==0x8153 && ep==2 && dir==2 && len==32768);
    submitted++; if(submitted==fail_submission) return -77;
    unsigned slot=(unsigned)((struct AxRxAsyncSlot*)user-g_rx_slots);assert(slot<3);
    pending[slot].user=user;pending[slot].buffer=buf;pending[slot].size=len;pending[slot].cb=cb;return 0;
}
static void setup(void)
{
    memset(regs,0,sizeof(regs)); memset(pending,0,sizeof(pending)); page=0;
    opened=closed=acquired=released=cancelled=submitted=fail_submission=0;
    ep_fail=patch_fail=no_device=control_fail=bulk_short=patch_calls=0;
    client_fail=acquire_fail=query_calls=0;
    stop_requested=0; rtl8153_set_cancel_check(NULL);
    regs[1][URE_PLA_TCR1]=0x20;regs[1][URE_PLA_TCR1+1]=0x5c;
    regs[1][URE_PLA_BOOT_CTRL]=URE_AUTOLOAD_DONE;regs[1][0xe001]=1;
    regs[1][URE_PLA_OOB_CTRL]=URE_LINK_LIST_READY;
    const uint8_t mac[8]={0,0xe0,0x4c,0x68,7,0x2b};memcpy(regs[1]+URE_PLA_BACKUP,mac,8);
}
static void complete(unsigned slot,unsigned char value,int ret,size_t actual)
{
    assert(pending[slot].cb);uint8_t *p=(uint8_t*)pending[slot].buffer;
    memset(p,0,88);p[0]=64;memset(p+24,value,60); /* descriptor + frame + FCS */
    pending[slot].cb(pending[slot].user,ret,0x8153,2,2,p,actual);
}
int main(void)
{
    char why[200];uint8_t frame[1600];int speed;
    setup();Rtl8153 *s=rtl8153_open(why,sizeof(why));assert(s && patch_calls==1);
    assert(s->ep_in==2 && s->ep_out==1 && s->ep_irq==3 && s->tx_maxpacket==512 && selected_controller==0);
    assert(rtl8153_open(why,sizeof(why))==NULL); /* no concurrent owner */
    assert(rtl8153_link(s,&speed)==0 && speed==0);
    regs[1][URE_PLA_PHYSTATUS]=URE_PHYSTATUS_LINK|URE_PHYSTATUS_FDX|URE_PHYSTATUS_1000MBPS;
    assert(rtl8153_link(s,&speed)==1 && speed==1000);
    memset(frame,0x42,60);assert(rtl8153_send(s,frame,60)==0);
    bulk_short=1;assert(rtl8153_send(s,frame,60)==-1);bulk_short=0;
    assert(rtl8153_receive(s,frame,sizeof(frame),0)==0 && submitted==3);
    /* Completions arrive out of order; Ethernet delivery must stay ordered. */
    complete(1,0x22,0,88);assert(rtl8153_receive(s,frame,sizeof(frame),0)==0);
    complete(0,0x11,0,88);assert(rtl8153_receive(s,frame,sizeof(frame),0)==60 && frame[0]==0x11);
    assert(rtl8153_receive(s,frame,sizeof(frame),0)==60 && frame[0]==0x22 && submitted==5);
    complete(2,0,0,23);assert(rtl8153_receive(s,frame,sizeof(frame),0)==-1);
    complete(0,0,0,0);assert(rtl8153_receive(s,frame,sizeof(frame),0)==0);
    regs[1][URE_PLA_PHYSTATUS]=0;assert(rtl8153_link(s,&speed)==0);
    rtl8153_close(s);assert(closed==1 && released==1 && cancelled==1 && !g_open);
    setup();no_device=1;assert(!rtl8153_open(why,sizeof(why)) && closed==1 && patch_calls==0);
    setup();control_fail=1;assert(!rtl8153_open(why,sizeof(why)) && released==1 && !g_open && patch_calls==0);
    setup();patch_fail=1;assert(!rtl8153_open(why,sizeof(why)) && released==1 && !g_open);
    setup();ep_fail=1;assert(!rtl8153_open(why,sizeof(why)) && released==1 && !g_open);
    setup();acquire_fail=1;assert(!rtl8153_open(why,sizeof(why)) && !released && closed==1);
    setup();client_fail=1;assert(!rtl8153_open(why,sizeof(why)) && !closed && !query_calls);
    setup();s=rtl8153_open(why,sizeof(why));assert(s);s->medium=1;fail_submission=2;
    assert(rtl8153_receive(s,frame,sizeof(frame),0)==-1 && s->io_error && submitted==2);
    assert(rtl8153_receive(s,frame,sizeof(frame),0)==-1 && submitted==2);
    rtl8153_close(s);assert(cancelled==1);
    setup();s=rtl8153_open(why,sizeof(why));assert(s);rtl8153_abandon_title(s);
    assert(!closed && !released && !cancelled && !g_open);
    setup();rtl8153_set_cancel_check(should_stop);stop_requested=1;
    assert(!rtl8153_open(why,sizeof(why)) && !g_open && released==1);
    puts("transport: endpoints, open/close, link/TX, ordered async DMA, truncation, partial-ring failure and title abandonment passed");
}
