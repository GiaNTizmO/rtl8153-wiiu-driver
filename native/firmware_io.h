#ifndef RTL8153_FIRMWARE_IO_H
#define RTL8153_FIRMWARE_IO_H
#include "firmware_rx.h"
#include "firmware_tx.h"

/* One serialized token completion queue, two firmware batch adapters.
 * finish routes to exactly one finalizer. A finalizer may destroy the entire
 * controller, so nothing accesses this router after invoking that finalizer.
 * No new object init/close/USB registration or loader calls are performed. */
typedef struct {
    RtlIosFirmwareRx *rx;
    RtlIosFirmwareTx *tx;
    unsigned finishing;
} RtlIosFirmwareIo;
int rtl_ios_firmware_io_complete(void*,const RtlIosUhsCompletion*,int,uint32_t);
void rtl_ios_firmware_io_finish(void*);
#endif
