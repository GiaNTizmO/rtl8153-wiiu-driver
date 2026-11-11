#ifndef RTL8153_FIRMWARE_TX_BINDING_H
#define RTL8153_FIRMWARE_TX_BINDING_H
#include "firmware_rx_binding.h"
#include "firmware_tx.h"
typedef struct {
    RtlIosFirmwareRxBinding common;
    RtlIosFirmwareRxHost memory;
} RtlIosFirmwareTxBinding;
/* Builds callbacks only; no install/attach/USB calls. Same loader/lifetime
 * requirements as firmware_rx_bind. TX functions get their own fingerprints. */
int rtl_ios_firmware_tx_bind(RtlIosFirmwareTxBinding*,IosAddress context,
                             IosAddress controller,
                             int (*read)(void*,IosAddress,void*,uint32_t),
                             void *user,RtlIosFirmwareTxHost *out);
#endif
