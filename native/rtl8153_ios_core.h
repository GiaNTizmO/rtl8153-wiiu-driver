#ifndef RTL8153_IOS_CORE_H
#define RTL8153_IOS_CORE_H
#include <stddef.h>
#include <stdint.h>
/* Callbacks execute in IOS-NET on ARM. Control returns the byte count or a
 * negative error. Loader must verify firmware before binding callbacks. */
typedef struct {
    void *user;
    int (*control)(void *user,int read,uint8_t request,uint16_t value,
                   uint16_t index,void *buffer,uint16_t length);
    void (*sleep_ms)(void *user,unsigned ms);
    int (*cancelled)(void *user);
} RtlIosTransport;
typedef struct {
    RtlIosTransport transport;
    uint16_t tx_maxpacket,chip;
    int chip_b,io_error;
    int32_t last_control;
    uint8_t mac[6];
    uint8_t control_dma[64] __attribute__((aligned(64)));
} RtlIosDevice;
int rtl_ios_init(RtlIosDevice *device,const RtlIosTransport *transport);
int rtl_ios_get_link(RtlIosDevice *device,unsigned *mbps,int *full_duplex);
int rtl_ios_enable(RtlIosDevice *device);
int rtl_ios_disable(RtlIosDevice *device);
#endif
