#ifndef RTL8153_IOS_COMPLETION_ADAPTER_H
#define RTL8153_IOS_COMPLETION_ADAPTER_H
#include "completion_bridge.h"

/* Legacy one-shot tuple validator for offline experiments. This API includes
 * an adapter pointer and lease output and is NOT itself the six-argument
 * ops+0x34 ABI. For sustained reuse, completion_tokens supplies the real entry.
 * Firmware omits metadata/cookie but does pass context/controller/kind/batch
 * plus two stack words for buffer and signed result. */
typedef int (*RtlIosCompletionDispatch)(void *user, unsigned kind,
                                        IosAddress batch, IosAddress buffer,
                                        int result, unsigned lease);

typedef struct {
    RtlIosCompletionBridge bridge;
    IosAddress context, controller;
    RtlIosCompletionDispatch dispatch;
    void *user;
    int bound;
} RtlIosCompletionAdapter;

int rtl_ios_completion_adapter_init(RtlIosCompletionAdapter *adapter,
                                    RtlIosCompletionDispatch dispatch,
                                    void *user);
int rtl_ios_completion_adapter_bind(RtlIosCompletionAdapter *adapter,
                                    IosAddress context, IosAddress controller);
int rtl_ios_completion_adapter_arm(RtlIosCompletionAdapter *adapter,
                                   unsigned kind, IosAddress batch,
                                   IosAddress buffer, uint32_t *sequence);
/* Helper only; must not be installed directly into the firmware ops table. */
int rtl_ios_completion_adapter_callback(RtlIosCompletionAdapter *adapter,
                                        IosAddress context, IosAddress controller,
                                        unsigned kind, IosAddress batch,
                                        IosAddress buffer, int result,
                                        unsigned *lease_out);
int rtl_ios_completion_adapter_cancel(RtlIosCompletionAdapter *adapter,
                                      unsigned lease);
int rtl_ios_completion_adapter_release(RtlIosCompletionAdapter *adapter,
                                       unsigned lease);
int rtl_ios_completion_adapter_reset(RtlIosCompletionAdapter *adapter);
#endif
