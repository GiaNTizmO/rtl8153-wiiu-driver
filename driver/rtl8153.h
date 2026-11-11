#ifndef WIIU_RTL8153_H
#define WIIU_RTL8153_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Realtek RTL8153 over CafeOS UHS. Experimental hardware port. */

#define RTL8153_MAC_LEN 6

typedef struct Rtl8153 Rtl8153;
/* Called by the owner before open; permits title shutdown during PHY waits. */
void rtl8153_set_cancel_check(int (*check)(void));

/* Finds the adapter, takes the interface and brings the chip up.
 * Returns NULL and writes why into `why`. Single instance, serial calls only. */
Rtl8153 *rtl8153_open(char *why, unsigned why_size);
void     rtl8153_close(Rtl8153 *ax);

/*
 * APPLICATION_ENDS path.
 *
 * /dev/uhs handles belong to the outgoing title process. At this late
 * lifecycle point do not issue UHS CANCEL/DISABLE/RELEASE/CLOSE requests:
 * forget the userspace bookkeeping and let process teardown reclaim IOSU
 * resources. The next title opens a completely fresh UHS client.
 */
void     rtl8153_abandon_title(Rtl8153 *ax);

/*
 * A physical USB unplug/replug resets the adapter even though this Aroma
 * session has already opened it before. Force the next open through the
 * complete cold PHY initialization/autonegotiation path.
 */
void     rtl8153_force_cold_next_open(void);

/* The adapter's own hardware address, read out of it. */
const uint8_t *rtl8153_mac(const Rtl8153 *ax);

/* Whether the PHY says a cable is in and a partner is talking, and at
 * what speed. `speed` is 10, 100 or 1000, or 0 when the link is down. */
/* Returns 1 up (MAC configured), 0 down/negotiating, -1 USB error.
 * Poll regularly to apply renegotiated speed/duplex before sending. */
int rtl8153_link(Rtl8153 *ax, int *speed);
int rtl8153_read_mac(Rtl8153 *ax, uint16_t reg, void *out, uint16_t size);
int rtl8153_read_phy(Rtl8153 *ax, uint16_t reg, uint16_t *value);
int32_t rtl8153_last_control(const Rtl8153 *ax);

/* One ethernet frame out. Returns 0, or -1. */
int rtl8153_send(Rtl8153 *ax, const void *frame, int length);

/*
 * One ethernet frame in, 0 when none is currently ready, -1 on
 * USB/malformed data.
 *
 * On Wii U the RX path keeps three asynchronous UHS bulk-IN requests
 * pending. Completed aggregates are copied into a parser-owned snapshot,
 * their DMA slot is immediately re-armed, and frames are then unwrapped.
 *
 * The adapter does not hand over bare frames: a bulk transfer carries
 * several, wrapped in the chip's own receive header, and this unwraps
 * them. Getting that wrong does not fail -- it silently corrupts every
 * inbound packet, which is what the AX88772B patcher had to fix on the
 * other chip and is the failure mode to watch for here.
 */
/* UHS timeout in microseconds, inferred from console timing measurements. */
int rtl8153_receive(Rtl8153 *ax, void *frame, int max_length, int timeout_us);

/*
 * Return the next ethernet frame already buffered inside the most recent
 * RTL8153 bulk transfer.
 *
 * This never submits a new USB request. It returns:
 *   >0 frame length
 *    0 no buffered frame remains
 *   -1 malformed buffered data
 */
int rtl8153_receive_buffered(Rtl8153 *ax, void *frame, int max_length);

/* What the last bulk request returned, and which endpoints were
 * chosen. Both exist because the first version swallowed its errors and
 * "no frames" could have meant any of five things. */
int32_t rtl8153_last_bulk(const Rtl8153 *ax);
/* Microseconds the last bulk IN took: a refusal returns at once, a
 * timeout takes the timeout it was given. */
uint32_t rtl8153_last_bulk_us(const Rtl8153 *ax);
void    rtl8153_endpoints(const Rtl8153 *ax, int *in, int *out, int *irq);

#ifdef __cplusplus
}
#endif

#endif /* WIIU_RTL8153_H */
