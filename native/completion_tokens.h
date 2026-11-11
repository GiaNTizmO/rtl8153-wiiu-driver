#ifndef RTL8153_COMPLETION_TOKENS_H
#define RTL8153_COMPLETION_TOKENS_H
#include "uhs_transport.h"

/* Serialized UHS wrapper. The real DMA pointer stays in bulk_async's data
 * argument; only the copied metadata.buffer becomes an opaque request token.
 * Stock dispatch forwards this word without dereferencing it for kind 1/2.
 * Only our callback may consume these records, never the ASIX callback.
 * Init requires a new/quiescent object. Tokens must NOT restart across a
 * controller reopen while an older worker message could still be delivered. */
typedef struct {
    RtlIosUhsCompletion original;
    uint32_t token, bytes;
    int pending;
} RtlIosCompletionToken;
typedef int (*RtlIosTokenDelivery)(void *user,const RtlIosUhsCompletion *meta,
                                  int status,uint32_t done);
typedef struct {
    RtlIosUhsBinding downstream;
    RtlIosCompletionToken slots[10];
    uint32_t next_token;
    IosAddress context,controller;
    RtlIosTokenDelivery deliver;
    void *user;
    int closing,busy;
    void (*after_delivery)(void*);
    void *after_user;
} RtlIosCompletionTokens;
int rtl_ios_tokens_init(RtlIosCompletionTokens*,const RtlIosUhsBinding*,
                        IosAddress context,IosAddress controller,
                        RtlIosTokenDelivery,void *user,RtlIosUhsBinding *wrapped);
/* Returns 0 for an accepted, consumed completion, even if the downstream
 * transfer failed. delivery_result carries that separate outcome. A worker
 * callback must not mistake a consumed USB error for an unhandled message. */
int rtl_ios_tokens_complete(RtlIosCompletionTokens*,IosAddress context,
                            IosAddress controller,unsigned kind,
                            IosAddress batch,uint32_t token,int signed_result,
                            int *delivery_result);
void rtl_ios_tokens_close(RtlIosCompletionTokens*);
int rtl_ios_tokens_drained(const RtlIosCompletionTokens*);
/* Configure final drain hook before the wrapper is attached/submitted. It
 * runs after token busy is cleared and may destroy the detached instance. */
int rtl_ios_tokens_set_after(RtlIosCompletionTokens*,void (*after)(void*),void*);
/* Single RTL controller binding for the exact firmware ops+0x34 ABI. Attach
 * only in the serialized worker after the loader has validated the image.
 * Detach requires drained state AND external proof that the worker/USB can
 * no longer invoke this callback. That proof is not provided by this API. */
int rtl_ios_tokens_attach(RtlIosCompletionTokens*);
int rtl_ios_tokens_detach(RtlIosCompletionTokens*);
/* Consumes messages for the bound controller even on token rejection, to
 * prevent stock worker's error path from freeing metadata.batch. Use
 * tokens_complete() when the caller needs the token acceptance outcome. */
int rtl_ios_tokens_callback(IosAddress context,IosAddress controller,
                            unsigned kind,IosAddress batch,uint32_t token,
                            int signed_result);
#endif
