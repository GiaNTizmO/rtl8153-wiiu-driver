/* Read only IOS code inspection. No IOSU writes, allocations, execution,
 * reload commands, USB access, or changes to logging configuration. */
#include <coreinit/debug.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <mocha/mocha.h>
#include <whb/proc.h>
#include <stdio.h>
#include "../../native/mocha_alloc_status.h"
#include "../../build/mocha-boot-state/profile.h"

static FILE *report;
static void say(const char *s)
{
    OSReport("RTL_BOOT_STATE: %s\n", s);
    if (report) { fputs(s, report); fputc('\n', report); fflush(report); }
}

static int read_word(uint32_t address, uint32_t *value)
{
    /* Fixed, mapped code regions only. Never follow a runtime pointer. */
    if ((address & 3u) ||
        !(address == 0x0812a120u || address == 0x12300ae4u ||
          (address >= 0x08135000u && address < 0x08139200u) ||
          (address >= 0x12431900u && address < 0x12432000u) ||
          (address >= RTL_ALLOC_STATUS_ADDRESS && address < RTL_ALLOC_STATUS_ADDRESS+64u) ||
          (address >= CANDIDATE_RELOAD_DIAG_ADDRESS &&
           address < CANDIDATE_RELOAD_DIAG_ADDRESS+16u))) return -1;
    return Mocha_IOSUKernelRead32(address, value) == MOCHA_RESULT_SUCCESS ? 0 : -1;
}

/* Match complete immutable .text bytes; lengths differ between builds.
 * An absent/unknown match never grants permission to activate anything. */
static int identify(const char *name, uint32_t base,
                     const uint32_t *official, unsigned official_bytes,
                     const uint32_t *candidate, unsigned candidate_bytes)
{
    int a = 1, b = 1;
    char line[160];
    unsigned official_words = (official_bytes + 3u) / 4u;
    unsigned candidate_words = (candidate_bytes + 3u) / 4u;
    unsigned n = official_words > candidate_words ? official_words : candidate_words;
    for (unsigned i = 0; i < n; ++i) {
        uint32_t word = 0;
        if (read_word(base + i * 4, &word)) {
            snprintf(line, sizeof(line), "%s READ_FAILED at %08lx", name, (unsigned long)(base+i*4));
            say(line); return 0;
        }
        if (i < official_words) {
            unsigned bytes = official_bytes - i*4;
            uint32_t mask = bytes >= 4 ? UINT32_MAX : UINT32_MAX << (8*(4-bytes));
            if ((word & mask) != official[i]) a = 0;
        }
        if (i < candidate_words) {
            unsigned bytes = candidate_bytes - i*4;
            uint32_t mask = bytes >= 4 ? UINT32_MAX : UINT32_MAX << (8*(4-bytes));
            if ((word & mask) != candidate[i]) b = 0;
        }
        if ((i & 255u) == 255u) OSSleepTicks(OSMillisecondsToTicks(1));
    }
    snprintf(line, sizeof(line), "%s text: official=%s allocation_candidate=%s",
             name, a ? "MATCH" : "no", b ? "MATCH" : "no");
    say(line);
    return (a ? 1 : 0) | (b ? 2 : 0);
}
#define WORDS(a) ((unsigned)(sizeof(a)/sizeof((a)[0])))

static void inspect_status(void)
{
    uint32_t a[16],b[16];
    char line[220];
    _Static_assert(CANDIDATE_STATUS_ADDRESS==RTL_ALLOC_STATUS_ADDRESS,"status address");
    for(unsigned i=0;i<16;i++) if(read_word(RTL_ALLOC_STATUS_ADDRESS+i*4,&a[i])) {
        say("Status read failed."); return;
    }
    for(unsigned i=0;i<16;i++) if(read_word(RTL_ALLOC_STATUS_ADDRESS+i*4,&b[i]) || a[i]!=b[i]) {
        say("Status changed or read failed; no result accepted."); return;
    }
    if(a[0]!=RTL_ALLOC_STATUS_MAGIC || a[1]!=RTL_ALLOC_STATUS_ABI ||
       a[15]!=~RTL_ALLOC_STATUS_MAGIC) {
        say("Status absent/incomplete; allocation unverified."); return;
    }
    for(unsigned i=8;i<15;i++) if(a[i]) {say("Status reserved words differ; unsupported result.");return;}
    snprintf(line,sizeof(line),"NET allocation status: stage=%lu init=%ld page=%08lx data=%lu alloc=%lu free=%lu",
             (unsigned long)a[2],(long)(int32_t)a[3],(unsigned long)a[4],
             (unsigned long)a[5],(unsigned long)a[6],(unsigned long)a[7]);say(line);
    if(a[2]==RTL_ALLOC_FREED && (int32_t)a[3]>=0 && a[4] && !(a[4]&4095u) &&
       a[5]==1 && a[6]==1 && a[7]==1)
        say("NET allocation/data/free path returned; executable heap and native driver remain unverified.");
    else say("Allocation test did not report complete success; no native activation.");
}

