#ifndef RTL8153_NATIVE_BATCH_BRIDGE_H
#define RTL8153_NATIVE_BATCH_BRIDGE_H
#include <stddef.h>
#include <stdint.h>

/* Raw big-endian IOS memory, usable on ARM and in host tests. These helpers
 * never invoke a firmware address, allocate memory, or transfer ownership.
 * The caller supplies allocations already owned by the native USB worker. */

/* Repack native TX's two-byte-aligned inline records into Realtek USB format.
 * ios_address is the base address corresponding to allocation[0], not a host
 * pointer. Each vector must name exactly its expected inline record; arbitrary
 * pointer dereferences and frames outside this allocation are rejected.
 * native_header_bytes must match the registered native ops table (8 for RTL).
 * Output must not overlap the native allocation. Failure does not modify out. */
int rtl_ios_repack_tx(const void *allocation,size_t allocation_bytes,
                      uint32_t ios_address,unsigned native_header_bytes,
                      void *out,size_t capacity,size_t *written);

/* Decode the completed Realtek DMA aggregate into native RX vectors. The RX
 * header's +444 buffer pointer must match dma_address. Only count/status and
 * vectors are written, after the entire aggregate passes validation. Neither
 * DMA payload nor the opaque native IPC fields are modified. On failure the
 * header remains unchanged; caller completes/releases the failed batch.
 * DMA storage must remain owned until native input returns it. */
int rtl_ios_publish_rx(void *header,size_t header_bytes,uint32_t dma_address,
                       const void *dma,size_t dma_allocation_bytes,
                       size_t completed_bytes);
#endif
