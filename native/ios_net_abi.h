#ifndef RTL8153_IOS_NET_ABI_H
#define RTL8153_IOS_NET_ABI_H
#include <stddef.h>
#include <stdint.h>
/* Reverse-engineered wire/address layouts, NOT callable PPC pointers.
 * Derived from the owner's 5.5.6 IOS-NET; all fields are big endian in RAM.
 * Keep 32-bit address fields even when compiling tests on a 64-bit host. */
typedef uint32_t IosAddress;
typedef struct {
    IosAddress data;
    int32_t length;
} IosEthFrame;
typedef struct {
    uint8_t opaque00[0x38];
    int32_t count;
    int32_t status;
    IosEthFrame frames[128];
    uint32_t unknown440;
    IosAddress transfer_buffer;
} IosEthRxBatch;
_Static_assert(offsetof(IosEthRxBatch,count)==0x38,"RX count ABI");
_Static_assert(offsetof(IosEthRxBatch,status)==0x3c,"RX status ABI");
_Static_assert(offsetof(IosEthRxBatch,frames)==0x40,"RX vectors ABI");
_Static_assert(offsetof(IosEthRxBatch,transfer_buffer)==0x444,"RX buffer ABI");
_Static_assert(sizeof(IosEthRxBatch)==0x448,"RX header ABI");

/* TX and RX are DIFFERENT layouts. usb_eth_send_frame's helper at 123b87c8
 * stores aggregate bytes at +40 and vectors at +44 (123b88bc/123b88d8).
 * ASIX TX at 123b97ec loads +40 for the USB transfer length, while RX
 * completion at 123b964c builds vectors at +40. TX payload is inline +448.
 * Native TX packing aligns frame lengths to TWO bytes; Realtek aggregation
 * needs repacking/alignment before USB submission, even with header size 8. */
typedef struct {
    uint8_t opaque00[0x38];
    int32_t count;
    int32_t status;
    uint32_t transfer_bytes;
    IosEthFrame frames[128];
    uint32_t unknown444;
} IosEthTxBatch;
_Static_assert(offsetof(IosEthTxBatch,count)==0x38,"TX count ABI");
_Static_assert(offsetof(IosEthTxBatch,status)==0x3c,"TX status ABI");
_Static_assert(offsetof(IosEthTxBatch,transfer_bytes)==0x40,"TX bytes ABI");
_Static_assert(offsetof(IosEthTxBatch,frames)==0x44,"TX vectors ABI");
_Static_assert(sizeof(IosEthTxBatch)==0x448,"TX header / inline payload ABI");

typedef struct {
    uint16_t vid,pid;
    uint32_t controller_bytes;
    uint32_t tx_transfer_capacity;
    uint32_t tx_header_bytes;
    uint32_t tx_pending;
    uint32_t rx_transfer_capacity;
    uint32_t reserved18;
    uint32_t rx_pending;
    IosAddress open;
    IosAddress close;
    IosAddress get_link;
    IosAddress send_async;
    IosAddress receive_async;
    IosAddress completion;
    IosAddress post_link_interrupt;
} IosEthDriverOps;
_Static_assert(offsetof(IosEthDriverOps,open)==0x20,"ops open ABI");
_Static_assert(offsetof(IosEthDriverOps,completion)==0x34,"ops completion ABI");
_Static_assert(sizeof(IosEthDriverOps)==0x3c,"ops table ABI");

/* get_link writes this to caller storage. Offset 0 is not yet identified. */
typedef struct {
    uint32_t unknown00,autoneg_complete,flow_control,speed_mbps;
    uint8_t duplex;
    uint8_t padding[3];
} IosEthLink;
_Static_assert(sizeof(IosEthLink)==20,"link ABI");

enum {
    IOS_ETH_OPS_POINTER=0x000,
    IOS_ETH_CONTROLLER_INTERFACE=0x004,
    IOS_ETH_UHS_INTERFACE_HANDLE=0x018,
    IOS_ETH_ENDPOINTS=0x188,
    IOS_ETH_PENDING_LINK=0x1a4,
    IOS_ETH_PENDING_RX=0x1a8,
    IOS_ETH_PENDING_TX=0x1ac,
    IOS_ETH_TX_BATCH=0x1b4,
    IOS_ETH_ASIX_CTRL_DMA=0x524
};

/* Recovered ASIX receive_async call shape (123ba6b0): the caller supplies an
 * RX batch, whose +0x444 field is the 32-byte aligned inline buffer. The owner
 * driver submits 0x4000 bytes as kind 1 and keeps the batch and buffer alive
 * until the worker callback retires the matching pair. These constants are
 * layout facts, not callable firmware addresses. */
enum {
    IOS_ETH_RX_BATCH_BUFFER=0x444,
    IOS_ETH_RX_TRANSFER_BYTES=0x4000,
    IOS_ETH_RX_KIND=1
};
#endif