static void inspect_reload_diag(void)
{
    uint32_t d[4]; char line[200];
    for (unsigned i=0;i<4;i++) if (read_word(CANDIDATE_RELOAD_DIAG_ADDRESS+i*4,&d[i])) {
        say("Reload diagnostic read failed."); return;
    }
    snprintf(line,sizeof(line),"reload diag=%08lx %08lx %08lx %08lx",
             (unsigned long)d[0],(unsigned long)d[1],
             (unsigned long)d[2],(unsigned long)d[3]); say(line);
    if (d[0]!=0x524c4445u) say("No retained kernel_launch_ios entry marker; invocation unproven.");
    else if (d[1]!=0x48445250u) say("kernel_launch_ios entered but rejected launch header.");
    else if (d[2]!=0x4e455450u) say("launch header accepted but NET patcher did not return.");
    else say("NET patcher returned; inspect NET hook and mailbox separately.");
}

static void inspect(void)
{
    uint32_t reload = 0, init = 0;
    char line[180];
    say("Read-only boot state; no reload, writes, native entry or USB access.");
    if (Mocha_InitLibrary() != MOCHA_RESULT_SUCCESS) { say("Mocha unavailable"); return; }
    if (read_word(0x0812a120u, &reload) || read_word(0x12300ae4u, &init)) {
        say("Hook read failed; inspection aborted.");
        Mocha_DeInitLibrary(); return;
    }
    snprintf(line, sizeof(line), "reload hook=%08lx NET init call=%08lx",
             (unsigned long)reload, (unsigned long)init); say(line);
    if (reload != OFFICIAL_RELOAD_WORD && reload != CANDIDATE_RELOAD_WORD) {
        say("Unknown reload hook; skipping payload reads.");
        Mocha_DeInitLibrary(); return;
    }
    int kernel=identify("Mocha kernel", 0x08135000u, official_ios_kernel, OFFICIAL_IOS_KERNEL_BYTES,
             candidate_ios_kernel, CANDIDATE_IOS_KERNEL_BYTES);
    int net=identify("Mocha NET", 0x12431900u, official_ios_net, OFFICIAL_IOS_NET_BYTES,
             candidate_ios_net, CANDIDATE_IOS_NET_BYTES);
    say(init == 0xebffffe2u ? "NET allocation hook absent." :
        init == CANDIDATE_NET_WORD ? "NET allocation hook present; execution/free not proven by code inspection." :
        "NET init call differs from both profiles.");
    say("Code identity does not prove the reload path ran or allocation succeeded.");
    if(kernel==2 && net==2 && reload==CANDIDATE_RELOAD_WORD) {
        inspect_reload_diag();
        if(init==CANDIDATE_NET_WORD) inspect_status();
    }
    Mocha_DeInitLibrary();
}

int main(void)
{
    OSReport("RTL_BOOT_STATE: main entered (headless)\n");
    WHBProcInit();
    const char *paths[] = {"fs:/vol/external01/wiiu/rtl8153/mocha-boot-state.log",
        "/vol/external01/wiiu/rtl8153/mocha-boot-state.log", "sd:/wiiu/rtl8153/mocha-boot-state.log"};
    for (unsigned i=0; i<WORDS(paths); ++i) { report=fopen(paths[i], "w"); if(report) break; }
    char run[100];
    unsigned long long ticks=(unsigned long long)OSGetTime();
    snprintf(run,sizeof(run),"Boot State invocation ticks=%016llx",ticks);
    say(run);
    inspect();
    say("Inspection complete. HOME -> Close to return.");
    if (report) { fclose(report); report=NULL; }
    while (WHBProcIsRunning()) OSSleepTicks(OSMillisecondsToTicks(16));
    WHBProcShutdown(); return 0;
}
