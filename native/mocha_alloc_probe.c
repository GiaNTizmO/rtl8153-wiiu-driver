/* Local experimental Mocha candidate; not deployed on a console.
 * The isolated build wires a guarded NET initialization hook on IOS reload.
 * Never callable directly from PPC.
 * This tests allocation/data access only; no code is copied or executed. */
#include <stdint.h>
#include <stddef.h>
#include "mocha_alloc_status.h"

extern void *rtl_net_alloc(uint32_t heap, uint32_t bytes, uint32_t alignment);
extern void rtl_net_free(uint32_t heap, void *memory);
extern int rtl_net_printf(const char *format, ...);

#define PROBE_HEAP 0xcafeu
#define PROBE_PAGE 4096u
#define PROBE_MARKER 0x52544c50u

volatile RtlMochaAllocStatus rtl_mocha_alloc_status
    __attribute__((section(".rtl_alloc_status"),aligned(64)));

static void finish_status(uint32_t stage)
{
    rtl_mocha_alloc_status.stage=stage;
    rtl_mocha_alloc_status.commit=~RTL_ALLOC_STATUS_MAGIC;
}

/* The assembly shim calls original_init first with the original stack
 * congruence, then provides an AAPCS-aligned stack and its exact result here. */
int rtl_mocha_net_alloc_probe_after_init(int result)
{
    volatile RtlMochaAllocStatus *s=&rtl_mocha_alloc_status;
    s->commit=0;
    s->magic=RTL_ALLOC_STATUS_MAGIC; s->abi=RTL_ALLOC_STATUS_ABI;
    s->stage=RTL_ALLOC_ENTERED; s->init_result=(uint32_t)result;
    s->page=0; s->data_ok=0; s->alloc_calls=0; s->free_calls=0;
    for (unsigned i=0;i<7;i++) s->reserved[i]=0;
    if (result < 0) { finish_status(RTL_ALLOC_INIT_FAILED); return result; }
    s->alloc_calls=1;
    volatile uint32_t *page = rtl_net_alloc(PROBE_HEAP, PROBE_PAGE, PROBE_PAGE);
    if (!page) {
        finish_status(RTL_ALLOC_NO_MEMORY);
        rtl_net_printf("RTL_NET_ALLOC: allocation failed; init=%d\n", result);
        return result;
    }
    uintptr_t address = (uintptr_t)page;
    s->page=(uint32_t)address;
    int data_ok = 0;
    if (!(address & (PROBE_PAGE - 1u))) {
        page[0] = PROBE_MARKER;
        page[PROBE_PAGE / sizeof(*page) - 1] = ~PROBE_MARKER;
        data_ok = page[0] == PROBE_MARKER &&
                  page[PROBE_PAGE / sizeof(*page) - 1] == ~PROBE_MARKER;
    }
    rtl_net_free(PROBE_HEAP, (void *)page);
    s->free_calls=1; s->data_ok=(uint32_t)data_ok;
    finish_status(address & (PROBE_PAGE-1u) ? RTL_ALLOC_MISALIGNED : RTL_ALLOC_FREED);
    rtl_net_printf("RTL_NET_ALLOC: page=%08lx data=%d freed; exec unverified\n",
                   (unsigned long)address, data_ok);
    return result;
}
