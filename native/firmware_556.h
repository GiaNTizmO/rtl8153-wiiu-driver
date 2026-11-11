/* Generated from owner firmware by scripts/make_ios_profile.py. */
#pragma once
#include <stdint.h>
static const struct { const char *name; uint32_t address,word; } ios_fingerprints[] = {
    {"ifnet_eth_input",0x1230886cu,0xadb0bd93u},
    {"ifnet_worker",0x12308a14u,0xa430ef5cu},
    {"eth_install_native_callbacks",0x1232ac6cu,0xe5e9305bu},
    {"eth_rx_release",0x1232acd0u,0x841a082du},
    {"eth_interface_init",0x1232acf0u,0x02a84e40u},
    {"eth_tx",0x1232b084u,0xe8f478beu},
    {"eth_interface_open",0x1232b0dcu,0x82c768f8u},
    {"eth_rx_callback",0x1232b13cu,0xd0826138u},
    {"eth_link_callback",0x1232b1a0u,0x7b98d278u},
    {"usb_eth_send_frame",0x123b464cu,0xfce3832du},
    {"usb_eth_set_link",0x123b4774u,0xfd2a45ccu},
    {"usb_eth_open",0x123b4924u,0x3259f67fu},
    {"usb_eth_dispatch",0x123b4bd0u,0xcdad293cu},
    {"usb_eth_context_get",0x123b5618u,0xa58b7eedu},
    {"usb_eth_discovery",0x123b8e7cu,0xeac52490u},
    {"asix_rx_completion",0x123b94d8u,0x53bea551u},
    {"asix_tx_async",0x123b9730u,0x30523d21u},
    {"asix_get_link",0x123b9cf8u,0x470aa83du},
    {"asix_close",0x123b9e54u,0x7f9efa2eu},
    {"asix_open",0x123b9f4cu,0x1895146du},
    {"asix_rx_async",0x123ba6b0u,0xae52fe08u},
    {"uhs_control_sync",0x123bdbd4u,0x4cc21fa4u},
};
static const uint32_t ios_asix_ops_words[15] = {
0x0b957720u,0x00000534u,0x00004000u,0x00000004u,0x00000008u,0x00004000u,0x00000000u,0x00000002u,0x123b9f4cu,0x123b9e54u,0x123b9cf8u,0x123b9730u,0x123ba6b0u,0x123b94d8u,0x123b920cu
};
