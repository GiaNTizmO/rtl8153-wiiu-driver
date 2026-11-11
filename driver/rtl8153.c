#include "rtl8153.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#if defined(RTL8153_HOST_TEST) || defined(RTL8153_MOCK_UHS)
#include "../tests/uhs_mock.h"
#else
#include <coreinit/cache.h>
#include <coreinit/event.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <nsysuhs/uhs.h>
#include <whb/log.h>
#include "../aroma_module/rtl_log.h"
#include "../aroma_module/iosu_patch.h"
#define WHBLogPrintf rtl_log
#endif

/*
 * The direction argument is 1 or 2, and neither is zero.
 *
 * Read out of nsysuhs.rpl rather than assumed. UhsSubmitBulkRequest
 * builds an ioctlv and counts its vectors from the direction:
 *
 *     param_4 == 1  ->  2 in, 0 out   the buffer is SENT      (write)
 *     param_4 == 2  ->  1 in, 1 out   the buffer is FILLED    (read)
 *     anything else ->  neither branch runs, and the call goes out
 *                       malformed
 *
 * The obvious guess -- 0 for out, 1 for in -- is wrong twice over: it
 * asks to WRITE to an IN endpoint, and its "out" is a request with no
 * vectors at all. Control transfers were unaffected because they take
 * no direction argument; theirs comes from bmRequestType, which is why
 * they were the one thing that always worked.
 */
#define UHS_DIR_OUT 1
#define UHS_DIR_IN  2

#if !defined(RTL8153_HOST_TEST) && !defined(RTL8153_MOCK_UHS)
/*
 * Exported by nsysuhs.rpl but currently missing from WUT's public uhs.h.
 * ABI verified against existing Wii U UHS users.
 */
typedef void (*AxUhsBulkAsyncCallback)(
    void *user,
    int ret,
    uint32_t if_handle,
    uint8_t endpoint,
    int direction,
    const void *buffer,
    size_t actual);

extern int32_t UhsSubmitBulkRequestAsync(
    UhsHandle *handle,
    uint32_t if_handle,
    uint8_t endpoint,
    int32_t direction,
    const void *buffer,
    size_t length,
    void *user,
    AxUhsBulkAsyncCallback callback);
#endif

#define REALTEK_VID 0x0bda
#define RTL8153_PID 0x8153
#define REQ_IN   0xC0
#define REQ_OUT  0x40

#define UHS_WORK_SIZE   (128 * 1024)
#define MAX_IFACES      16
/* The bulk IN carries several frames at once, headers and all. */
#define RX_BUFFER_SIZE  (32 * 1024)
#define RX_ASYNC_SLOTS  3
/* UHS's answer when a bulk IN had nothing to hand back. */
#define AX_UHS_NO_DATA (-2162747)

struct Rtl8153 {
    UhsHandle handle;
    UhsConfig config;
    uint16_t tx_maxpacket;
    uint16_t medium;
    uint16_t chip;
    int chip_b, io_error;
    uint32_t enabled_eps;
    uint32_t ep_in_mask, ep_out_mask;
    int32_t last_control;
    uint8_t last_cmd;
    uint16_t last_value, last_index, last_size;
    uint8_t last_read;
    uint32_t  if_handle;
    uint8_t   ep_in;      /* bulk, frames from the wire */
    uint8_t   ep_out;     /* bulk, frames to it */
    uint8_t   ep_irq;     /* interrupt, link status */
    uint8_t   mac[RTL8153_MAC_LEN];

    /* What is left of the last bulk transfer, still to be unwrapped. */
    int       rx_len;
    int       rx_pos;         /* how far into the frames we have walked */

    unsigned  rx_consume;
    int       rx_async_started;

    int32_t   last_bulk;      /* what the last bulk request returned */
    /* How long the last failing bulk IN took. A refusal comes back at
     * once; a timeout takes the timeout. That is the whole difference
     * between "the adapter will not talk to us" and "nothing arrived",
     * and the error code alone does not say which. */
    uint32_t  last_bulk_us;
};

