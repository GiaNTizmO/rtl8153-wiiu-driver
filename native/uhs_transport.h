#ifndef RTL8153_IOS_UHS_TRANSPORT_H
#define RTL8153_IOS_UHS_TRANSPORT_H
#include "rtl8153_ios_core.h"
#include "ios_net_abi.h"

/* Recovered native UHS ABI; these function pointers execute ONLY in IOS-NET.
 * Binding addresses is the loader's responsibility after profile validation.
 * No PPC caller may invoke this API. No binding/installation happens here. */
typedef struct {
    uint32_t kind,interface_id;
    IosAddress batch,buffer;
    /* UHS copies this word, but the stock ops+0x34 callback does NOT receive
     * it or a metadata pointer. This record is an internal ownership API;
     * a firmware adapter must preserve request identity separately. */
    uint32_t cookie;
} RtlIosUhsCompletion;
_Static_assert(sizeof(RtlIosUhsCompletion)==20,"UHS copied completion metadata");
typedef int (*RtlIosControlSync)(void *uhs,uint32_t interface_handle,
    uint8_t request,uint8_t request_type,uint16_t value,uint16_t index,
    void *data,uint16_t bytes,uint32_t timeout_us);
typedef int (*RtlIosBulkAsync)(void *uhs,uint32_t interface_handle,
    uint8_t endpoint,unsigned direction,void *data,uint32_t bytes,
    const RtlIosUhsCompletion *metadata,int completion_queue);
/* Native helper 123baee4, observed at the ASIX close callsite. The final
 * two words are zero for endpoint cancellation. This is not the PPC enum. */
typedef int (*RtlIosEndpointAdmin)(void *uhs,uint32_t interface_handle,
    uint32_t operation,uint32_t endpoint_mask,uint32_t arg4,uint32_t arg5);

typedef struct {
    void *uhs;
    uint32_t interface_handle,interface_id;
    uint32_t completion_cookie;
    int completion_queue;
    uint8_t bulk_in,bulk_out; /* full USB endpoint addresses, not array indices */
    RtlIosControlSync control_sync;
    RtlIosBulkAsync bulk_async;
    void *user;
    void (*sleep_ms)(void *user,unsigned ms);
    int (*cancelled)(void *user);
    RtlIosEndpointAdmin endpoint_admin;
} RtlIosUhsBinding;

int rtl_ios_uhs_transport(RtlIosUhsBinding *binding,RtlIosTransport *out);
/* Submit caller-owned, 64-byte aligned DMA memory. On success, completion
 * will arrive through the firmware worker; caller increments pending exactly
 * once and retains both batch and DMA until completion/cancel acknowledgement.
 * No pending counters are changed here. metadata is copied by native UHS. */
int rtl_ios_uhs_submit(RtlIosUhsBinding *binding,int read,IosAddress batch,
                       IosAddress dma,uint32_t bytes);
/* Request cancellation of this binding's bulk endpoints. A zero return only
 * accepts the request; buffers/pending counters remain owned until callback.
 * Allowed even when cancelled(user) is true, so shutdown can make progress. */
int rtl_ios_uhs_cancel(RtlIosUhsBinding *binding);
#endif
