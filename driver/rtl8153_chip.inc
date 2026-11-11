/* Realtek register transport and CafeOS initialization adapter.
 * BSD-derived chip sequencing is in rtl8153_bsd.inc (license retained). */
#include <errno.h>
#include "rtl8153_regs.h"
#define USB_SPEED_HIGH 3
#define USB_SPEED_SUPER 4
#define usbd_delay_ms(sc, ms) sleep_ms(ms)

static int ure_read_mem(Rtl8153 *sc, uint16_t reg, uint16_t bank, void *p, int len)
{
    if (sc->io_error) return -1;
    int rc = control(sc, 1, 0x05, reg, bank, len, p);
    if (rc) sc->io_error = 1;
    return rc;
}
static int ure_write_mem(Rtl8153 *sc, uint16_t reg, uint16_t bank, const void *p, int len)
{
    if (sc->io_error) return -1;
    int rc = control(sc, 0, 0x05, reg, bank, len, (void *)p);
    if (rc) sc->io_error = 1;
    return rc;
}
static uint32_t ure_read_4(Rtl8153 *sc, uint16_t reg, uint16_t bank)
{
    uint8_t b[4] = {0};
    ure_read_mem(sc, reg, bank, b, 4);
    return le32(b);
}
static uint16_t ure_read_2(Rtl8153 *sc, uint16_t reg, uint16_t bank)
{
    return (uint16_t)(ure_read_4(sc, reg & ~3u, bank) >> ((reg & 2) * 8));
}
static uint8_t ure_read_1(Rtl8153 *sc, uint16_t reg, uint16_t bank)
{
    return (uint8_t)(ure_read_4(sc, reg & ~3u, bank) >> ((reg & 3) * 8));
}
static void ure_write_4(Rtl8153 *sc, uint16_t reg, uint16_t bank, uint32_t val)
{
    uint8_t b[4] = { val, val >> 8, val >> 16, val >> 24 };
    ure_write_mem(sc, reg, bank | URE_BYTE_EN_DWORD, b, 4);
}
static void ure_write_2(Rtl8153 *sc, uint16_t reg, uint16_t bank, uint16_t val)
{
    unsigned shift = reg & 2;
    uint32_t v = (uint32_t)val << (shift * 8);
    uint8_t b[4] = { v, v >> 8, v >> 16, v >> 24 };
    ure_write_mem(sc, reg & ~3u, bank | (URE_BYTE_EN_WORD << shift), b, 4);
}
static void ure_write_1(Rtl8153 *sc, uint16_t reg, uint16_t bank, uint8_t val)
{
    unsigned shift = reg & 3;
    uint32_t v = (uint32_t)val << (shift * 8);
    uint8_t b[4] = { v, v >> 8, v >> 16, v >> 24 };
    ure_write_mem(sc, reg & ~3u, bank | (URE_BYTE_EN_BYTE << shift), b, 4);
}
#define URE_SETBIT_1(s,r,b,v) ure_write_1(s,r,b,ure_read_1(s,r,b)|(v))
#define URE_SETBIT_2(s,r,b,v) ure_write_2(s,r,b,ure_read_2(s,r,b)|(v))
#define URE_SETBIT_4(s,r,b,v) ure_write_4(s,r,b,ure_read_4(s,r,b)|(v))
#define URE_CLRBIT_1(s,r,b,v) ure_write_1(s,r,b,ure_read_1(s,r,b)&~(v))
#define URE_CLRBIT_2(s,r,b,v) ure_write_2(s,r,b,ure_read_2(s,r,b)&~(v))
#define URE_CLRBIT_4(s,r,b,v) ure_write_4(s,r,b,ure_read_4(s,r,b)&~(v))