/* UHS DMAs into these, so they are aligned and static rather than on a
 * stack that moves. */
static uint8_t g_uhs_work[UHS_WORK_SIZE] __attribute__((aligned(0x40)));
static uint8_t g_ctrl[64] __attribute__((aligned(0x40)));

/*
 * g_rx remains the parser-owned snapshot. UHS itself DMA-writes into the
 * three ring buffers so a new transfer can already be pending while the
 * previous aggregate is being parsed.
 */
static uint8_t g_rx[RX_BUFFER_SIZE]
    __attribute__((aligned(0x40)));

static uint8_t g_rx_async[RX_ASYNC_SLOTS][RX_BUFFER_SIZE]
    __attribute__((aligned(0x40)));

enum {
    AX_RX_SLOT_IDLE = 0,
    AX_RX_SLOT_PENDING,
    AX_RX_SLOT_DONE
};

struct AxRxAsyncSlot {
    atomic_int state;
    int32_t ret;
    uint32_t actual;
    uint8_t *buffer;
};

static struct AxRxAsyncSlot g_rx_slots[RX_ASYNC_SLOTS];

/*
 * Wake the network worker as soon as any asynchronous UHS RX transfer
 * completes. Auto-reset is sufficient because one wake causes the
 * worker to drain every completed slot currently available.
 */
static OSEvent g_rx_event;

static uint8_t g_tx[2048] __attribute__((aligned(0x40)));
static UhsInterfaceProfile g_profiles[MAX_IFACES] __attribute__((aligned(0x40)));
/* Static DMA buffers: one instance, called serially by its owner thread. */
static int g_open;
static int (*g_cancel_check)(void);
void rtl8153_set_cancel_check(int (*check)(void)) { g_cancel_check = check; }
static int rtl_cancelled(void) { return g_cancel_check && g_cancel_check(); }

void
rtl8153_force_cold_next_open(void)
{
    /*
     * A physically reattached RTL8153 is a freshly reset chip, not the
     * warm device from the previous title transition.
     */
    /* Every open uses the complete RTL8153 initialization sequence. */
}

#define PHY_LOG(fmt, ...) \
    WHBLogPrintf("[%llu] PHY: " fmt, \
        (unsigned long long)OSTicksToMilliseconds(OSGetTime()), ##__VA_ARGS__)

/* The chip speaks little-endian and this console does not. */
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}


#ifndef RTL8153_HOST_TEST
static void
ax_rx_async_callback(
    void *user,
    int ret,
    uint32_t if_handle,
    uint8_t endpoint,
    int direction,
    const void *buffer,
    size_t actual)
{
    (void)if_handle;
    (void)endpoint;
    (void)direction;

    struct AxRxAsyncSlot *slot =
        (struct AxRxAsyncSlot *)user;

    if (!slot || buffer != slot->buffer) {
        return;
    }

    slot->ret = ret;
    slot->actual = (uint32_t)actual;

    atomic_store_explicit(
        &slot->state,
        AX_RX_SLOT_DONE,
        memory_order_release);

    /*
     * Wake the UHS owner immediately instead of relying on periodic
     * polling. If nobody is currently waiting, the auto event remains
     * signalled until the next wait.
     */
    OSSignalEvent(&g_rx_event);
}

