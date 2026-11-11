#ifndef RTL8153_NATIVE_FRAME_ADAPTER_H
#define RTL8153_NATIVE_FRAME_ADAPTER_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t offset,length; } RtlNativeFrame;
/* RX views reference the DMA aggregate; caller retains it through the native
 * input callback. No packet copies and no protocol parsing occur here. */
int rtl_native_rx_views(const void *aggregate,size_t bytes,RtlNativeFrame *frames,
                        size_t max_frames,size_t *count);
/* Native TX input can contain many Ethernet frames. Realtek headers must be
 * present for each frame and subsequent headers aligned to four bytes. */
int rtl_native_tx_batch(void *out,size_t capacity,const void *const *frames,
                        const size_t *lengths,size_t count,size_t *written);
#endif
