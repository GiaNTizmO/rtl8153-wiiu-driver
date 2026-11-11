"""Run the actual read-only probe with fixed-memory IOS mocks under sanitizers."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/tests/mocha-boot-state"
OUT.mkdir(parents=True, exist_ok=True)
for name in ("coreinit/debug.h", "coreinit/thread.h", "coreinit/time.h",
             "mocha/mocha.h", "whb/proc.h"):
    p = OUT / "include" / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text("/* Test declarations provided by harness. */\n")
source = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../../../build/mocha-boot-state/profile.h"
#define MOCHA_RESULT_SUCCESS 0
static char messages[4096];
static unsigned used, reads, initialized, deinitialized;
static uint32_t reload_word, net_word, fail_at, corrupt_at;
static uint32_t reload_diag[4];
static uint32_t status[16];
static unsigned status_reads,diag_reads;
static int unstable;
static int candidate, init_fail;
static void OSReport(const char *format, ...) {
    va_list ap; va_start(ap, format);
    int n=vsnprintf(messages+used, sizeof(messages)-used, format, ap);
    va_end(ap); assert(n>=0 && (unsigned)n<sizeof(messages)-used); used+=(unsigned)n;
}
static int Mocha_InitLibrary(void) { ++initialized; return init_fail ? -1 : 0; }
static void Mocha_DeInitLibrary(void) { ++deinitialized; }
static void OSSleepTicks(unsigned ticks) { (void)ticks; }
static unsigned OSMillisecondsToTicks(unsigned n) { return n; }
static unsigned long long OSGetTime(void) { return 0x123456789ULL; }
static void WHBProcInit(void) {}
static void WHBProcShutdown(void) {}
static int WHBProcIsRunning(void) { return 0; }
static int Mocha_IOSUKernelRead32(uint32_t address, uint32_t *value) {
    ++reads;
    if(address==fail_at) return -1;
    if(address==0x0812a120) { *value=reload_word; return 0; }
    if(address==0x12300ae4) { *value=net_word; return 0; }
    if(address>=CANDIDATE_RELOAD_DIAG_ADDRESS && address<CANDIDATE_RELOAD_DIAG_ADDRESS+16) {
        ++diag_reads;
        *value=reload_diag[(address-CANDIDATE_RELOAD_DIAG_ADDRESS)/4]; return 0;
    }
    if(address>=0x1288d040 && address<0x1288d080) {
        ++status_reads; *value=status[(address-0x1288d040)/4];
        if(unstable && status_reads>16) *value ^= 1;
        return 0;
    }
    const uint32_t *p; unsigned n, offset;
    if(address>=0x08135000 && address<0x08139200) {
        p=candidate?candidate_ios_kernel:official_ios_kernel;
        n=candidate?sizeof(candidate_ios_kernel):sizeof(official_ios_kernel);
        offset=address-0x08135000;
    } else {
        assert(address>=0x12431900 && address<0x12432000);
        p=candidate?candidate_ios_net:official_ios_net;
        n=candidate?sizeof(candidate_ios_net):sizeof(official_ios_net);
        offset=address-0x12431900;
    }
    *value=offset<n?p[offset/4]:0xcccccccc;
    if(!candidate && address==0x12431900+OFFICIAL_IOS_NET_BYTES/4*4)
        *value |= 0x55; /* Actual trailing padding is not part of .text. */
    if(address==corrupt_at) *value ^= 0x10000;
    return 0;
}
#define main probe_main
/* Skip second inclusion of generated declarations in the actual source. */
'''
# The generated header deliberately contains only const tables. Guard the
# second inclusion for this harness without changing production declarations.
profile = ROOT / "build/mocha-boot-state/profile.h"
header_copy = OUT / "profile_once.h"
header_copy.write_text("#pragma once\n" + profile.read_text())
source = source.replace('#include "../../../../build/mocha-boot-state/profile.h"', '#include "profile_once.h"')
actual = (ROOT / "tools/mocha_boot_state/main.c").read_text()
actual = actual.replace('#include "../../build/mocha-boot-state/profile.h"', '#include "profile_once.h"')
actual = actual.replace('#include "../../native/mocha_alloc_status.h"',
                        '#include "'+(ROOT/"native/mocha_alloc_status.h").as_posix()+'"')
(OUT / "probe.c").write_text(actual)
source += r'''
#include "probe.c"
#undef main
static void reset(int is_candidate) {
    used=reads=initialized=deinitialized=0; messages[0]=0;
    status_reads=diag_reads=0; unstable=0; memset(status,0,sizeof(status)); memset(reload_diag,0,sizeof(reload_diag));
    fail_at=corrupt_at=0; init_fail=0; candidate=is_candidate;
    reload_word=candidate?CANDIDATE_RELOAD_WORD:OFFICIAL_RELOAD_WORD;
    net_word=candidate?CANDIDATE_NET_WORD:0xebffffe2;
}
int main(void) {
    reset(0); inspect();
    assert(strstr(messages,"Mocha kernel text: official=MATCH allocation_candidate=no"));
    assert(strstr(messages,"Mocha NET text: official=MATCH allocation_candidate=no"));
    assert(strstr(messages,"NET allocation hook absent"));
    assert(initialized==1 && deinitialized==1);
    assert(status_reads==0);
    assert(diag_reads==0);
    reset(1); inspect();
    assert(strstr(messages,"Mocha kernel text: official=no allocation_candidate=MATCH"));
    assert(strstr(messages,"Mocha NET text: official=no allocation_candidate=MATCH"));
    assert(strstr(messages,"execution/free not proven"));
    assert(status_reads==32 && strstr(messages,"Status absent/incomplete"));
    reload_diag[0]=0x524c4445; reload_diag[1]=0x48445250; reload_diag[2]=0x4e455450;
    inspect(); assert(strstr(messages,"NET patcher returned"));
    memset(reload_diag,0,sizeof(reload_diag)); reload_diag[0]=0x524c4445;
    inspect(); assert(strstr(messages,"rejected launch header"));
    memset(reload_diag,0,sizeof(reload_diag)); reload_diag[0]=0x524c4445; reload_diag[1]=0x48445250;
    inspect(); assert(strstr(messages,"NET patcher did not return"));
    reset(1);
    status[0]=RTL_ALLOC_STATUS_MAGIC; status[1]=1; status[2]=RTL_ALLOC_FREED;
    status[4]=0x18001000; status[5]=status[6]=status[7]=1;
    status[15]=~RTL_ALLOC_STATUS_MAGIC;
    inspect(); assert(strstr(messages,"NET allocation/data/free path returned"));
    used=0; messages[0]=0; status[7]=0;
    inspect(); assert(!strstr(messages,"NET allocation/data/free path returned"));
    assert(strstr(messages,"did not report complete success"));
    used=0; messages[0]=0; status[7]=1; status[4]++;
    inspect(); assert(!strstr(messages,"NET allocation/data/free path returned"));
    used=0; messages[0]=0; status[4]--; status[3]=0xffffffff;
    inspect(); assert(!strstr(messages,"NET allocation/data/free path returned"));
    status[15]=0; inspect(); assert(strstr(messages,"Status absent/incomplete"));
    reset(1); unstable=1; inspect(); assert(strstr(messages,"Status changed"));
    reset(1); status[0]=RTL_ALLOC_STATUS_MAGIC; status[1]=1;
    status[15]=~RTL_ALLOC_STATUS_MAGIC; status[8]=1;
    inspect(); assert(strstr(messages,"reserved words differ"));
    reset(1); net_word=0xebffffe2; inspect();
    assert(strstr(messages,"allocation_candidate=MATCH"));
    assert(strstr(messages,"NET allocation hook absent"));
    assert(status_reads==0);
    assert(diag_reads==4 && strstr(messages,"invocation unproven"));
    reset(1); net_word=0xebffffe2;
    reload_diag[0]=0x524c4445;
    inspect(); assert(diag_reads==4 && status_reads==0);
    assert(strstr(messages,"rejected launch header"));
    reset(1); fail_at=CANDIDATE_RELOAD_DIAG_ADDRESS;
    inspect(); assert(strstr(messages,"Reload diagnostic read failed"));
    reset(0); reload_word=0; inspect();
    assert(reads==2 && strstr(messages,"Unknown reload hook"));
    reset(0); init_fail=1; inspect();
    assert(reads==0 && deinitialized==0 && strstr(messages,"Mocha unavailable"));
    reset(0); fail_at=0x12300ae4; inspect();
    assert(reads==2 && deinitialized==1 && strstr(messages,"Hook read failed"));
    reset(0); fail_at=0x08135004; inspect();
    assert(strstr(messages,"Mocha kernel READ_FAILED"));
    assert(!strstr(messages,"Mocha kernel text:"));
    reset(0); corrupt_at=0x12431900; inspect();
    assert(strstr(messages,"Mocha NET text: official=no allocation_candidate=no"));
    reset(1); corrupt_at=0x08135000; inspect();
    assert(strstr(messages,"Mocha kernel text: official=no allocation_candidate=no"));
    reset(0); uint32_t value;
    assert(read_word(0x0812a121,&value)==-1);
    assert(read_word(0x08134ffc,&value)==-1);
    assert(read_word(0x08139200,&value)==-1);
    assert(read_word(0x12432000,&value)==-1);
    assert(read_word(0x1288d020,&value)==-1);
    assert(reads==0);
    puts("Mocha boot state: code identities, absent/success/torn/invalid status, read failure and fixed address bounds passed");
}
'''
(OUT / "harness.c").write_text(source)
subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-g", "-fsanitize=address,undefined", "-I"+str(OUT/"include"),
                str(OUT/"harness.c"), "-o", str(OUT/"test")], check=True)
subprocess.run([str(OUT/"test")], check=True)