static int
ax_rx_submit_slot(
    Rtl8153 *ax,
    unsigned index)
{
    struct AxRxAsyncSlot *slot =
        &g_rx_slots[index];

    DCFlushRange(
        slot->buffer,
        RX_BUFFER_SIZE);

    slot->ret = 0;
    slot->actual = 0;

    atomic_store_explicit(
        &slot->state,
        AX_RX_SLOT_PENDING,
        memory_order_release);

    int32_t rc =
        UhsSubmitBulkRequestAsync(
            &ax->handle,
            ax->if_handle,
            ax->ep_in,
            UHS_DIR_IN,
            slot->buffer,
            RX_BUFFER_SIZE,
            slot,
            ax_rx_async_callback);

    if (rc != 0) {
        atomic_store_explicit(
            &slot->state,
            AX_RX_SLOT_IDLE,
            memory_order_release);

        ax->last_bulk = rc;
        ax->io_error = 1; /* Reopen instead of reusing partially queued DMA. */
        return -1;
    }

    return 0;
}

static int
ax_rx_start_async(
    Rtl8153 *ax)
{
    ax->rx_consume = 0;

    OSInitEvent(
        &g_rx_event,
        FALSE,
        OS_EVENT_MODE_AUTO);

    ax->rx_async_started = 1;

    for (unsigned i = 0;
         i < RX_ASYNC_SLOTS;
         i++) {

        g_rx_slots[i].buffer =
            g_rx_async[i];

        g_rx_slots[i].ret = 0;
        g_rx_slots[i].actual = 0;

        atomic_store_explicit(
            &g_rx_slots[i].state,
            AX_RX_SLOT_IDLE,
            memory_order_release);
    }

    for (unsigned i = 0;
         i < RX_ASYNC_SLOTS;
         i++) {

        if (ax_rx_submit_slot(ax, i) != 0) {
            return -1;
        }
    }

    return 0;
}

/*
 * Copy the next completed USB aggregate into parser-owned g_rx and
 * immediately re-arm its DMA slot.
 *
 * Consume slots strictly in submission order (0,1,2,0,1,2...) so packet
 * ordering is preserved even if several callbacks have already fired.
 *
 * Returns:
 *   1 aggregate loaded
 *   0 next aggregate not complete
 *  -1 completed transfer was invalid/error
 */
static int
ax_rx_take_completed(
    Rtl8153 *ax)
{
    unsigned index =
        ax->rx_consume;

    struct AxRxAsyncSlot *slot =
        &g_rx_slots[index];

    if (atomic_load_explicit(
            &slot->state,
            memory_order_acquire) !=
        AX_RX_SLOT_DONE) {
        return 0;
    }

    int32_t ret = slot->ret;
    uint32_t actual = slot->actual;

    ax->rx_consume =
        (index + 1) % RX_ASYNC_SLOTS;

    ax->last_bulk = ret;
    ax->last_bulk_us = 0;

    if (ret < 0 ||
        actual < 24 ||
        actual > RX_BUFFER_SIZE) {
        if (ret < 0) ax->io_error = 1;

        atomic_store_explicit(
            &slot->state,
            AX_RX_SLOT_IDLE,
            memory_order_release);

        /* Keep the ring alive even after one failed transfer. */
        ax_rx_submit_slot(ax, index);

        return ret < 0 || actual != 0
            ? -1
            : 0;
    }

    DCInvalidateRange(
        slot->buffer,
        actual);

    memcpy(
        g_rx,
        slot->buffer,
        actual);

    /*
     * The DMA slot is no longer needed by the parser: re-arm it before
     * doing any lwIP work on the copied aggregate.
     */
    atomic_store_explicit(
        &slot->state,
        AX_RX_SLOT_IDLE,
        memory_order_release);

    if (ax_rx_submit_slot(ax, index) != 0) {
        /* The copied aggregate can be consumed; io_error forces a reopen
         * on the next PHY poll instead of silently losing a DMA slot. */
    }

    ax->rx_len = (int)actual;

    ax->rx_pos = 0;

    return 1;
}
#endif

static void sleep_ms(int ms)
{
    if (rtl_cancelled()) return;
    OSSleepTicks(OSMillisecondsToTicks((uint32_t)ms));
}

/* --- register access -------------------------------------------------- */

