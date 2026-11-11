#include "loader_contract.h"

static int aligned(uint32_t value)
{
    return (value & (RTL_IOS_PAGE_SIZE - 1u)) == 0;
}

static int range_ok(uint32_t start, uint32_t bytes)
{
    return bytes != 0 && start <= UINT32_MAX - bytes;
}

int rtl_ios_validate_mapping(const RtlIosMapping *m)
{
    if (!m || m->domain != RTL_IOS_DOMAIN_NET ||
        (m->flags & ~(RTL_IOS_MAP_READ | RTL_IOS_MAP_WRITE |
                       RTL_IOS_MAP_EXEC | RTL_IOS_MAP_DMA)) ||
        (m->flags & (RTL_IOS_MAP_READ | RTL_IOS_MAP_WRITE |
                     RTL_IOS_MAP_EXEC)) !=
            (RTL_IOS_MAP_READ | RTL_IOS_MAP_WRITE | RTL_IOS_MAP_EXEC) ||
        !aligned(m->physical_start) || !aligned(m->virtual_start) ||
        !aligned(m->bytes) || !range_ok(m->physical_start, m->bytes) ||
        !range_ok(m->virtual_start, m->bytes))
        return -1;
    return 0;
}

int rtl_ios_validate_entry_layout(const RtlIosMapping *m,
                                  const RtlIosEntryCheck *e)
{
    if (!e || rtl_ios_validate_mapping(m) ||
        e->image_start != m->virtual_start ||
        e->image_bytes < sizeof(uint32_t) || e->image_bytes > m->bytes ||
        (e->entry & 3u) || e->entry < e->image_start ||
        e->entry - e->image_start > e->image_bytes - sizeof(uint32_t) ||
        (e->result_word & 3u) || e->result_word < m->virtual_start ||
        e->result_word - m->virtual_start > m->bytes - sizeof(uint32_t) ||
        e->result_word - m->virtual_start < e->image_bytes)
        return -1;
    return 0;
}

int rtl_ios_validate_entry_check(const RtlIosLoaderState *s)
{
    if (!s || s->phase < RTL_IOS_LOADER_CACHE_CLEAN ||
        s->phase > RTL_IOS_LOADER_ARMED ||
        rtl_ios_validate_entry_layout(&s->mapping, &s->entry) ||
        s->entry.marker != 0x52544c50u)
        return -1;
    return 0;
}

int rtl_ios_loader_advance(RtlIosLoaderState *s,
                           enum RtlIosLoaderPhase next,
                           const RtlIosMapping *mapping,
                           const RtlIosEntryCheck *entry)
{
    if (!s || (s->backend != RTL_IOS_LOADER_STROOPWAFEL &&
               s->backend != RTL_IOS_LOADER_MOCHA) ||
        s->phase < RTL_IOS_LOADER_NEW || s->phase >= RTL_IOS_LOADER_ARMED ||
        next != s->phase + 1 || next > RTL_IOS_LOADER_ARMED)
        return -1;
    if (next == RTL_IOS_LOADER_MAPPED) {
        if (rtl_ios_validate_mapping(mapping)) return -1;
        s->mapping = *mapping;
    }
    if (next == RTL_IOS_LOADER_ENTRY_VERIFIED) {
        if (!entry || rtl_ios_validate_entry_layout(&s->mapping, entry) ||
            entry->marker != 0x52544c50u) return -1;
        s->entry = *entry;
    }
    if (next == RTL_IOS_LOADER_ARMED && rtl_ios_validate_entry_check(s))
        return -1;
    s->phase = next;
    return 0;
}
