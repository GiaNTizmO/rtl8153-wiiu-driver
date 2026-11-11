#include "completion_bridge.h"
#include <string.h>

static int valid_pair(unsigned kind, IosAddress batch, IosAddress buffer)
{
    return (kind == 1 || kind == 2) && batch && !(batch & 3u) &&
           buffer && !(buffer & 63u);
}
int rtl_ios_bridge_init(RtlIosCompletionBridge *b)
{
    if (!b) return -1;
    memset(b, 0, sizeof(*b)); b->next_sequence = 1; b->initialized = 1;
    return 0;
}
int rtl_ios_bridge_arm(RtlIosCompletionBridge *b, unsigned kind,
                       IosAddress batch, IosAddress buffer,
                       uint32_t *sequence_out)
{
    if (!b || !b->initialized || !valid_pair(kind,batch,buffer) ||
        !sequence_out || !b->next_sequence) return -1;
    unsigned free_slot=10;
    for (unsigned i=0;i<10;++i) {
        const RtlIosBridgeLease *s=&b->leases[i];
        if (s->state!=RTL_BRIDGE_FREE && s->batch==batch && s->buffer==buffer) return -1;
        if (free_slot==10 && s->state==RTL_BRIDGE_FREE) free_slot=i;
    }
    if (free_slot==10) return -1;
    RtlIosBridgeLease *s=&b->leases[free_slot];
    *s=(RtlIosBridgeLease){.batch=batch,.buffer=buffer,.kind=kind,
                           .sequence=b->next_sequence++,.state=RTL_BRIDGE_ARMED};
    *sequence_out=s->sequence; return (int)free_slot;
}
int rtl_ios_bridge_complete(RtlIosCompletionBridge *b, unsigned kind,
                            IosAddress batch, IosAddress buffer, int result,
                            unsigned *lease_out)
{
    if (!b || !b->initialized || !lease_out || !valid_pair(kind,batch,buffer)) return -1;
    for (unsigned i=0;i<10;++i) {
        RtlIosBridgeLease *s=&b->leases[i];
        if (s->state!=RTL_BRIDGE_ARMED || s->kind!=kind || s->batch!=batch || s->buffer!=buffer) continue;
        s->result=result; s->state=RTL_BRIDGE_COMPLETED; *lease_out=i; return 0;
    }
    return -1;
}
int rtl_ios_bridge_release(RtlIosCompletionBridge *b, unsigned lease)
{
    if (!b || !b->initialized || lease>=10 || b->leases[lease].state!=RTL_BRIDGE_COMPLETED) return -1;
    b->leases[lease].state=RTL_BRIDGE_RETIRED; return 0;
}
int rtl_ios_bridge_cancel(RtlIosCompletionBridge *b, unsigned lease)
{
    if (!b || !b->initialized || lease>=10 || b->leases[lease].state!=RTL_BRIDGE_ARMED) return -1;
    b->leases[lease].state=RTL_BRIDGE_RETIRED; return 0;
}
int rtl_ios_bridge_reset(RtlIosCompletionBridge *b)
{
    if (!b || !b->initialized || !rtl_ios_bridge_drained(b)) return -1;
    for (unsigned i=0;i<10;++i) b->leases[i]=(RtlIosBridgeLease){0};
    return b->next_sequence ? 0 : -1;
}
int rtl_ios_bridge_drained(const RtlIosCompletionBridge *b)
{
    if (!b || !b->initialized) return 0;
    for (unsigned i=0;i<10;++i)
        if (b->leases[i].state==RTL_BRIDGE_ARMED || b->leases[i].state==RTL_BRIDGE_COMPLETED) return 0;
    return 1;
}
