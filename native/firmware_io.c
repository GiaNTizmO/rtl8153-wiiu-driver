#include "firmware_io.h"
int rtl_ios_firmware_io_complete(void *opaque,const RtlIosUhsCompletion *m,
                                 int status,uint32_t done)
{
    RtlIosFirmwareIo *io=opaque;
    if(!io||!m||io->finishing)return -1;
    int rc=-1;
    if(m->kind==1&&io->rx)rc=rtl_ios_firmware_rx_complete(io->rx,m,status,done);
    else if(m->kind==2&&io->tx)rc=rtl_ios_firmware_tx_complete(io->tx,m,status,done);
    if(!rc)io->finishing=m->kind;
    return rc;
}
void rtl_ios_firmware_io_finish(void *opaque)
{
    RtlIosFirmwareIo *io=opaque;
    if(!io)return;
    unsigned kind=io->finishing;
    RtlIosFirmwareRx *rx=io->rx;RtlIosFirmwareTx *tx=io->tx;
    io->finishing=0;
    if(kind==1)rtl_ios_firmware_rx_finish(rx);
    else if(kind==2)rtl_ios_firmware_tx_finish(tx);
    /* No access to io: finalizer may destroy it. */
}