static uint16_t ure_phy_read(Rtl8153 *sc, uint16_t addr)
{
    ure_write_2(sc, URE_PLA_OCP_GPHY_BASE, URE_MCU_TYPE_PLA, addr & 0xf000);
    return ure_read_2(sc, (addr & 0x0fff) | 0xb000, URE_MCU_TYPE_PLA);
}
static void ure_phy_write(Rtl8153 *sc, uint16_t addr, uint16_t val)
{
    ure_write_2(sc, URE_PLA_OCP_GPHY_BASE, URE_MCU_TYPE_PLA, addr & 0xf000);
    ure_write_2(sc, (addr & 0x0fff) | 0xb000, URE_MCU_TYPE_PLA, val);
}

#include "rtl8153_bsd.inc"

/* RTL8153B subset of OpenBSD ure_rtl8153b_init (same BSD license as
 * rtl8153_bsd.inc). RTL8156/8157 paths are deliberately not selected. */
static int rtl8153b_init(Rtl8153 *sc)
{
    URE_CLRBIT_2(sc, URE_USB_LPM_CONFIG, URE_MCU_TYPE_USB, LPM_U1U2_EN);
    unsigned i;
    for (i = 0; i < 500 && !sc->io_error; ++i) {
        if (ure_read_2(sc, URE_PLA_BOOT_CTRL, URE_MCU_TYPE_PLA) & URE_AUTOLOAD_DONE) break;
        sleep_ms(20);
    }
    if (i == 500 || sc->io_error) return -1;
    ure_rtl8153_phy_status(sc, 0);
    ure_rtl8153_phy_status(sc, URE_PHY_STAT_LAN_ON);
    URE_CLRBIT_2(sc, URE_USB_U2P3_CTRL, URE_MCU_TYPE_USB, URE_U2P3_ENABLE);
    ure_write_2(sc, URE_USB_MSC_TIMER, URE_MCU_TYPE_USB, 4095);
    URE_CLRBIT_2(sc, URE_USB_POWER_CUT, URE_MCU_TYPE_USB, URE_PWR_EN);
    URE_CLRBIT_2(sc, URE_USB_MISC_0, URE_MCU_TYPE_USB, URE_PCUT_STATUS);
    URE_CLRBIT_1(sc, URE_USB_POWER_CUT, URE_MCU_TYPE_USB, URE_UPS_EN | URE_USP_PREWAKE);
    URE_CLRBIT_1(sc, URE_USB_MISC_2, URE_MCU_TYPE_USB, URE_UPS_FORCE_PWR_DOWN | URE_UPS_NO_UPS);
    URE_CLRBIT_1(sc, URE_PLA_INDICATE_FALG, URE_MCU_TYPE_PLA, URE_UPCOMING_RUNTIME_D3);
    URE_CLRBIT_1(sc, URE_PLA_SUSPEND_FLAG, URE_MCU_TYPE_PLA, URE_LINK_CHG_EVENT);
    URE_CLRBIT_2(sc, URE_PLA_EXTRA_STATUS, URE_MCU_TYPE_PLA, URE_LINK_CHANGE_FLAG);
    ure_write_1(sc, URE_PLA_CRWECR, URE_MCU_TYPE_PLA, URE_CRWECR_CONFIG);
    URE_CLRBIT_2(sc, URE_PLA_CONFIG34, URE_MCU_TYPE_PLA, URE_LINK_OFF_WAKE_EN);
    ure_write_1(sc, URE_PLA_CRWECR, URE_MCU_TYPE_PLA, URE_CRWECR_NORAML);
    uint16_t status = ure_read_2(sc, URE_PLA_EXTRA_STATUS, URE_MCU_TYPE_PLA);
    if (ure_read_2(sc, URE_PLA_PHYSTATUS, URE_MCU_TYPE_PLA) & URE_PHYSTATUS_LINK) status |= URE_CUR_LINK_OK;
    else status &= ~URE_CUR_LINK_OK;
    ure_write_2(sc, URE_PLA_EXTRA_STATUS, URE_MCU_TYPE_PLA, status | URE_POLL_LINK_CHG);
    URE_SETBIT_2(sc, URE_PLA_MAC_PWR_CTRL2, URE_MCU_TYPE_PLA, URE_MAC_CLK_SPDWN_EN);
    URE_CLRBIT_2(sc, URE_USB_USB_CTRL, URE_MCU_TYPE_USB, URE_RX_AGG_DISABLE | URE_RX_ZERO_EN);
    URE_SETBIT_2(sc, URE_PLA_RSTTALLY, URE_MCU_TYPE_PLA, URE_TALLY_RESET);
    return sc->io_error ? -1 : 0;
}

