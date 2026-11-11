#include "completion_tokens.h"
#include <string.h>

static RtlIosCompletionTokens *active_tokens;

static int control(void *u,uint32_t h,uint8_t r,uint8_t t,uint16_t v,
                   uint16_t i,void *p,uint16_t n,uint32_t timeout)
{
    RtlIosCompletionTokens *s=u;
    return s->downstream.control_sync(s->downstream.uhs,h,r,t,v,i,p,n,timeout);
}
static int admin(void *u,uint32_t h,uint32_t op,uint32_t mask,uint32_t x,uint32_t y)
{
    RtlIosCompletionTokens *s=u;
    return s->downstream.endpoint_admin(s->downstream.uhs,h,op,mask,x,y);
}
static int bulk(void *u,uint32_t handle,uint8_t ep,unsigned direction,
                void *data,uint32_t bytes,const RtlIosUhsCompletion *m,int queue)
{
    RtlIosCompletionTokens *s=u;
    if(!s||!m||s->closing||s->busy||!s->next_token||
       handle!=s->downstream.interface_handle||queue!=s->downstream.completion_queue||
       m->interface_id!=s->downstream.interface_id||!m->cookie||
       (m->kind!=1&&m->kind!=2)||!m->batch||(m->batch&3u)||
       !m->buffer||(m->buffer&63u)||(uintptr_t)data!=m->buffer||!bytes||
       bytes>(m->kind==1?32768u:16384u)||bytes>UINT32_MAX-m->buffer||
       direction!=(m->kind==1?2u:1u)||
       ep!=((m->kind==1?s->downstream.bulk_in:s->downstream.bulk_out)&15u))return -1;
    unsigned index=10;
    for(unsigned i=0;i<10;++i){
        RtlIosCompletionToken *slot=&s->slots[i];
        if(slot->pending&&(slot->original.batch==m->batch||
           (m->buffer<slot->original.buffer+slot->bytes&&
            slot->original.buffer<m->buffer+bytes)))return -1;
        if(!slot->pending&&index==10)index=i;
    }
    if(index==10)return -1;
    RtlIosCompletionToken *slot=&s->slots[index];
    *slot=(RtlIosCompletionToken){.original=*m,.token=s->next_token++,
                                  .bytes=bytes,.pending=1};
    RtlIosUhsCompletion wire=*m;
    wire.buffer=slot->token;
    s->busy=1;
    int rc=s->downstream.bulk_async(s->downstream.uhs,handle,ep,direction,
                                    data,bytes,&wire,queue);
    s->busy=0;
    if(rc<0)memset(slot,0,sizeof(*slot));
    return rc;
}
int rtl_ios_tokens_init(RtlIosCompletionTokens *s,const RtlIosUhsBinding *d,
                        IosAddress context,IosAddress controller,
                        RtlIosTokenDelivery delivery,void *user,RtlIosUhsBinding *out)
{
    if(!s||active_tokens==s||!d||!out||!delivery||!context||(context&3u)||!controller||
       (controller&3u)||!d->uhs||!d->bulk_async||!d->control_sync||
       !d->interface_id||d->completion_queue<0||
       !(d->bulk_in&15)||(d->bulk_in&0x70)||!(d->bulk_in&0x80)||
       !(d->bulk_out&15)||(d->bulk_out&0xf0))return -1;
    RtlIosUhsBinding copy=*d;
    memset(s,0,sizeof(*s));s->downstream=copy;s->next_token=1;
    s->context=context;s->controller=controller;s->deliver=delivery;s->user=user;
    *out=copy;out->uhs=s;out->bulk_async=bulk;out->control_sync=control;
    out->endpoint_admin=copy.endpoint_admin?admin:NULL;
    return 0;
}
int rtl_ios_tokens_complete(RtlIosCompletionTokens *s,IosAddress context,
                            IosAddress controller,unsigned kind,IosAddress batch,
                            uint32_t token,int result,int *delivery_result)
{
    if(!s||!s->deliver||s->busy||!delivery_result||!token||
       context!=s->context||controller!=s->controller)return -1;
    for(unsigned i=0;i<10;++i){
        RtlIosCompletionToken *slot=&s->slots[i];
        if(!slot->pending||slot->token!=token||slot->original.kind!=kind||
           slot->original.batch!=batch)continue;
        RtlIosUhsCompletion original=slot->original;
        void (*after)(void*)=s->after_delivery;
        void *after_user=s->after_user;
        memset(slot,0,sizeof(*slot));
        s->busy=1;
        *delivery_result=s->deliver(s->user,&original,result<0?result:0,
                                     result<0?0u:(uint32_t)result);
        s->busy=0;
        if(after)after(after_user); /* May destroy s: no access below. */
        return 0;
    }
    return -1;
}
void rtl_ios_tokens_close(RtlIosCompletionTokens *s){if(s)s->closing=1;}
int rtl_ios_tokens_drained(const RtlIosCompletionTokens *s)
{
    if(!s||!s->deliver||s->busy)return 0;
    for(unsigned i=0;i<10;++i)if(s->slots[i].pending)return 0;
    return 1;
}
int rtl_ios_tokens_attach(RtlIosCompletionTokens *s)
{
    if(!s||!s->deliver||s->busy||s->closing||active_tokens)return -1;
    active_tokens=s;return 0;
}
int rtl_ios_tokens_set_after(RtlIosCompletionTokens *s,void (*after)(void*),void *user)
{
    if(!s||s==active_tokens||!rtl_ios_tokens_drained(s))return -1;
    s->after_delivery=after;s->after_user=user;return 0;
}
int rtl_ios_tokens_detach(RtlIosCompletionTokens *s)
{
    if(!s||active_tokens!=s||!s->closing||!rtl_ios_tokens_drained(s))return -1;
    active_tokens=NULL;return 0;
}
int rtl_ios_tokens_callback(IosAddress context,IosAddress controller,
                            unsigned kind,IosAddress batch,uint32_t token,int result)
{
    RtlIosCompletionTokens *s=active_tokens;
    if(!s||context!=s->context||controller!=s->controller)return -1;
    int delivery_result;
    (void)rtl_ios_tokens_complete(s,context,controller,kind,
                                  batch,token,result,&delivery_result);
    /* Stock worker interprets negative callback returns as permission to free
     * metadata.batch. A stale/duplicate request must never free a currently
     * reused batch. Consume messages for our bound controller even when the
     * token lookup rejects them; only accepted tokens reach deliver(). */
    return 0;
}
