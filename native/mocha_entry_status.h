#ifndef RTL_MOCHA_ENTRY_STATUS_H
#define RTL_MOCHA_ENTRY_STATUS_H
#include <stdint.h>

/* Entry diagnostic mailbox (ABI 2). Same owned BSS extension as the earlier
 * allocation mailbox: it exists only in the experimental boot ELF whose NET
 * BSS was explicitly extended to 0x1288d080. Never write it in stock IOS. */
#define RTL_ENTRY_STATUS_ADDRESS 0x1288d040u
#define RTL_ENTRY_STATUS_BYTES 64u
#define RTL_ENTRY_STATUS_MAGIC 0x52544c45u /* RTLE */
#define RTL_ENTRY_STATUS_ABI 2u
#define RTL_ENTRY_BLOB_MARKER 0x52544c58u /* RTLX, written only by heap code */
#define RTL_ENTRY_NONCE_SALT 0x5a17c0deu

/* NET main (owner 5.5.6) creates its local process heap with svc 0x24 at
 * 0x12300ab4 from buffer 0x124c6060, 0x40000 bytes: inside the NET-owned,
 * identity-mapped BSS PT_LOAD 0x124c6000..0x1288d028 (uid 7, RW). The probe
 * executes only a page fully inside this range. */
#define RTL_NET_LOCAL_HEAP_START 0x124c6060u
#define RTL_NET_LOCAL_HEAP_BYTES 0x40000u
#define RTL_ENTRY_PAGE_BYTES 4096u

enum RtlMochaEntryStage {
    RTL_ENTRY_ENTERED=1,      /* transient, never committed */
    RTL_ENTRY_INIT_FAILED,    /* original NET init < 0; nothing else done */
    RTL_ENTRY_NO_MEMORY,      /* allocation returned NULL */
    RTL_ENTRY_OUT_OF_RANGE,   /* misaligned/outside local heap; freed, not run */
    RTL_ENTRY_COPY_MISMATCH,  /* data readback differs; freed, not run */
    RTL_ENTRY_FLUSH_FAILED,   /* FlushDCache returned nonzero; freed, not run */
    RTL_ENTRY_ENTERING,       /* transient: heap code is being called */
    RTL_ENTRY_RETURNED_OK,    /* heap code returned exact expected values */
    RTL_ENTRY_RETURNED_BAD    /* heap code returned, values differ */
};
typedef struct {
    uint32_t magic,abi,stage,init_result;
    uint32_t page,copy_ok,flush_rc,entry_return;
    uint32_t observed_pc,observed_marker; /* written by the heap code itself */
    uint32_t nonce,alloc_calls,flush_calls,entry_calls,free_calls;
    uint32_t commit; /* ~magic, written last after all terminal fields */
} RtlMochaEntryStatus;
_Static_assert(sizeof(RtlMochaEntryStatus)==RTL_ENTRY_STATUS_BYTES,
               "entry status ABI");
#endif
