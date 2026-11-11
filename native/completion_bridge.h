#ifndef RTL8153_IOS_COMPLETION_BRIDGE_H
#define RTL8153_IOS_COMPLETION_BRIDGE_H
#include <stdint.h>
#include "ios_net_abi.h"

/* The owner worker drops the UHS metadata cookie before calling ops+0x34.
 * This bridge therefore matches the callback ABI exactly and keeps a one-shot
 * lease for each batch/buffer pair. A retired pair cannot be reused until
 * controller quiescence has been established and reset() is called. */
enum RtlIosBridgeLeaseState { RTL_BRIDGE_FREE, RTL_BRIDGE_ARMED,
                              RTL_BRIDGE_COMPLETED, RTL_BRIDGE_RETIRED };
typedef struct {
    IosAddress batch, buffer;
    uint32_t kind, sequence;
    int result;
    unsigned state;
} RtlIosBridgeLease;
typedef struct {
    RtlIosBridgeLease leases[10];
    uint32_t next_sequence;
    int initialized;
} RtlIosCompletionBridge;
int rtl_ios_bridge_init(RtlIosCompletionBridge *bridge);
int rtl_ios_bridge_arm(RtlIosCompletionBridge *bridge, unsigned kind,
                       IosAddress batch, IosAddress buffer,
                       uint32_t *sequence_out);
/* Actual ops+0x34 boundary: no cookie or metadata pointer. */
int rtl_ios_bridge_complete(RtlIosCompletionBridge *bridge, unsigned kind,
                            IosAddress batch, IosAddress buffer, int result,
                            unsigned *lease_out);
int rtl_ios_bridge_release(RtlIosCompletionBridge *bridge, unsigned lease);
/* Retire only after failed submit with no accepted USB ownership, or after
 * an independent guarantee of no future callback. A failed or accepted cancel
 * request alone does NOT grant that guarantee. */
int rtl_ios_bridge_cancel(RtlIosCompletionBridge *bridge, unsigned lease);
/* Local drain is necessary but NOT sufficient: caller must separately prove
 * that no queued/late callback from the old controller can still arrive. */
int rtl_ios_bridge_reset(RtlIosCompletionBridge *bridge);
int rtl_ios_bridge_drained(const RtlIosCompletionBridge *bridge);
#endif
