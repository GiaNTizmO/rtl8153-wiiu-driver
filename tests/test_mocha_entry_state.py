"""Run the actual read-only entry-state inspector with fixed-memory IOS mocks
under ASan/UBSan. Host test only; not a console result."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/tests/mocha-entry-state"
OUT.mkdir(parents=True, exist_ok=True)
for name in ("coreinit/debug.h", "coreinit/thread.h", "coreinit/time.h",
             "mocha/mocha.h", "whb/proc.h"):
    p = OUT / "include" / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text("/* Test declarations provided by harness. */\n")
profile = ROOT / "build/mocha-entry-state/profile.h"
(OUT / "profile_once.h").write_text("#pragma once\n" + profile.read_text())
actual = (ROOT / "tools/mocha_entry_state/main.c").read_text()
actual = actual.replace('#include "../../build/mocha-entry-state/profile.h"', '#include "profile_once.h"')
actual = actual.replace('#include "../../native/mocha_entry_status.h"',
                        '#include "' + (ROOT / "native/mocha_entry_status.h").as_posix() + '"')
(OUT / "probe.c").write_text(actual)
source = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "profile_once.h"
#define MOCHA_RESULT_SUCCESS 0
static char messages[8192];
static unsigned used, reads, initialized, deinitialized, status_reads, diag_reads;
static uint32_t reload_word, net_word, fail_at, status[16], reload_diag[4];
static int candidate, init_fail, unstable;
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
        ++diag_reads; *value=reload_diag[(address-CANDIDATE_RELOAD_DIAG_ADDRESS)/4]; return 0;
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
    return 0;
}
#define main probe_main
#include "probe.c"
#undef main
static void reset(int is_candidate) {
    used=reads=initialized=deinitialized=status_reads=diag_reads=0; messages[0]=0;
    unstable=0; fail_at=0; init_fail=0; candidate=is_candidate;
    memset(status,0,sizeof(status)); memset(reload_diag,0,sizeof(reload_diag));
    reload_word=candidate?CANDIDATE_RELOAD_WORD:OFFICIAL_RELOAD_WORD;
    net_word=candidate?CANDIDATE_NET_WORD:0xebffffe2;
    reload_diag[0]=0x524c4445; reload_diag[1]=0x48445250; reload_diag[2]=0x4e455450;
}
static void success(uint32_t page) {
    uint32_t nonce=page^RTL_ENTRY_NONCE_SALT;
    uint32_t s[16]={RTL_ENTRY_STATUS_MAGIC,2,RTL_ENTRY_RETURNED_OK,0,page,1,0,
                    nonce^RTL_ENTRY_BLOB_MARKER,page+8,RTL_ENTRY_BLOB_MARKER,nonce,1,1,1,1,
                    ~RTL_ENTRY_STATUS_MAGIC};
    memcpy(status,s,sizeof(s));
}
#define OK "NET local-heap page code executed and returned"
static void again(void) { used=0; messages[0]=0; inspect(); }
int main(void) {
    reset(0); inspect();
    assert(strstr(messages,"Mocha NET text: official=MATCH entry_candidate=no"));
    assert(strstr(messages,"NET entry hook absent") && status_reads==0 && diag_reads==0);
    reset(1); inspect();
    assert(strstr(messages,"Mocha NET text: official=no entry_candidate=MATCH"));
    assert(strstr(messages,"NET patcher returned"));
    assert(status_reads==32 && strstr(messages,"Status absent/incomplete"));
    reset(1); success(0x124c7000); inspect(); assert(strstr(messages,OK));
    success(0x12505000); again(); assert(strstr(messages,OK));
    /* Every field except init_result must be exact; any deviation rejects. */
    for (unsigned i=2;i<15;i++) {
        if (i==3) continue;
        success(0x124c7000); status[i]^=0x100; again();
        assert(!strstr(messages,OK));
    }
    success(0x124c7000); status[3]=0xffffffff; again(); assert(!strstr(messages,OK));
    success(0x12506000); again(); assert(!strstr(messages,OK));   /* past heap */
    success(0x124c5000); again(); assert(!strstr(messages,OK));   /* below heap */
    success(0x124c7000); status[2]=RTL_ENTRY_RETURNED_BAD; again();
    assert(strstr(messages,"marker/PC/return differ"));
    success(0x124c7000); status[2]=RTL_ENTRY_FLUSH_FAILED; again();
    assert(strstr(messages,"FlushDCache nonzero"));
    success(0x124c7000); status[2]=RTL_ENTRY_ENTERING; again();
    assert(strstr(messages,"unexpected terminal stage"));
    success(0x124c7000); status[15]=0; again(); assert(strstr(messages,"Status absent/incomplete"));
    success(0x124c7000); status[1]=1; again(); assert(strstr(messages,"Status absent/incomplete"));
    success(0x124c7000); status[0]=0x52544c41; again(); assert(strstr(messages,"Status absent/incomplete"));
    reset(1); success(0x124c7000); unstable=1; inspect(); assert(strstr(messages,"Status changed"));
    reset(1); success(0x124c7000); net_word=0xebffffe2; inspect();
    assert(strstr(messages,"NET entry hook absent") && status_reads==0 && !strstr(messages,OK));
    reset(1); success(0x124c7000); fail_at=0x1288d07c; inspect(); assert(strstr(messages,"Status read failed"));
    reset(0); reload_word=0; inspect(); assert(reads==2 && strstr(messages,"Unknown reload hook"));
    reset(0); init_fail=1; inspect(); assert(reads==0 && strstr(messages,"Mocha unavailable"));
    reset(0); uint32_t value;
    assert(read_word(0x0812a121,&value)==-1 && read_word(0x12432000,&value)==-1);
    assert(read_word(0x1288d080,&value)==-1 && read_word(0x124c7000,&value)==-1);
    assert(reads==0);
    puts("Mocha entry state: identities, exact success fields, 13 single-field rejections, heap bounds, failure stages, torn/absent status and address bounds passed");
}
'''
(OUT / "harness.c").write_text(source)
subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                "-fsanitize=address,undefined", "-I" + str(OUT / "include"),
                str(OUT / "harness.c"), "-o", str(OUT / "test")], check=True)
subprocess.run([str(OUT / "test")], check=True)