/* A failed USB request poisons this initialization attempt. No subsequent
 * register writes are sent using a value synthesized by a failed read. */
static int rtl_wait(Rtl8153 *sc, uint16_t reg, uint16_t bank, unsigned width,
                    uint32_t mask, uint32_t want, unsigned count, int delay_ms)
{
    for (unsigned i = 0; i < count && !sc->io_error; ++i) {
        uint32_t v = width == 1 ? ure_read_1(sc, reg, bank) : ure_read_2(sc, reg, bank);
        if (!sc->io_error && (v & mask) == want) return 0;
        sleep_ms(delay_ms);
    }
    WHBLogPrintf("RTL8153 wait failed reg=%04x mask=%04lx want=%04lx UHS=%ld",
        reg, (unsigned long)mask, (unsigned long)want, (long)sc->last_control);
    sc->io_error = 1;
    return -1;
}

static int rtl_nic_reset(Rtl8153 *sc)
{
    uint8_t zero[8] = {0};
    if (sc->chip_b)
        URE_CLRBIT_2(sc, URE_USB_LPM_CONFIG, URE_MCU_TYPE_USB, LPM_U1U2_EN);
    else
        ure_write_mem(sc, URE_USB_TOLERANCE, URE_BYTE_EN_SIX_BYTES, zero, 8);
    URE_CLRBIT_2(sc, URE_USB_U2P3_CTRL, URE_MCU_TYPE_USB, URE_U2P3_ENABLE);
    ure_phy_write(sc, URE_OCP_POWER_CFG, ure_phy_read(sc, URE_OCP_POWER_CFG) & ~URE_EN_ALDPS);
    if (rtl_wait(sc, 0xe000, URE_MCU_TYPE_PLA, 2, 0x0100, 0x0100, 20, 1)) return -1;
    URE_SETBIT_2(sc, URE_PLA_MISC_1, URE_MCU_TYPE_PLA, URE_RXDY_GATED_EN);
    if (sc->chip_b) ure_write_1(sc, URE_PLA_TEREDO_CFG, URE_MCU_TYPE_PLA, 0xff);
    else URE_CLRBIT_2(sc, URE_PLA_TEREDO_CFG, URE_MCU_TYPE_PLA,
                     URE_TEREDO_SEL | URE_TEREDO_RS_EVENT_MASK | URE_OOB_TEREDO_EN);
    ure_write_2(sc, URE_PLA_WDT6_CTRL, URE_MCU_TYPE_PLA, URE_WDT6_SET_MODE);
    ure_write_2(sc, URE_PLA_REALWOW_TIMER, URE_MCU_TYPE_PLA, 0);
    ure_write_4(sc, URE_PLA_TEREDO_TIMER, URE_MCU_TYPE_PLA, 0);
    URE_CLRBIT_4(sc, URE_PLA_RCR, URE_MCU_TYPE_PLA, URE_RCR_ACPT_ALL);
    ure_write_1(sc, URE_PLA_CR, URE_MCU_TYPE_PLA, URE_CR_RST);
    if (rtl_wait(sc, URE_PLA_CR, URE_MCU_TYPE_PLA, 1, URE_CR_RST, 0, 1000, 1)) return -1;
    URE_CLRBIT_1(sc, URE_USB_BMU_RESET, URE_MCU_TYPE_USB, BMU_RESET_EP_IN | BMU_RESET_EP_OUT);
    URE_SETBIT_1(sc, URE_USB_BMU_RESET, URE_MCU_TYPE_USB, BMU_RESET_EP_IN | BMU_RESET_EP_OUT);
    URE_CLRBIT_1(sc, URE_PLA_OOB_CTRL, URE_MCU_TYPE_PLA, URE_NOW_IS_OOB);
    URE_CLRBIT_2(sc, URE_PLA_SFF_STS_7, URE_MCU_TYPE_PLA, URE_MCU_BORW_EN);
    if (rtl_wait(sc, URE_PLA_OOB_CTRL, URE_MCU_TYPE_PLA, 1, URE_LINK_LIST_READY, URE_LINK_LIST_READY, 1000, 1)) return -1;
    URE_SETBIT_2(sc, URE_PLA_SFF_STS_7, URE_MCU_TYPE_PLA, URE_RE_INIT_LL);
    if (rtl_wait(sc, URE_PLA_OOB_CTRL, URE_MCU_TYPE_PLA, 1, URE_LINK_LIST_READY, URE_LINK_LIST_READY, 1000, 1)) return -1;
    URE_CLRBIT_2(sc, URE_PLA_CPCR, URE_MCU_TYPE_PLA, URE_CPCR_RX_VLAN);
    ure_write_2(sc, URE_PLA_RMS, URE_MCU_TYPE_PLA, 1522);
    ure_write_1(sc, URE_PLA_MTPS, URE_MCU_TYPE_PLA, 192);
    URE_SETBIT_2(sc, URE_PLA_TCR0, URE_MCU_TYPE_PLA, URE_TCR0_AUTO_FIFO);
    ure_write_1(sc, URE_PLA_CR, URE_MCU_TYPE_PLA, URE_CR_RST);
    if (rtl_wait(sc, URE_PLA_CR, URE_MCU_TYPE_PLA, 1, URE_CR_RST, 0, 1000, 1)) return -1;
    ure_write_4(sc, URE_PLA_RXFIFO_CTRL0, URE_MCU_TYPE_PLA, URE_RXFIFO_THR1_NORMAL);
    ure_write_2(sc, URE_PLA_RXFIFO_CTRL1, URE_MCU_TYPE_PLA, URE_RXFIFO_THR2_NORMAL);
    ure_write_2(sc, URE_PLA_RXFIFO_CTRL2, URE_MCU_TYPE_PLA, URE_RXFIFO_THR3_NORMAL);
    ure_write_4(sc, URE_PLA_TXFIFO_CTRL, URE_MCU_TYPE_PLA, URE_TXFIFO_THR_NORMAL2);
    if (sc->chip_b) {
        ure_write_4(sc, URE_USB_RX_BUF_TH, URE_MCU_TYPE_USB, URE_RX_THR_B);
        URE_CLRBIT_2(sc, URE_PLA_MAC_PWR_CTRL3, URE_MCU_TYPE_PLA, URE_PLA_MCU_SPDWN_EN);
    } else {
        URE_SETBIT_1(sc, URE_PLA_CONFIG6, URE_MCU_TYPE_PLA, URE_LANWAKE_CLR_EN);
        URE_CLRBIT_1(sc, URE_PLA_LWAKE_CTRL_REG, URE_MCU_TYPE_PLA, URE_LANWAKE_PIN);
        URE_CLRBIT_2(sc, URE_USB_SSPHYLINK1, URE_MCU_TYPE_USB, URE_DELAY_PHY_PWR_CHG);
    }
    /* Keep USB power-saving off on the Wii U USB 2 host. */
    return sc->io_error ? -1 : 0;
}

