#ifndef RTL8153_NATIVE_GLUE_H
#define RTL8153_NATIVE_GLUE_H
#include "batch_bridge.h"
#include "rtl8153_ios_core.h"
#include "transfer_owner.h"
enum RtlIosNativeDriverState { RTL_NATIVE_CLOSED=0, RTL_NATIVE_OPENING, RTL_NATIVE_RUNNING, RTL_NATIVE_CLOSING };
typedef int (*RtlIosNativeAlloc)(void*,size_t,size_t,void**,IosAddress*);
typedef void (*RtlIosNativeFree)(void*,void*,IosAddress);
typedef int (*RtlIosNativeRx)(void*,void*,IosAddress,uint32_t);
typedef int (*RtlIosNativeLink)(void*,const IosEthLink*);
/* Successful delivery retains RX memory until release_rx. Serialized worker
 * callbacks must not re-enter the driver. TX returns the original batch. */
typedef void (*RtlIosNativeTxDone)(void*,void*,IosAddress,int);
typedef struct { void *user; RtlIosNativeAlloc alloc; RtlIosNativeFree free; RtlIosNativeRx deliver_rx; RtlIosNativeLink report_link; RtlIosNativeTxDone complete_tx; } RtlIosNativeHost;
typedef struct { void *batch_ptr; IosAddress batch_address; size_t batch_bytes; void *dma_ptr; IosAddress dma_address; size_t dma_bytes; int batch_owned; int allocated; } RtlIosNativeSlot;
typedef struct { enum RtlIosNativeDriverState state; RtlIosDevice chip; RtlIosOwner owner; RtlIosUhsBinding uhs; RtlIosNativeHost host; RtlIosNativeSlot slots[10]; int cancel_requested; } RtlIosNativeDriver;
int rtl_ios_native_open(RtlIosNativeDriver*,const RtlIosUhsBinding*,const RtlIosNativeHost*);
int rtl_ios_native_close(RtlIosNativeDriver*);
/* Closing-only; request cancellation without retiring pending slots. Failed
 * requests may be retried by the caller; successful requests are not repeated.
 * Transport/host callbacks are serialized and must not reenter the driver. */
int rtl_ios_native_request_cancel(RtlIosNativeDriver*);
int rtl_ios_native_get_link(RtlIosNativeDriver*,IosEthLink*);
int rtl_ios_native_send_async(RtlIosNativeDriver*,void*,IosAddress,size_t);
/* Internal completion API, not the firmware ops+0x34 ABI. Stock dispatch
 * forwards batch/buffer/signed result but drops the metadata cookie. */
int rtl_ios_native_complete(RtlIosNativeDriver*,const RtlIosUhsCompletion*,int,uint32_t);
/* Pair helper only, NOT the six-argument firmware ABI: it lacks context and
 * controller and takes split status/length. External identity validation is
 * mandatory before calling. Sustained firmware integration uses tokens. */
int rtl_ios_native_complete_pair(RtlIosNativeDriver*,unsigned,IosAddress,
                                 IosAddress,int,uint32_t);
int rtl_ios_native_release_rx(RtlIosNativeDriver*,IosAddress,uint32_t);
int rtl_ios_native_post_link(RtlIosNativeDriver*);
#endif
