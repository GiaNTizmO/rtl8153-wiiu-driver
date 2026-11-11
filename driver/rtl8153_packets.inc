#include "rtl8153_framing.h"

int rtl8153_link(Rtl8153 *sc, int *speed)
{
    if (speed) *speed = 0;
    if (!sc || sc->io_error) return -1;
    uint16_t status = ure_read_2(sc, URE_PLA_PHYSTATUS, URE_MCU_TYPE_PLA);
    if (sc->io_error) return -1;
    int up = !!(status & URE_PHYSTATUS_LINK);
    if (!up) {
        if (sc->medium) {
            URE_SETBIT_2(sc, URE_PLA_MISC_1, URE_MCU_TYPE_PLA, URE_RXDY_GATED_EN);
            URE_CLRBIT_1(sc, URE_PLA_CR, URE_MCU_TYPE_PLA, URE_CR_RE | URE_CR_TE);
            sc->medium = 0;
            WHBLogPrintf("RTL8153 link down");
        }
        return sc->io_error ? -1 : 0;
    }
    int rate = status & URE_PHYSTATUS_1000MBPS ? 1000 : status & URE_PHYSTATUS_100MBPS ? 100 : 10;
    if (!sc->medium) {
        URE_CLRBIT_2(sc, URE_PLA_FMC, URE_MCU_TYPE_PLA, URE_FMC_FCR_MCU_EN);
        URE_SETBIT_2(sc, URE_PLA_FMC, URE_MCU_TYPE_PLA, URE_FMC_FCR_MCU_EN);
        uint8_t multicast[8];
        memset(multicast, 0xff, sizeof(multicast));
        ure_write_mem(sc, URE_PLA_MAR, URE_MCU_TYPE_PLA | URE_BYTE_EN_DWORD, multicast, 8);
        /* Accept our MAC, broadcast, and multicast. No promiscuous reception. */
        uint32_t rcr = ure_read_4(sc, URE_PLA_RCR, URE_MCU_TYPE_PLA);
        rcr = (rcr & ~URE_RCR_ACPT_ALL) | URE_RCR_APM | URE_RCR_AB | URE_RCR_AM;
        ure_write_4(sc, URE_PLA_RCR, URE_MCU_TYPE_PLA, rcr);
        URE_SETBIT_1(sc, URE_PLA_CR, URE_MCU_TYPE_PLA, URE_CR_RE | URE_CR_TE);
        if (sc->chip_b)
            ure_write_1(sc, URE_USB_UPT_RXDMA_OWN, URE_MCU_TYPE_USB, URE_OWN_UPDATE | URE_OWN_CLEAR);
        URE_CLRBIT_2(sc, URE_PLA_MISC_1, URE_MCU_TYPE_PLA, URE_RXDY_GATED_EN);
        if (sc->io_error) return -1;
        sc->medium = 1;
        WHBLogPrintf("RTL8153 link up %d Mbps %s duplex", rate, status & URE_PHYSTATUS_FDX ? "full" : "half");
    }
    if (speed) *speed = rate;
    return 1;
}

int rtl8153_send(Rtl8153 *sc, const void *frame, int length)
{
    if (!sc || !sc->medium || sc->io_error || length < 0) return -1;
    int bytes = rtl8153_wrap(g_tx, sizeof(g_tx), frame, (size_t)length);
    if (bytes < 0) return -1;
    DCFlushRange(g_tx, sizeof(g_tx));
    int32_t rc = (int32_t)UhsSubmitBulkRequest(&sc->handle, sc->if_handle,
        sc->ep_out, UHS_DIR_OUT, g_tx, bytes, 1000000);
    sc->last_bulk = rc;
    return rc == bytes ? 0 : -1;
}

static int rtl8153_receive_buffered_impl(Rtl8153 *sc, void *frame, int max_length)
{
    if (!sc || !frame || max_length < 14) return -1;
    size_t pos = sc->rx_pos;
    int rc = rtl8153_unwrap(g_rx, (size_t)sc->rx_len, &pos, frame, (size_t)max_length);
    sc->rx_pos = (int)pos;
    return rc;
}
int rtl8153_receive_buffered(Rtl8153 *sc, void *frame, int max_length)
{
    return rtl8153_receive_buffered_impl(sc, frame, max_length);
}
