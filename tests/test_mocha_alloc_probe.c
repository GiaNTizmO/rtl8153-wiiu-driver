#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../native/mocha_alloc_status.h"

int rtl_mocha_net_alloc_probe_after_init(int);
static uint32_t page[1024] __attribute__((aligned(4096)));
static unsigned allocs, frees, logs;
static int original, fail_alloc, misalign;
static void *allocated;
static int rtl_mocha_net_init_alloc_probe(void)
{ return rtl_mocha_net_alloc_probe_after_init(original); }
void *rtl_net_alloc(uint32_t heap, uint32_t bytes, uint32_t align)
{
    assert(heap == 0xcafe && bytes == 4096 && align == 4096);
    ++allocs;
    allocated = fail_alloc ? NULL : (void *)(page + misalign);
    return allocated;
}
void rtl_net_free(uint32_t heap, void *p)
{
    assert(heap == 0xcafe && p == allocated); ++frees;
}
int rtl_net_printf(const char *format, ...)
{
    assert(format); ++logs; return 0;
}
static void reset(void)
{
    original = fail_alloc = misalign = 0;
    allocs = frees = logs = 0; allocated = NULL;
    memset(page, 0x5a, sizeof(page));
}
int main(void)
{
    reset(); original = -77;
    assert(rtl_mocha_net_init_alloc_probe() == -77 && !allocs && !logs);
    assert(rtl_mocha_alloc_status.stage==RTL_ALLOC_INIT_FAILED);
    reset(); original = 17; fail_alloc = 1;
    assert(rtl_mocha_net_init_alloc_probe() == 17 && allocs == 1 && !frees && logs == 1);
    assert(rtl_mocha_alloc_status.stage==RTL_ALLOC_NO_MEMORY);
    reset(); misalign = 1;
    assert(rtl_mocha_net_init_alloc_probe() == 0 && frees == 1);
    assert(rtl_mocha_alloc_status.stage==RTL_ALLOC_MISALIGNED);
    for (unsigned i = 0; i < 1024; ++i) assert(page[i] == 0x5a5a5a5a);
    reset(); original = 9;
    assert(rtl_mocha_net_init_alloc_probe() == 9 && allocs == 1 && frees == 1 && logs == 1);
    assert(rtl_mocha_alloc_status.magic==RTL_ALLOC_STATUS_MAGIC &&
           rtl_mocha_alloc_status.commit==~RTL_ALLOC_STATUS_MAGIC &&
           rtl_mocha_alloc_status.stage==RTL_ALLOC_FREED &&
           rtl_mocha_alloc_status.alloc_calls==1 && rtl_mocha_alloc_status.free_calls==1);
    assert(page[0] == 0x52544c50 && page[1023] == ~0x52544c50u);
    for (unsigned i = 1; i < 1023; ++i) assert(page[i] == 0x5a5a5a5a);
    puts("Mocha allocation candidate: original init result, allocation failure, alignment and owned-page bounds passed (host only)");
    return 0;
}
