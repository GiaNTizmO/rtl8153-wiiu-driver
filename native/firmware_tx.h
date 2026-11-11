#ifndef RTL8153_FIRMWARE_TX_H
#define RTL8153_FIRMWARE_TX_H
#include "batch_bridge.h"
#include "uhs_transport.h"

/* Serialized worker, caller-owned native TX batches. tx_done may synchronously
 * submit the next batch through stock 123b8034; it therefore runs only in
 * finish(), after the token wrapper has cleared busy. drain is last and may
 * destroy controller state. No hardware/cache/loader binding is automatic. */
typedef struct {
    void *user;
    int (*alloc_dma)(void*,uint32_t,void**,IosAddress*);
    void (*free_dma)(void*,void*,IosAddress);
    void (*pending)(void*,int);
    void (*tx_done)(void*,void*,IosAddress);
    void (*drain)(void*);
} RtlIosFirmwareTxHost;
typedef struct {
    void *batch,*dma;
    IosAddress batch_address,dma_address;
    uint32_t allocation,submitted,cookie;
    int pending;
} RtlIosFirmwareTxSlot;
typedef struct {
    RtlIosUhsBinding binding;
    RtlIosFirmwareTxHost host;
    RtlIosFirmwareTxSlot slots[8];
    uint32_t next_cookie;
    int closing,busy;
    void *completed_batch;
    IosAddress completed_address;
} RtlIosFirmwareTx;
int rtl_ios_firmware_tx_init(RtlIosFirmwareTx*,const RtlIosUhsBinding*,const RtlIosFirmwareTxHost*);
int rtl_ios_firmware_tx_submit(RtlIosFirmwareTx*,void*,IosAddress,uint32_t allocation);
int rtl_ios_firmware_tx_complete(void*,const RtlIosUhsCompletion*,int,uint32_t);
void rtl_ios_firmware_tx_finish(void*);
int rtl_ios_firmware_tx_close(RtlIosFirmwareTx*);
int rtl_ios_firmware_tx_drained(const RtlIosFirmwareTx*);
int rtl_ios_firmware_tx_attach(RtlIosFirmwareTx*,IosAddress context,IosAddress controller);
int rtl_ios_firmware_tx_detach(RtlIosFirmwareTx*);
int rtl_ios_firmware_send_async(IosAddress context,IosAddress controller,void *batch);
#endif
