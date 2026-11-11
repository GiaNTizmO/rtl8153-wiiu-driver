/* Read-only native interface inspection. No network configuration, USB claim,
 * memory patch, firmware loading, OTP access or application socket tests. */
#include <coreinit/ios.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/debug.h>
#include <mocha/mocha.h>
#include <whb/log.h>
#include <whb/proc.h>
#include <stdio.h>
#include <string.h>
#include "../../native/firmware_556.h"

static FILE *report;
static void tcp_report(const char *line)
{
    OSReport("RTL_NATIVE_PROBE: %s\n",line);
}
static void say(const char *line)
{
    WHBLogPrint(line);
    if(report){fputs(line,report);fputc('\n',report);fflush(report);}
}
static int read32(uint32_t address,uint32_t *out)
{
    /* Only mapped IOS-NET RAM addresses recovered from this exact image. */
    if(address<0x12300000u||address>0x1288d024u||(address&3))return -1;
    return Mocha_IOSUKernelRead32(address,out)==MOCHA_RESULT_SUCCESS?0:-1;
}
static int dump_words(const char *name,uint32_t address,unsigned count)
{
    char line[180];
    snprintf(line,sizeof(line),"%s at %08lx (%u words)",name,(unsigned long)address,count);say(line);
    for(unsigned i=0;i<count;i+=4){
        uint32_t v[4]={0};
        for(unsigned j=0;j<4&&i+j<count;++j){
            if(read32(address+(i+j)*4,&v[j])){say("  READ FAILED; dump aborted");return -1;}
        }
        snprintf(line,sizeof(line),"  %08lx: %08lx %08lx %08lx %08lx",
            (unsigned long)(address+i*4),(unsigned long)v[0],(unsigned long)v[1],
            (unsigned long)v[2],(unsigned long)v[3]);say(line);
    }
    return 0;
}
static void inspect(void)
{
    char line[220];unsigned matched=0;
    say("Read-only IOS-NET inspection; no driver installation.");
    if(Mocha_InitLibrary()!=MOCHA_RESULT_SUCCESS){say("Mocha unavailable");return;}
    for(unsigned i=0;i<sizeof(ios_fingerprints)/sizeof(ios_fingerprints[0]);++i){
        uint32_t hash=2166136261u;int rc=0;
        for(unsigned j=0;j<16;++j){
            uint32_t word=0;
            if(read32(ios_fingerprints[i].address+j*4,&word)){rc=-1;break;}
            for(int shift=24;shift>=0;shift-=8)hash=(hash^((word>>shift)&255))*16777619u;
        }
        int ok=!rc&&hash==ios_fingerprints[i].word;
        matched+=ok;
        snprintf(line,sizeof(line),"%s %s %08lx got=%08lx expected=%08lx",
            ok?"MATCH":"MISMATCH",ios_fingerprints[i].name,
            (unsigned long)ios_fingerprints[i].address,(unsigned long)hash,
            (unsigned long)ios_fingerprints[i].word);say(line);
    }
    snprintf(line,sizeof(line),"Code 64-byte fingerprints: %u/%u (not full image verification)",
        matched,(unsigned)(sizeof(ios_fingerprints)/sizeof(ios_fingerprints[0])));say(line);
    if(matched!=sizeof(ios_fingerprints)/sizeof(ios_fingerprints[0])){
        say("Profile mismatch: skipping dynamic pointer inspection.");
        Mocha_DeInitLibrary();return;
    }
    int ops_ok=1;
    for(unsigned i=0;i<15;++i){uint32_t v=0;if(read32(0x12456dd0+i*4,&v)||v!=ios_asix_ops_words[i])ops_ok=0;}
    say(ops_ok?"ASIX operation table MATCH":"ASIX operation table MISMATCH");
    dump_words("Native interface pointers",0x128090bc,2);
    for(unsigned i=0;i<2;++i){
        uint32_t ptr=0;
        if(!read32(0x128090bc+i*4,&ptr)&&ptr>=0x124c6000&&ptr<=0x1288ff00&&!(ptr&3))
            dump_words(i?"Interface slot 1":"Interface slot 0",ptr,52);
    }
    uint32_t ctx=0;
    if(!read32(0x1288d020,&ctx)){
        snprintf(line,sizeof(line),"USB Ethernet context pointer: %08lx",(unsigned long)ctx);say(line);
        if(ctx>=0x124c6000&&ctx<=0x12884d54&&!(ctx&3)){
            dump_words("USB Ethernet context",ctx,16);
            dump_words("USB Ethernet discovery slots",ctx+0x8294,16);
        }else say("Context not in verified BSS range; no dereference.");
    }
    /* Known status-only request from Stroopwafel's public service client.
     * Device ACL refusal is recorded; no privilege bypass is attempted. */
    int fd=IOS_Open("/dev/net/ifmgr/ncl",(IOSOpenMode)0);
    snprintf(line,sizeof(line),"/dev/net/ifmgr/ncl open=%d",fd);say(line);
    if(fd>=0){
        for(unsigned id=0;id<2;++id){
            uint16_t in[32] __attribute__((aligned(64)))={0};
            uint16_t out[32] __attribute__((aligned(64)))={0};
            in[0]=id;
            int rc=IOS_Ioctl(fd,0x14,in,2,out,8);
            snprintf(line,sizeof(line),"NCL status id=%u rc=%d data=%04x %04x %04x %04x",id,rc,out[0],out[1],out[2],out[3]);say(line);
        }
        IOS_Close(fd);
    }
    Mocha_DeInitLibrary();say("Inspection complete. HOME -> Close to return.");
}
int main(void)
{
    /* No graphics: previous version faulted in OSScreenClearBufferEx before
     * the first Mocha call. TCP OSReport works without screen buffers. */
    OSReport("RTL_NATIVE_PROBE: main entered (headless revision 2)\n");
    WHBProcInit();WHBAddLogHandler(tcp_report);
    const char *paths[]={"fs:/vol/external01/wiiu/rtl8153/native-probe.log",
        "/vol/external01/wiiu/rtl8153/native-probe.log","sd:/wiiu/rtl8153/native-probe.log"};
    for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);++i){report=fopen(paths[i],"w");if(report)break;}
    OSReport("RTL_NATIVE_PROBE: report file %s\n",report?"opened":"unavailable; using TCP only");
    inspect();
    if(report){fclose(report);report=NULL;}
    while(WHBProcIsRunning())OSSleepTicks(OSMillisecondsToTicks(16));
    WHBRemoveLogHandler(tcp_report);WHBProcShutdown();return 0;
}
