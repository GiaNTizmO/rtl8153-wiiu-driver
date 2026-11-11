/* Read-only capability check for a future native loader.
 * It never maps memory, writes IOSU memory, executes an IOS function, or
 * touches USB. It only opens the optional Stroopwafel device and asks for its
 * documented API version. */
#include <coreinit/debug.h>
#include <coreinit/ios.h>
#include <coreinit/time.h>
#include <coreinit/thread.h>
#include <whb/proc.h>
#include <stdio.h>

static FILE *report;

static void say(const char *line)
{
    OSReport("RTL_LOADER_PROBE: %s\n", line);
    if (report) {
        fputs(line, report);
        fputc('\n', report);
        fflush(report);
    }
}

static void inspect(void)
{
    char line[160];
    int fd = IOS_Open("/dev/stroopwafel", 0);
    if (fd < 0) {
        snprintf(line, sizeof(line),
                 "Stroopwafel unavailable; IOS_Open rc=%d; no native action",
                 fd);
        say(line);
        return;
    }

    /* IPC needs cache-aligned storage; the server returns 4 on success. */
    uint32_t version[16] __attribute__((aligned(64))) = {0};
    int rc = IOS_Ioctl(fd, 0x1, NULL, 0, version, sizeof(uint32_t));
    snprintf(line, sizeof(line),
             "Stroopwafel device present; API ioctl rc=%d version=0x%08lx",
             rc, (unsigned long)version[0]);
    say(line);
    IOS_Close(fd);
}

int main(void)
{
    OSReport("RTL_LOADER_PROBE: main entered\n");
    WHBProcInit();
    const char *paths[] = {
        "fs:/vol/external01/wiiu/rtl8153/loader-capability-probe.log",
        "/vol/external01/wiiu/rtl8153/loader-capability-probe.log",
        "sd:/wiiu/rtl8153/loader-capability-probe.log",
    };
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        report = fopen(paths[i], "w");
        if (report) break;
    }
    say("read-only loader capability check; map/write/execute/USB disabled");
    inspect();
    say("Inspection complete. HOME -> Close to return.");
    if (report) { fclose(report); report = NULL; }
    while (WHBProcIsRunning()) OSSleepTicks(OSMillisecondsToTicks(16));
    WHBProcShutdown();
    return 0;
}
