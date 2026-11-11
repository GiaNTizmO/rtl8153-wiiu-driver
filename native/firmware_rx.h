#ifndef RTL8153_FIRMWARE_RX_H
#define RTL8153_FIRMWARE_RX_H
#include "batch_bridge.h"
#include "uhs_transport.h"

/* Native worker only, serialized/non-reentrant. The receive_async caller owns
 * batch+inline buffer before submission. On failure it still owns both. After
 * accepted submission this adapter borrows them until completion, then hands
 * ownership to rx_done (normally 123b8174), or discard during close. The
 * firmware/native stack owns subsequent release; this adapter never retains
 * or frees a batch after either handoff and never resubmits it itself.
 * All callbacks are unbound: loader/profile/cache validation is still needed. */
typedef struct {
    void *user;
    int (*alloc_dma)(void*,uint32_t,void**,IosAddress*);
    void (*free_dma)(void*,void*,IosAddress);
    void (*pending)(void*,int delta); /* real controller RX count, exactly once */
    /* rx_done may refill RX before returning, but cannot synchronously invoke
     * a USB completion on this same worker. Final drain may destroy state. */
    void (*rx_done)(void*,void *batch,IosAddress,uint32_t pacing);
    void (*discard)(void*,void *batch,IosAddress);
    void (*drain)(void*); /* last action; may destroy the controller */
} RtlIosFirmwareRxHost;
typedef struct {
    void *batch,*inline_buffer,*dma;
    IosAddress batch_address,inline_address,dma_address;
    uint32_t capacity,cookie,allocation;
    int pending;
} RtlIosFirmwareRxSlot;
typedef struct {
    RtlIosUhsBinding binding;
    RtlIosFirmwareRxHost host;
    RtlIosFirmwareRxSlot slots[2];
    uint32_t next_cookie;
    int closing,busy,needs_drain;
    void *completed_batch;
    IosAddress completed_address;
    int completed_closing;
} RtlIosFirmwareRx;
/* New/quiescent object only. binding may be a token-wrapped UHS binding. */
int rtl_ios_firmware_rx_init(RtlIosFirmwareRx*,const RtlIosUhsBinding*,
                            const RtlIosFirmwareRxHost*);
/* allocation covers the original firmware batch and inline storage. inline_ptr
 * is the host-visible view of the batch's +444 address, not arbitrary memory.
 * capacity must equal the registered RTL RX capacity (16 KiB for this stage). */
int rtl_ios_firmware_rx_submit(RtlIosFirmwareRx*,void *batch,
                               IosAddress batch_address,uint32_t allocation,
                               void *inline_ptr,uint32_t capacity);
/* Signature matches token delivery. Native RX publication owns the original
 * batch; bounce DMA is freed before rx_done/discard. Native delivery is
 * deferred to finish so a firmware pacing/refill call can submit another RX.
 * finish must run after
 * the token wrapper has cleared its busy state, as its after_delivery hook. */
int rtl_ios_firmware_rx_complete(void*,const RtlIosUhsCompletion*,int,uint32_t);
void rtl_ios_firmware_rx_finish(void*);
int rtl_ios_firmware_rx_close(RtlIosFirmwareRx*);
int rtl_ios_firmware_rx_drained(const RtlIosFirmwareRx*);
/* Exact receive_async(context,controller,batch) ABI. Geometry is pinned to
 * ops.rx_capacity=0x4000, ops.reserved18=0; no console binding is automatic.
 * Keep attachment until firmware can no longer invoke its callback. */
int rtl_ios_firmware_rx_attach(RtlIosFirmwareRx*,IosAddress context,IosAddress controller);
int rtl_ios_firmware_rx_detach(RtlIosFirmwareRx*);
int rtl_ios_firmware_receive_async(IosAddress context,IosAddress controller,void *batch);
#endif
