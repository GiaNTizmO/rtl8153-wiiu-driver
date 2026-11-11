#include "native_glue.h"
#include <string.h>

static int valid_host(const RtlIosNativeHost *h)
{
    return h && h->alloc && h->free && h->deliver_rx && h->report_link && h->complete_tx;
}
static void free_slot(RtlIosNativeDriver *d, unsigned i)
{
    RtlIosNativeSlot *s = &d->slots[i];
    /* Also handles partial allocation failures before allocated is set. */
    if (s->dma_ptr) d->host.free(d->host.user, s->dma_ptr, s->dma_address);
    if (s->batch_owned && s->batch_ptr)
        d->host.free(d->host.user, s->batch_ptr, s->batch_address);
    memset(s, 0, sizeof(*s));
}
static int alloc_rx(RtlIosNativeDriver *d, unsigned i)
{
    if (i >= 2) return -1;
    RtlIosNativeSlot *s = &d->slots[i];
    s->batch_owned = 1;
    if (d->host.alloc(d->host.user, sizeof(IosEthRxBatch), 64, &s->batch_ptr, &s->batch_address) ||
        d->host.alloc(d->host.user, 32768, 64, &s->dma_ptr, &s->dma_address)) {
        free_slot(d, i); return -1;
    }
    s->batch_bytes = sizeof(IosEthRxBatch); s->dma_bytes = 32768; s->allocated = 1;
    memset(s->batch_ptr, 0, s->batch_bytes);
    uint8_t *p = (uint8_t *)s->batch_ptr + offsetof(IosEthRxBatch, transfer_buffer);
    p[0] = s->dma_address >> 24; p[1] = s->dma_address >> 16;
    p[2] = s->dma_address >> 8; p[3] = s->dma_address;
    return 0;
}
static int submit_rx(RtlIosNativeDriver *d, unsigned i)
{
    RtlIosNativeSlot *s = &d->slots[i];
    if (!s->allocated || rtl_ios_owner_prepare(&d->owner, i, 1, s->batch_address, s->dma_address, 32768)) return -1;
    if (rtl_ios_owner_submit(&d->owner, i, &d->uhs, 32768)) {
        (void)rtl_ios_owner_release(&d->owner, i); return -1;
    }
    return 0;
}
int rtl_ios_native_open(RtlIosNativeDriver *d, const RtlIosUhsBinding *u, const RtlIosNativeHost *h)
{
    RtlIosTransport transport;
    if (!d || !u || !valid_host(h) || d->state != RTL_NATIVE_CLOSED || !u->interface_id) return -1;
    memset(d, 0, sizeof(*d)); d->state = RTL_NATIVE_OPENING; d->uhs = *u; d->host = *h;
    if (rtl_ios_owner_init(&d->owner, u->interface_id) || rtl_ios_uhs_transport(&d->uhs, &transport) ||
        rtl_ios_init(&d->chip, &transport) || rtl_ios_enable(&d->chip) || alloc_rx(d, 0) ||
        alloc_rx(d, 1) || submit_rx(d, 0) || submit_rx(d, 1)) {
        rtl_ios_owner_close(&d->owner);
        if (!rtl_ios_owner_drained(&d->owner)) { d->state = RTL_NATIVE_CLOSING; return -2; }
        (void)rtl_ios_disable(&d->chip);
        for (unsigned i = 0; i < 10; i++) free_slot(d, i);
        d->state = RTL_NATIVE_CLOSED; return -1;
    }
    d->state = RTL_NATIVE_RUNNING; return 0;
}
int rtl_ios_native_request_cancel(RtlIosNativeDriver *d)
{
    if (!d || d->state != RTL_NATIVE_CLOSING) return -1;
    if (d->cancel_requested) return 0;
    int pending = 0;
    for (unsigned i = 0; i < 10; ++i)
        if (d->owner.slots[i].state == RTL_SLOT_PENDING) pending = 1;
    if (!pending) return 0;
    int rc = rtl_ios_uhs_cancel(&d->uhs);
    if (rc < 0) return rc;
    d->cancel_requested = 1;
    return 0;
}
int rtl_ios_native_close(RtlIosNativeDriver *d)
{
    if (!d || d->state == RTL_NATIVE_CLOSED || d->state == RTL_NATIVE_OPENING) return -1;
    if (d->state == RTL_NATIVE_RUNNING) { d->state = RTL_NATIVE_CLOSING; rtl_ios_owner_close(&d->owner); }
    if (d->uhs.endpoint_admin) {
        int rc = rtl_ios_native_request_cancel(d);
        if (rc < 0) return rc;
    }
    if (!rtl_ios_owner_drained(&d->owner)) return -2;
    if (rtl_ios_disable(&d->chip)) return -1;
    for (unsigned i = 0; i < 10; i++) free_slot(d, i);
    d->state = RTL_NATIVE_CLOSED; return 0;
}
int rtl_ios_native_get_link(RtlIosNativeDriver *d, IosEthLink *out)
{
    unsigned mbps = 0; int duplex = 0;
    if (!d || !out || d->state != RTL_NATIVE_RUNNING) return -1;
    int link = rtl_ios_get_link(&d->chip, &mbps, &duplex);
    if (link < 0) return -1;
    memset(out, 0, sizeof(*out)); out->autoneg_complete = link ? 1u : 0u;
    out->speed_mbps = mbps; out->duplex = (uint8_t)!!duplex; return 0;
}
int rtl_ios_native_send_async(RtlIosNativeDriver *d, void *b, IosAddress a, size_t n)
{
    if (!d || !b || d->state != RTL_NATIVE_RUNNING || !a || (a & 3u) || !n) return -1;
    unsigned i;
    for (i = 2; i < 10; i++) if (!d->slots[i].allocated) break;
    if (i == 10) return -1;
    RtlIosNativeSlot *s = &d->slots[i];
    if (d->host.alloc(d->host.user, 16384, 64, &s->dma_ptr, &s->dma_address)) return -1;
    s->batch_ptr = b; s->batch_address = a; s->batch_bytes = n; s->dma_bytes = 16384; s->allocated = 1;
    size_t written = 0;
    if (rtl_ios_owner_prepare(&d->owner, i, 0, a, s->dma_address, 16384) ||
        rtl_ios_repack_tx(b, n, a, 8, s->dma_ptr, 16384, &written) ||
        rtl_ios_owner_submit(&d->owner, i, &d->uhs, (uint32_t)written)) {
        (void)rtl_ios_owner_release(&d->owner, i); free_slot(d, i); return -1;
    }
    return 0;
}
static int native_complete_ready(RtlIosNativeDriver *d, int i)
{
    RtlIosNativeSlot *s = &d->slots[i];
    RtlIosOwnedSlot *owned = &d->owner.slots[i];
    int status = owned->result;
    if (i >= 2) {
        d->host.complete_tx(d->host.user, s->batch_ptr, s->batch_address, status);
        (void)rtl_ios_owner_release(&d->owner, (unsigned)i); free_slot(d, (unsigned)i);
        return status < 0 ? -1 : 0;
    }
    if (d->state == RTL_NATIVE_CLOSING) {
        (void)rtl_ios_owner_release(&d->owner, (unsigned)i); free_slot(d, (unsigned)i); return 0;
    }
    if (status >= 0 && rtl_ios_publish_rx(s->batch_ptr, s->batch_bytes, s->dma_address,
                                        s->dma_ptr, s->dma_bytes, owned->completed) == 0) {
        if (rtl_ios_owner_deliver(&d->owner, (unsigned)i)) return -1;
        if (d->host.deliver_rx(d->host.user, s->batch_ptr, s->batch_address, owned->cookie) == 0)
            return 0; /* Stack owns the batch until release_rx, no USB resubmit. */
    }
    (void)rtl_ios_owner_release(&d->owner, (unsigned)i); free_slot(d, (unsigned)i); return -1;
}
int rtl_ios_native_complete(RtlIosNativeDriver *d, const RtlIosUhsCompletion *m, int result, uint32_t done)
{
    if (!d || !m || (d->state != RTL_NATIVE_RUNNING && d->state != RTL_NATIVE_CLOSING)) return -1;
    int i = rtl_ios_owner_complete(&d->owner, m, result, done);
    return i < 0 ? -1 : native_complete_ready(d, i);
}
int rtl_ios_native_complete_pair(RtlIosNativeDriver *d, unsigned kind,
                                 IosAddress batch, IosAddress dma,
                                 int result, uint32_t done)
{
    if (!d || (d->state != RTL_NATIVE_RUNNING && d->state != RTL_NATIVE_CLOSING)) return -1;
    int i = rtl_ios_owner_complete_pair(&d->owner, kind, batch, dma, result, done);
    return i < 0 ? -1 : native_complete_ready(d, i);
}
int rtl_ios_native_release_rx(RtlIosNativeDriver *d, IosAddress batch, uint32_t cookie)
{
    if (!d || !cookie || (d->state != RTL_NATIVE_RUNNING && d->state != RTL_NATIVE_CLOSING)) return -1;
    for (unsigned i = 0; i < 2; ++i) {
        RtlIosOwnedSlot *s = &d->owner.slots[i];
        if (s->state != RTL_SLOT_DELIVERED || s->batch != batch || s->cookie != cookie) continue;
        if (rtl_ios_owner_release(&d->owner, i)) return -1;
        if (d->state == RTL_NATIVE_CLOSING) { free_slot(d, i); return 0; }
        if (submit_rx(d, i) == 0) return 0;
        free_slot(d, i); return -1;
    }
    return -1;
}
int rtl_ios_native_post_link(RtlIosNativeDriver *d)
{
    IosEthLink l;
    if (!d || d->state != RTL_NATIVE_RUNNING || rtl_ios_native_get_link(d, &l)) return -1;
    return d->host.report_link(d->host.user, &l);
}