static int control(Rtl8153 *ax, int read, uint8_t cmd, uint16_t value,
                   uint16_t index, uint16_t size, void *data)
{
    if (!ax || rtl_cancelled() || !data || !size || size > sizeof(g_ctrl)) return -1;
    memset(g_ctrl, 0, sizeof(g_ctrl));
    if (!read) memcpy(g_ctrl, data, size);
    DCFlushRange(g_ctrl, sizeof(g_ctrl));
    ax->last_cmd = cmd;
    ax->last_value = value;
    ax->last_index = index;
    ax->last_size = size;
    ax->last_read = read;
    int32_t r = (int32_t)UhsSubmitControlRequest(&ax->handle, ax->if_handle,
        g_ctrl, cmd, read ? REQ_IN : REQ_OUT, value, index, size, 1000);
    ax->last_control = r;
    if (r < 0) return -1;
    /* UHS synchronous transfers return the transferred byte count. */
    if (r != size) return -1;
    if (read) {
        DCInvalidateRange(g_ctrl, sizeof(g_ctrl));
        memcpy(data, g_ctrl, size);
    }
    return 0;
}

#include "rtl8153_chip.inc"

/* --- finding and opening ---------------------------------------------- */

/*
 * The endpoints, taken from the interface descriptor rather than
 * assumed.
 *
 * The first probes read in_endpoints[0] and [1] and got different
 * answers from a filtered query than from MATCH_ANY -- the array is
 * sparse, not packed. So this walks it and sorts by what each endpoint
 * IS: bulk in, bulk out, interrupt in. A bulk transfer to the wrong
 * endpoint fails silently, which is the worst way for this to be wrong.
 */
static int find_endpoints(Rtl8153 *ax, const UhsInterfaceProfile *p)
{
    ax->ep_in = ax->ep_out = ax->ep_irq = 0;
    for (int i = 0; i < 16; i++) {
        const UhsEndpointDescriptor *in = &p->in_endpoints[i];
        if (in->bLength && (in->bEndpointAddress & 0x80)) {
            const uint8_t kind = in->bmAttributes & 0x03;
            if (kind == 0x02 && !ax->ep_in) {
                ax->ep_in = in->bEndpointAddress & 0x0F;
                ax->ep_in_mask = 1u << (16 + ax->ep_in);
            } else if (kind == 0x03 && !ax->ep_irq) {
                ax->ep_irq = in->bEndpointAddress & 0x0F;
            }
        }
        const UhsEndpointDescriptor *out = &p->out_endpoints[i];
        if (out->bLength && !(out->bEndpointAddress & 0x80)) {
            if ((out->bmAttributes & 0x03) == 0x02 && !ax->ep_out) {
                ax->ep_out = out->bEndpointAddress & 0x0F;
                ax->ep_out_mask = 1u << ax->ep_out;
                /* Accept native or raw USB byte order; legal bulk sizes
                 * for this device are unambiguous in either representation. */
                uint16_t mps = out->wMaxPacketSize;
                if (mps != 64 && mps != 512 && mps != 1024)
                    mps = (uint16_t)((mps >> 8) | (mps << 8));
                if (mps == 64 || mps == 512 || mps == 1024)
                    ax->tx_maxpacket = mps;
            }
        }
    }
    return (ax->ep_in && ax->ep_out && ax->tx_maxpacket) ? 0 : -1;
}

