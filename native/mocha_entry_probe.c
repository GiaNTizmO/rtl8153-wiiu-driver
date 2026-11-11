/* Local experimental Mocha entry candidate; not deployed on a console.
 * Runs once from the reload-only NET init hook, after the original init and
 * before NET starts its workers. Copies a 28-byte position-independent blob
 * into ONE page of NET's own local heap (0xCAFE, inside NET BSS), cleans the
 * data cache for that page, calls it once, checks its marker/return, frees
 * the page and commits a mailbox. No USB, ops, threads, mappings or domains. */
#include <stdint.h>
#include <stddef.h>
#include "mocha_entry_status.h"

extern void *rtl_net_alloc(uint32_t heap, uint32_t bytes, uint32_t alignment);
extern void rtl_net_free(uint32_t heap, void *memory);
extern int rtl_net_flush_dcache(void *address, uint32_t bytes);
extern int rtl_net_printf(const char *format, ...);
extern const uint32_t rtl_heap_entry_blob[], rtl_heap_entry_blob_end[];

#define LOCAL_HEAP 0xcafeu
typedef uint32_t (*RtlHeapEntry)(volatile uint32_t *out, uint32_t nonce);

volatile RtlMochaEntryStatus rtl_mocha_entry_status
    __attribute__((section(".rtl_alloc_status"),aligned(64)));

static void commit(volatile RtlMochaEntryStatus *s, uint32_t stage)
{
    s->stage=stage;
    s->commit=~RTL_ENTRY_STATUS_MAGIC;
}

static int page_in_local_heap(uintptr_t page)
{
    return !(page & (RTL_ENTRY_PAGE_BYTES-1u)) &&
           page >= RTL_NET_LOCAL_HEAP_START &&
           page - RTL_NET_LOCAL_HEAP_START <= RTL_NET_LOCAL_HEAP_BYTES - RTL_ENTRY_PAGE_BYTES;
}

int rtl_mocha_net_entry_probe_after_init(int result)
{
    volatile RtlMochaEntryStatus *s=&rtl_mocha_entry_status;
    s->commit=0;
    s->magic=RTL_ENTRY_STATUS_MAGIC; s->abi=RTL_ENTRY_STATUS_ABI;
    s->stage=RTL_ENTRY_ENTERED; s->init_result=(uint32_t)result;
    s->page=0; s->copy_ok=0; s->flush_rc=0; s->entry_return=0;
    s->observed_pc=0; s->observed_marker=0; s->nonce=0;
    s->alloc_calls=0; s->flush_calls=0; s->entry_calls=0; s->free_calls=0;
    if (result < 0) { commit(s, RTL_ENTRY_INIT_FAILED); return result; }

    s->alloc_calls=1;
    volatile uint32_t *page=rtl_net_alloc(LOCAL_HEAP, RTL_ENTRY_PAGE_BYTES, RTL_ENTRY_PAGE_BYTES);
    if (!page) {
        commit(s, RTL_ENTRY_NO_MEMORY);
        rtl_net_printf("RTL_NET_ENTRY: allocation failed; init=%d\n", result);
        return result;
    }
    uintptr_t address=(uintptr_t)page;
    s->page=(uint32_t)address;
    uint32_t stage;
    if (!page_in_local_heap(address)) {
        stage=RTL_ENTRY_OUT_OF_RANGE; /* never written, never entered */
    } else {
        unsigned words=(unsigned)(rtl_heap_entry_blob_end-rtl_heap_entry_blob);
        int same=words>0 && words<=RTL_ENTRY_PAGE_BYTES/4u;
        for (unsigned i=0; same && i<words; ++i) page[i]=rtl_heap_entry_blob[i];
        for (unsigned i=0; same && i<words; ++i) same=page[i]==rtl_heap_entry_blob[i];
        s->copy_ok=(uint32_t)same;
        if (!same) {
            stage=RTL_ENTRY_COPY_MISMATCH;
        } else {
            s->flush_calls=1;
            int flushed=rtl_net_flush_dcache((void *)address, RTL_ENTRY_PAGE_BYTES);
            s->flush_rc=(uint32_t)flushed;
            if (flushed != 0) {
                stage=RTL_ENTRY_FLUSH_FAILED;
            } else {
                uint32_t nonce=(uint32_t)address ^ RTL_ENTRY_NONCE_SALT;
                s->nonce=nonce;
                s->stage=RTL_ENTRY_ENTERING;
                s->entry_calls=1;
                uint32_t returned=((RtlHeapEntry)address)(&s->observed_pc, nonce);
                s->entry_return=returned;
                stage=returned==(nonce ^ RTL_ENTRY_BLOB_MARKER) &&
                      s->observed_pc==(uint32_t)address+8u &&
                      s->observed_marker==RTL_ENTRY_BLOB_MARKER ?
                      RTL_ENTRY_RETURNED_OK : RTL_ENTRY_RETURNED_BAD;
            }
        }
    }
    rtl_net_free(LOCAL_HEAP, (void *)page);
    s->free_calls=1;
    commit(s, stage);
    rtl_net_printf("RTL_NET_ENTRY: page=%08lx stage=%lu freed\n",
                   (unsigned long)address, (unsigned long)stage);
    return result;
}
