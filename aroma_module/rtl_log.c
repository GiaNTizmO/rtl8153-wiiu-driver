#include "rtl_log.h"
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <whb/log.h>

/* Only lifecycle and low-frequency hardware diagnostics use this logger.
 * Never write packet/socket tracing to SD. Each line is closed immediately
 * so early initialization failures remain available without networking. */
void rtl_log(const char *fmt, ...)
{
    static atomic_flag writing = ATOMIC_FLAG_INIT;
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    WHBLogPrint(line);
    if (atomic_flag_test_and_set_explicit(&writing, memory_order_acquire)) return;
    static const char *paths[] = {
        "fs:/vol/external01/wiiu/rtl8153/driver.log",
        "/fs/vol/external01/wiiu/rtl8153/driver.log",
        "fs:/wiiu/rtl8153/driver.log"
    };
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        FILE *f = fopen(paths[i], "a");
        if (!f) continue;
        fputs(line, f);
        fputc('\n', f);
        fclose(f);
        break;
    }
    atomic_flag_clear_explicit(&writing, memory_order_release);
}
