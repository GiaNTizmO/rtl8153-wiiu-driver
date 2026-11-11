#include "rtl8153_framing.h"
#include <string.h>

static uint32_t get_le32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int rtl8153_unwrap(const uint8_t *data, size_t size, size_t *offset,
                   void *frame, size_t capacity)
{
    if (!data || !offset || !frame || *offset > size) return -1;
    while (*offset < size) {
        size_t remaining = size - *offset;
        if (remaining < 24) { *offset = size; return -1; }
        const uint8_t *desc = data + *offset;
        uint32_t opts = get_le32(desc);
        size_t wire = opts & 0x7fffu; /* Includes four FCS bytes. */
        if (wire < 18 || wire > remaining - 24) { *offset = size; return -1; }
        size_t padded = (wire + 7u) & ~(size_t)7u;
        /* Last frame may end without the final alignment bytes. */
        if (padded > remaining - 24 && wire != remaining - 24) {
            *offset = size; return -1;
        }
        *offset += 24 + (padded <= remaining - 24 ? padded : wire);
        if (opts & 0x8000u) continue; /* CRC error: consume and drop. */
        size_t length = wire - 4;
        if (length > capacity) continue; /* Jumbo/oversized frame. */
        memcpy(frame, desc + 24, length);
        return (int)length;
    }
    return 0;
}

int rtl8153_wrap(void *output, size_t capacity, const void *frame, size_t size)
{
    if (!output || !frame || size < 14 || size > 1518) return -1;
    /* Pad short Ethernet frames explicitly; never expose stale DMA bytes. */
    size_t length = size < 60 ? 60 : size;
    if (capacity < length + 8) return -1;
    uint8_t *p = output;
    uint32_t opts = (uint32_t)length | 0xc0000000u; /* First + last segment. */
    memset(p, 0, length + 8);
    p[0] = opts; p[1] = opts >> 8; p[2] = opts >> 16; p[3] = opts >> 24;
    memcpy(p + 8, frame, size);
    return (int)(length + 8);
}
