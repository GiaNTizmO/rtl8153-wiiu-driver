"""Create a version-bound symbol/fingerprint manifest from the owner's ELF.
 * No firmware bytes beyond short instruction fingerprints are distributed.
"""
import hashlib,json,struct
from pathlib import Path
root=Path(__file__).resolve().parents[1]
folder=root/"evidence/firmware-5.5.6/analysis"
m=json.loads((folder/"map.json").read_text())
b=(folder/"ios.elf").read_bytes()
def read(addr,size):
    for s in m["segments"]:
        if s["type"]==1 and s["vaddr"]<=addr and addr+size<=s["vaddr"]+s["filesz"]:
            off=s["offset"]+addr-s["vaddr"];return b[off:off+size]
    raise ValueError(hex(addr))
symbols={
    "ifnet_eth_input":(0x1230886c,"RX IPC object -> native IP stack; no PPC callers established"),
    "ifnet_worker":(0x12308a14,"resource manager message dispatcher"),
    "eth_install_native_callbacks":(0x1232ac6c,"sets native queue/message callback at context +20/+24"),
    "eth_rx_release":(0x1232acd0,"called after native RX-copy input; returns batch to driver"),
    "eth_interface_init":(0x1232acf0,"builds native Ethernet interface callbacks/context"),
    "eth_tx":(0x1232b084,"native TX callback forwards to usb_eth_send_frame"),
    "eth_interface_open":(0x1232b0dc,"opens USB Ethernet API with callbacks"),
    "eth_rx_callback":(0x1232b13c,"queues native ifnet input; allocator release on send failure"),
    "eth_link_callback":(0x1232b1a0,"reports link to /dev/net/ifmgr/usbeth"),
    "usb_eth_send_frame":(0x123b464c,"copies frame into USB driver's TX batch"),
    "usb_eth_set_link":(0x123b4774,"synchronous worker request with 20-byte link config"),
    "usb_eth_open":(0x123b4924,"MAC destination plus three native callbacks"),
    "usb_eth_dispatch":(0x123b4bd0,"state transition dispatcher"),
    "usb_eth_context_get":(0x123b5618,"reads singleton pointer at 1288d020"),
    "usb_eth_discovery":(0x123b8e7c,"registers 0b95:7720 ops table with internal UHS client"),
    "asix_rx_completion":(0x123b94d8,"type 1 decapsulates RX, type 2 completes TX, type 3 link IRQ"),
    "asix_tx_async":(0x123b9730,"adds ASIX per-frame header and submits USB bulk OUT"),
    "asix_get_link":(0x123b9cf8,"writes 20-byte link information"),
    "asix_close":(0x123b9e54,"stops interrupt timer and cancels/releases controller"),
    "asix_open":(0x123b9f4c,"ASIX register init; unsuitable for Realtek"),
    "asix_rx_async":(0x123ba6b0,"submits 16 KiB bulk IN"),
    "uhs_control_sync":(0x123bdbd4,"internal UHS control API, ABI not fully bound"),
}
def fnv1a(data):
    v=2166136261
    for byte in data:v=((v^byte)*16777619)&0xffffffff
    return v
table=struct.unpack(">15I",read(0x12456dd0,60))
assert table[0]==0x0b957720 and table[8:]==(0x123b9f4c,0x123b9e54,0x123b9cf8,0x123b9730,0x123ba6b0,0x123b94d8,0x123b920c)
profile={"name":"owner-5.5.6-ios-net","decrypted_body_sha256":m["source_sha256"],
         "elf_sha256":hashlib.sha256(b).hexdigest(),"uid":7,"cpu":"ARM926EJ-S big endian",
         "ops_address":0x12456dd0,"ops_words":list(table),
         "interface_table":0x128090bc,"usb_context_pointer":0x1288d020,
         "symbols":{name:{"address":addr,"first_word":struct.unpack(">I",read(addr,4))[0],
                            "fingerprint_sha256_64":hashlib.sha256(read(addr,64)).hexdigest(),
                            "interpretation":note,"evidence":"static ARM disassembly; runtime binding unverified"}
                    for name,(addr,note) in symbols.items()}}
(root/"native/firmware_556.json").write_text(json.dumps(profile,indent=2)+"\n")
header=["/* Generated from owner firmware by scripts/make_ios_profile.py. */",
        "#pragma once","#include <stdint.h>",
        "static const struct { const char *name; uint32_t address,word; } ios_fingerprints[] = {"]
for name,s in profile["symbols"].items():
    header.append('    {"%s",0x%08xu,0x%08xu},'%(name,s["address"],fnv1a(read(s["address"],64))))
header += ["};","static const uint32_t ios_asix_ops_words[15] = {",
           ",".join("0x%08xu"%v for v in table),"};"]
(root/"native/firmware_556.h").write_text("\n".join(header)+"\n")
print("Validated ASIX table and generated",len(symbols),"static symbol fingerprints")
