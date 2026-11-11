#ifndef RTL8153_IOS_TRANSFER_OWNER_H
#define RTL8153_IOS_TRANSFER_OWNER_H
#include "uhs_transport.h"
/* Serialized by the native USB worker. This owns neither allocations nor IPC
 * messages: it prevents the adapter from recycling buffers while USB/native
 * input still owns them. Never reset a live owner to force shutdown. */
enum RtlIosSlotState { RTL_SLOT_FREE,RTL_SLOT_PREPARED,RTL_SLOT_PENDING,
                       RTL_SLOT_READY,RTL_SLOT_DELIVERED };
typedef struct {
    IosAddress batch,dma;
    uint32_t capacity,submitted,completed,cookie;
    int result;
    unsigned kind,state;
} RtlIosOwnedSlot;
typedef struct {
    RtlIosOwnedSlot slots[10]; /* two RX, eight TX, matching native limits */
    uint32_t interface_id;
    uint32_t next_cookie; /* no wrap: exhaustion refuses further submissions */
    int closing;
} RtlIosOwner;
int rtl_ios_owner_init(RtlIosOwner *owner,uint32_t interface_id);
int rtl_ios_owner_prepare(RtlIosOwner *owner,unsigned slot,int read,
                           IosAddress batch,IosAddress dma,uint32_t capacity);
/* Submit and state update are serialized against completion dispatch. */
int rtl_ios_owner_submit(RtlIosOwner *owner,unsigned slot,
                          RtlIosUhsBinding *binding,uint32_t bytes);
/* Caller passes metadata returned by firmware and its actual result/length.
 * Completion must be consumed once. Cancel is complete only after this path
 * acknowledges every pending USB request; requesting cancel is insufficient. */
int rtl_ios_owner_complete(RtlIosOwner *owner,const RtlIosUhsCompletion *meta,
                            int result,uint32_t completed);
/* Same callback tuple as stock ops+0x34. The caller must have separately
 * established that this batch/buffer pair is the currently armed request. */
int rtl_ios_owner_complete_pair(RtlIosOwner *owner,unsigned kind,
                                IosAddress batch,IosAddress dma,int result,
                                uint32_t completed);
int rtl_ios_owner_deliver(RtlIosOwner *owner,unsigned slot);
int rtl_ios_owner_release(RtlIosOwner *owner,unsigned slot);
void rtl_ios_owner_close(RtlIosOwner *owner);
int rtl_ios_owner_drained(const RtlIosOwner *owner);
#endif