Rtl8153 *rtl8153_open(char *why, unsigned why_size)
{
    if (g_open) {
        snprintf(why, why_size, "driver already open (single instance)");
        return NULL;
    }
    Rtl8153 *ax = calloc(1, sizeof(*ax));
    if (!ax) {
        snprintf(why, why_size, "out of memory");
        return NULL;
    }

    ax->config.controller_num = 0;
    ax->config.buffer = g_uhs_work;
    ax->config.buffer_size = sizeof(g_uhs_work);
    if ((int32_t)UhsClientOpen(&ax->handle, &ax->config) < 0) {
        snprintf(why, why_size, "cannot open /dev/uhs");
        free(ax);
        return NULL;
    }

    UhsInterfaceFilter filter;
    memset(&filter, 0, sizeof(filter));
    filter.match_params = MATCH_DEV_VID | MATCH_DEV_PID;
    filter.vid = REALTEK_VID;
    filter.pid = RTL8153_PID;

    const int32_t found = UhsQueryInterfaces(&ax->handle, &filter, g_profiles, MAX_IFACES);
    if (found <= 0) {
        snprintf(why, why_size, "no RTL8153 on the bus");
        UhsClientClose(&ax->handle);
        free(ax);
        return NULL;
    }

    int selected = -1;
    for (int i = 0; i < found && i < MAX_IFACES; ++i) {
        ax->ep_in_mask = ax->ep_out_mask = 0;
        ax->tx_maxpacket = 0;
        if (find_endpoints(ax, &g_profiles[i]) == 0) { selected = i; break; }
    }
    if (selected < 0) {
        snprintf(why, why_size, "no bulk endpoint pair on the interface");
        UhsClientClose(&ax->handle);
        free(ax);
        return NULL;
    }
    ax->if_handle = g_profiles[selected].if_handle;

    if ((int32_t)UhsAcquireInterface(&ax->handle, ax->if_handle, NULL, NULL) < 0) {
        snprintf(why, why_size, "the interface was refused");
        UhsClientClose(&ax->handle);
        free(ax);
        return NULL;
    }

    g_open = 1;
    const char *stage = "RTL8153 chip initialization";
#define CHECK(call) do { if ((call) != 0) goto fail; } while (0)
    CHECK(rtl_chip_init(ax));
    char patch_why[160];
    stage = "guarded IOSU endpoint patch";
    int patch_rc = iosu_patch_apply(patch_why, sizeof(patch_why));
    WHBLogPrintf("RTL8153 IOSU patch: %s", patch_why);
    CHECK(patch_rc);
    /* UHS endpoint mask: OUT in bits 0..15, IN in bits 16..31,
     * matching wut UHSEndpointGetMask. Bulk direction is a separate argument. */
    uint32_t ep_mask = ax->ep_in_mask;
    stage = "enable bulk IN";
    int32_t ep_result = (int32_t)UhsAdministerEndpoint(&ax->handle, ax->if_handle,
        UHS_ADMIN_EP_ENABLE, ep_mask, RX_ASYNC_SLOTS, RX_BUFFER_SIZE);
    if (ep_result < 0) goto ep_fail;
    ax->enabled_eps |= ep_mask;
    stage = "enable bulk OUT";
    ep_mask = ax->ep_out_mask;
    ep_result = (int32_t)UhsAdministerEndpoint(&ax->handle, ax->if_handle,
        UHS_ADMIN_EP_ENABLE, ep_mask, 1, sizeof(g_tx));
    if (ep_result < 0) goto ep_fail;
    ax->enabled_eps |= ep_mask;
    stage = "start RX";
    /* RX/TX are enabled after PHY link is established. */
#undef CHECK
    return ax;
ep_fail:
    snprintf(why, why_size, "%s mask=%08lx: UHS %ld", stage, (unsigned long)ep_mask, (long)ep_result);
    rtl8153_close(ax);
    return NULL;
fail:
    snprintf(why, why_size, "%s: %s cmd=%02x value=%04x index=%04x len=%u UHS=%ld",
        stage, ax->last_read ? "IN" : "OUT", ax->last_cmd, ax->last_value,
        ax->last_index, ax->last_size, (long)ax->last_control);
    rtl8153_close(ax);
    return NULL;
}

