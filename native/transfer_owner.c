#include "transfer_owner.h"
#include <string.h>
int rtl_ios_owner_init(RtlIosOwner *o,uint32_t interface_id)
{
    if(!o)return -1;
    /* Only valid on a NEW/unowned object. API cannot prove external lifetime. */
    memset(o,0,sizeof(*o));o->interface_id=interface_id;o->next_cookie=1;return 0;
}
int rtl_ios_owner_prepare(RtlIosOwner *o,unsigned index,int read,
                           IosAddress batch,IosAddress dma,uint32_t capacity)
{
    if(!o||o->closing||index>=10||!!read!=(index<2)||
       !batch||(batch&3)||!dma||(dma&63)||!capacity||
       capacity>(read?32768u:16384u)||capacity>UINT32_MAX-dma)return -1;
    RtlIosOwnedSlot *s=&o->slots[index];
    if(s->state!=RTL_SLOT_FREE)return -1;
    /* One batch/region per slot; reject aliases including partial overlap. */
    for(unsigned i=0;i<10;++i){
        RtlIosOwnedSlot *p=&o->slots[i];
        if(p->state==RTL_SLOT_FREE)continue;
        if(p->batch==batch||(dma<p->dma+p->capacity&&p->dma<dma+capacity))return -1;
    }
    *s=(RtlIosOwnedSlot){.batch=batch,.dma=dma,.capacity=capacity,
                        .kind=read?1u:2u,.state=RTL_SLOT_PREPARED};return 0;
}
int rtl_ios_owner_submit(RtlIosOwner *o,unsigned index,RtlIosUhsBinding *b,uint32_t bytes)
{
    if(!o||!b||o->closing||index>=10||b->interface_id!=o->interface_id)return -1;
    RtlIosOwnedSlot *s=&o->slots[index];
    if(s->state!=RTL_SLOT_PREPARED||bytes>s->capacity||!o->next_cookie)return -1;
    RtlIosUhsBinding tagged=*b;tagged.completion_cookie=o->next_cookie++;
    int rc=rtl_ios_uhs_submit(&tagged,s->kind==1,s->batch,s->dma,bytes);
    if(rc<0)return rc;
    s->cookie=tagged.completion_cookie;s->submitted=bytes;s->state=RTL_SLOT_PENDING;return 0;
}
static int complete_slot(RtlIosOwner *o,unsigned i,int result,uint32_t completed)
{
    RtlIosOwnedSlot *s=&o->slots[i];
    /* Oversized/short TX callbacks retire ownership but never authorize
     * delivery of potentially corrupt data. RX may legally be short. */
    s->result=result;
    if(result>=0&&(completed>s->submitted||(s->kind==2&&completed!=s->submitted)))
        s->result=-1;
    s->completed=s->result<0?0:completed;s->state=RTL_SLOT_READY;
    return (int)i;
}
int rtl_ios_owner_complete(RtlIosOwner *o,const RtlIosUhsCompletion *m,
                            int result,uint32_t completed)
{
    if(!o||!m||m->interface_id!=o->interface_id)return -1;
    for(unsigned i=0;i<10;++i){
        RtlIosOwnedSlot *s=&o->slots[i];
        if(s->state==RTL_SLOT_PENDING&&s->batch==m->batch&&s->dma==m->buffer&&
           s->kind==m->kind&&s->cookie==m->cookie)
            return complete_slot(o,i,result,completed);
    }
    return -1;
}
int rtl_ios_owner_complete_pair(RtlIosOwner *o,unsigned kind,IosAddress batch,
                                IosAddress dma,int result,uint32_t completed)
{
    if(!o||(kind!=1&&kind!=2)||!batch||!dma)return -1;
    for(unsigned i=0;i<10;++i){
        RtlIosOwnedSlot *s=&o->slots[i];
        if(s->state==RTL_SLOT_PENDING&&s->batch==batch&&s->dma==dma&&s->kind==kind)
            return complete_slot(o,i,result,completed);
    }
    return -1;
}
int rtl_ios_owner_deliver(RtlIosOwner *o,unsigned index)
{
    if(!o||o->closing||index>=2)return -1;
    RtlIosOwnedSlot *s=&o->slots[index];
    if(s->state!=RTL_SLOT_READY||s->result<0)return -1;
    s->state=RTL_SLOT_DELIVERED;return 0;
}
int rtl_ios_owner_release(RtlIosOwner *o,unsigned index)
{
    if(!o||index>=10)return -1;
    RtlIosOwnedSlot *s=&o->slots[index];
    if(s->state!=RTL_SLOT_PREPARED&&s->state!=RTL_SLOT_READY&&s->state!=RTL_SLOT_DELIVERED)return -1;
    memset(s,0,sizeof(*s));return 0;
}
void rtl_ios_owner_close(RtlIosOwner *o){if(o)o->closing=1;}
int rtl_ios_owner_drained(const RtlIosOwner *o)
{
    if(!o)return 0;
    for(unsigned i=0;i<10;++i)if(o->slots[i].state!=RTL_SLOT_FREE)return 0;
    return 1;
}
