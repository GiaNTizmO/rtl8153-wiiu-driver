#include "ios_completion_adapter.h"
#include <string.h>

int rtl_ios_completion_adapter_init(RtlIosCompletionAdapter *a,
                                    RtlIosCompletionDispatch dispatch,
                                    void *user)
{
    if (!a || !dispatch || rtl_ios_bridge_init(&a->bridge)) return -1;
    a->context=a->controller=0; a->dispatch=dispatch; a->user=user; a->bound=0;
    return 0;
}
int rtl_ios_completion_adapter_bind(RtlIosCompletionAdapter *a,
                                    IosAddress context, IosAddress controller)
{
    if (!a || !a->dispatch || !context || (context&3u) || !controller ||
        (controller&3u) || a->bound) return -1;
    a->context=context; a->controller=controller; a->bound=1; return 0;
}
int rtl_ios_completion_adapter_arm(RtlIosCompletionAdapter *a, unsigned kind,
                                   IosAddress batch, IosAddress buffer,
                                   uint32_t *sequence)
{
    if (!a || !a->bound) return -1;
    return rtl_ios_bridge_arm(&a->bridge,kind,batch,buffer,sequence);
}
int rtl_ios_completion_adapter_callback(RtlIosCompletionAdapter *a,
                                        IosAddress context, IosAddress controller,
                                        unsigned kind, IosAddress batch,
                                        IosAddress buffer, int result,
                                        unsigned *lease_out)
{
    if (!a || !a->bound || context!=a->context || controller!=a->controller ||
        !lease_out) return -1;
    unsigned lease;
    if (rtl_ios_bridge_complete(&a->bridge,kind,batch,buffer,result,&lease)) return -1;
    if (a->dispatch(a->user,kind,batch,buffer,result,lease)) {
        (void)rtl_ios_bridge_release(&a->bridge,lease); return -1;
    }
    *lease_out=lease; return 0;
}
int rtl_ios_completion_adapter_cancel(RtlIosCompletionAdapter *a,unsigned lease)
{
    return a ? rtl_ios_bridge_cancel(&a->bridge,lease) : -1;
}
int rtl_ios_completion_adapter_release(RtlIosCompletionAdapter *a,unsigned lease)
{
    return a ? rtl_ios_bridge_release(&a->bridge,lease) : -1;
}
int rtl_ios_completion_adapter_reset(RtlIosCompletionAdapter *a)
{
    return a ? rtl_ios_bridge_reset(&a->bridge) : -1;
}