static int rtl_chip_init(Rtl8153 *sc)
{
    uint16_t ver = ure_read_2(sc, URE_PLA_TCR1, URE_MCU_TYPE_PLA) & URE_VERSION_MASK;
    switch (ver) {
    case 0x5c00: sc->chip = URE_CHIP_VER_5C00; break;
    case 0x5c10: sc->chip = URE_CHIP_VER_5C10; break;
    case 0x5c20: sc->chip = URE_CHIP_VER_5C20; break;
    case 0x5c30: sc->chip = URE_CHIP_VER_5C30; break;
    case 0x6000: sc->chip_b = 1; break;
    case 0x6010: sc->chip_b = 1; break;
    default:
        WHBLogPrintf("RTL8153 unsupported TCR revision %04x (USB bcdDevice is not silicon revision)", ver);
        sc->io_error = 1;
        return -1;
    }
    WHBLogPrintf("RTL8153 TCR=%04x, USB bulk maxpacket=%u", ver, sc->tx_maxpacket);
    if ((sc->chip_b ? rtl8153b_init(sc) : ure_rtl8153_init(sc)) || sc->io_error) return -1;
    uint8_t mac[8] = {0};
    if (ure_read_mem(sc, URE_PLA_BACKUP, URE_MCU_TYPE_PLA, mac, 8)) return -1;
    unsigned any = 0, all = 0xff;
    for (int i = 0; i < 6; ++i) { any |= mac[i]; all &= mac[i]; }
    if (!any || all == 0xff || (mac[0] & 1)) {
        sc->io_error = 1;
        WHBLogPrintf("RTL8153 invalid factory MAC");
        return -1;
    }
    memcpy(sc->mac, mac, 6);
    WHBLogPrintf("RTL8153 MAC %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (rtl_nic_reset(sc)) return -1;
    ure_write_1(sc, URE_PLA_CRWECR, URE_MCU_TYPE_PLA, URE_CRWECR_CONFIG);
    ure_write_mem(sc, URE_PLA_IDR, URE_MCU_TYPE_PLA | URE_BYTE_EN_SIX_BYTES, mac, 8);
    ure_write_1(sc, URE_PLA_CRWECR, URE_MCU_TYPE_PLA, URE_CRWECR_NORAML);
    ure_write_2(sc, URE_USB_RX_EARLY_SIZE, URE_MCU_TYPE_USB,
        (RX_BUFFER_SIZE - 1522 - 24 - 8) / (sc->chip_b ? 8 : 4));
    ure_write_2(sc, URE_USB_RX_EARLY_AGG, URE_MCU_TYPE_USB, sc->chip_b ? 158 : URE_COALESCE_HIGH / 8);
    if (sc->chip_b) {
        ure_write_2(sc, URE_USB_PM_CTRL_STATUS, URE_MCU_TYPE_USB, 1875);
        uint16_t l1 = ure_read_2(sc, URE_USB_L1_CTRL, URE_MCU_TYPE_USB);
        ure_write_2(sc, URE_USB_L1_CTRL, URE_MCU_TYPE_USB, (l1 & ~0x0f) | 1);
    }
    if (ver == 0x6010) {
        URE_CLRBIT_2(sc, URE_USB_FW_TASK, URE_MCU_TYPE_USB, URE_FC_PATCH_TASK);
        sleep_ms(1);
        URE_SETBIT_2(sc, URE_USB_FW_TASK, URE_MCU_TYPE_USB, URE_FC_PATCH_TASK);
    }
    /* PHY autonegotiation: 10/100/1000 full and half, symmetric/asymmetric pause. */
    ure_phy_write(sc, URE_OCP_BASE_MII + 4 * 2, 0x0de1);
    ure_phy_write(sc, URE_OCP_BASE_MII + 9 * 2, 0x0200);
    ure_phy_write(sc, URE_OCP_BASE_MII, 0x1200);
    return sc->io_error ? -1 : 0;
}

int rtl8153_read_mac(Rtl8153 *sc, uint16_t reg, void *out, uint16_t size)
{
    return ure_read_mem(sc, reg, URE_MCU_TYPE_PLA, out, size);
}
int rtl8153_read_phy(Rtl8153 *sc, uint16_t reg, uint16_t *value)
{
    if (!sc || !value || reg > 31) return -1;
    *value = ure_phy_read(sc, URE_OCP_BASE_MII + reg * 2);
    return sc->io_error ? -1 : 0;
}
int32_t rtl8153_last_control(const Rtl8153 *sc)
{
    return sc ? sc->last_control : -1;
}
