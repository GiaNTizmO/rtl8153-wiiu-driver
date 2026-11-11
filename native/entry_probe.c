/*
 * Minimal native IOS-NET entry probe.
 *
 * This deliberately has no IOS, UHS, USB or network dependencies. A loader
 * may call it only after the image has been mapped into the IOS-NET domain.
 * The caller supplies a mapped word so the result can be observed without a
 * logging dependency or a callback into PPC code.
 */
#include <stdint.h>

#define RTL_IOS_ENTRY_PROBE_MARKER 0x52544c50u /* RTLP */

uint32_t rtl_ios_entry_probe(const uint32_t *config, uint32_t *output)
{
    if (config && *config)
        *(volatile uint32_t *)(uintptr_t)*config = RTL_IOS_ENTRY_PROBE_MARKER;
    if (output)
        *output = RTL_IOS_ENTRY_PROBE_MARKER;
    return RTL_IOS_ENTRY_PROBE_MARKER;
}
