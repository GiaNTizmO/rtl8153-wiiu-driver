#ifndef RTL_MOCHA_ALLOC_STATUS_H
#define RTL_MOCHA_ALLOC_STATUS_H
#include <stdint.h>

/* Owned only by the experimental boot ELF's explicit BSS extension.
 * Never write this address in the stock running IOS image. */
#define RTL_ALLOC_STATUS_ADDRESS 0x1288d040u
#define RTL_ALLOC_STATUS_BYTES 64u
#define RTL_ALLOC_STATUS_MAGIC 0x52544c41u
#define RTL_ALLOC_STATUS_ABI 1u
enum RtlMochaAllocStage {
    RTL_ALLOC_ENTERED=1, RTL_ALLOC_INIT_FAILED, RTL_ALLOC_NO_MEMORY,
    RTL_ALLOC_FREED, RTL_ALLOC_MISALIGNED
};
typedef struct {
    uint32_t magic,abi,stage,init_result;
    uint32_t page,data_ok,alloc_calls,free_calls;
    uint32_t reserved[7];
    uint32_t commit; /* Written last after the terminal status fields. */
} RtlMochaAllocStatus;
_Static_assert(sizeof(RtlMochaAllocStatus)==RTL_ALLOC_STATUS_BYTES,
               "allocation status ABI");
extern volatile RtlMochaAllocStatus rtl_mocha_alloc_status;
#endif
