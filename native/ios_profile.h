#ifndef RTL8153_IOS_PROFILE_H
#define RTL8153_IOS_PROFILE_H
#include <stdint.h>
typedef struct { uint32_t text_start,text_end,bss_start,bss_end; uint32_t discovery_slot,old_ops,context_pointer; uint32_t eth_install_callbacks,eth_rx_release,eth_interface_init,eth_tx,eth_interface_open,eth_rx_callback,eth_link_callback; uint32_t usb_send_frame,usb_set_link,usb_open,usb_dispatch,usb_context_get,usb_discovery; uint32_t completion,tx_async,get_link,close,open,rx_async,control_sync,post_link; } RtlIosFirmwareProfile;
const RtlIosFirmwareProfile *rtl_ios_profile_556(void);
int rtl_ios_profile_validate(const RtlIosFirmwareProfile *profile);
int rtl_ios_profile_validate_callbacks(const RtlIosFirmwareProfile *profile,uint32_t image_start,uint32_t image_end);
#endif