void rtl8153_close(Rtl8153 *ax)
{
    if (!ax) {
        return;
    }
    /* Gate RX before cancelling the UHS ring. */
    ure_write_1(ax, URE_PLA_CR, URE_MCU_TYPE_PLA, 0);

#ifndef RTL8153_HOST_TEST
    ax->rx_async_started = 0;

    if (ax->ep_in_mask) {
        UhsAdministerEndpoint(
            &ax->handle,
            ax->if_handle,
            UHS_ADMIN_EP_CANCEL,
            ax->ep_in_mask,
            0,
            0);
    }
#endif

    if (ax->enabled_eps)
        UhsAdministerEndpoint(&ax->handle, ax->if_handle, UHS_ADMIN_EP_DISABLE,
                              ax->enabled_eps, 0, 0);
    UhsReleaseInterface(&ax->handle, ax->if_handle, false);
    UhsClientClose(&ax->handle);
    free(ax);
    g_open = 0;
}

void rtl8153_abandon_title(Rtl8153 *ax)
{
    if (!ax)
        return;

    /*
     * No UHS call here.
     *
     * We are already inside the outgoing title's __PPCExit path. IOSU owns
     * the kernel-side lifetime of this process' /dev/uhs client and will
     * reclaim it when the process disappears.
     *
     * Calling UhsAdministerEndpoint/UhsReleaseInterface/UhsClientClose here
     * is unsafe because that teardown may already be in progress.
     */
    ax->rx_async_started = 0;

    free(ax);

    /*
     * This flag is module-resident, so reset it explicitly for the next
     * title. The next rtl8153_open() will create a brand-new UHS client.
     */
    g_open = 0;
}

const uint8_t *rtl8153_mac(const Rtl8153 *ax)
{
    return ax ? ax->mac : NULL;
}

#include "rtl8153_packets.inc"

int
rtl8153_receive(
    Rtl8153 *ax,
    void *frame,
    int max_length,
    int timeout_us)
{
    if (!ax || ax->io_error || !frame ||
        max_length < 14) {
        return -1;
    }

    if (!ax->medium) {
        return 0;
    }

    /*
     * Finish the parser-owned aggregate first.
     */
    int n =
        rtl8153_receive_buffered_impl(
            ax,
            frame,
            max_length);

    if (n != 0) {
        return n;
    }

#ifdef RTL8153_HOST_TEST
    return 0;
#else
    /*
     * First active receive starts the asynchronous UHS ring.
     *
     * The USB transfers themselves remain permanently asynchronous.
     * timeout_us only controls how long the owner thread may sleep while
     * waiting for a completion event.
     */
    if (!ax->rx_async_started) {
        if (ax_rx_start_async(ax) != 0) {
            return -1;
        }
    }

    int loaded =
        ax_rx_take_completed(ax);

    /*
     * Nothing completed yet. Sleep until a UHS callback signals the
     * auto-reset event or until the caller's timeout expires.
     *
     * There is no lost-wakeup race here: if the callback fires between
     * ax_rx_take_completed() and OSWaitEventWithTimeout(), the event is
     * already signalled and the wait returns immediately.
     */
    if (loaded == 0 && timeout_us > 0) {
        OSWaitEventWithTimeout(
            &g_rx_event,
            OSMicrosecondsToTicks(
                (uint64_t)timeout_us));

        loaded =
            ax_rx_take_completed(ax);
    }

    if (loaded <= 0) {
        return loaded;
    }

    return rtl8153_receive_buffered_impl(
        ax,
        frame,
        max_length);
#endif
}

int32_t rtl8153_last_bulk(const Rtl8153 *ax)
{
    return ax ? ax->last_bulk : 0;
}

uint32_t rtl8153_last_bulk_us(const Rtl8153 *ax)
{
    return ax ? ax->last_bulk_us : 0;
}

void rtl8153_endpoints(const Rtl8153 *ax, int *in, int *out, int *irq)
{
    if (!ax) {
        return;
    }
    if (in)  *in  = ax->ep_in;
    if (out) *out = ax->ep_out;
    if (irq) *irq = ax->ep_irq;
}
