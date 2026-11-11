#ifndef RTL8153_FIRMWARE_RX_BINDING_H
#define RTL8153_FIRMWARE_RX_BINDING_H
#include "firmware_rx.h"

/* IOS-NET ARM only. Does not install ops, attach callbacks, claim USB or patch
 * memory. Caller validates full owner firmware/profile and lifetime first.
 * The five function addresses are pinned to owner 5.5.6 and checked over 64
 * bytes through a caller-supplied read callback before any invocation.
 * Binding storage must outlive final firmware drain and be detached by the
 * controller's teardown before it or its RX/token state can be reclaimed. */
typedef struct {
    IosAddress context,controller;
    int (*read)(void*,IosAddress,void*,uint32_t);
    void *read_user;
    int verified,free_error;
} RtlIosFirmwareRxBinding;
int rtl_ios_firmware_rx_bind(RtlIosFirmwareRxBinding*,IosAddress context,
                             IosAddress controller,
                             int (*read)(void*,IosAddress,void*,uint32_t),
                             void *read_user,RtlIosFirmwareRxHost *out);
#endif
