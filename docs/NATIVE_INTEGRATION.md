# Native IOS-NET integration — investigation 2026-11-09

Status: headless Native Probe revision 2 completed on the owner's console.
All 22 code fingerprints and the ASIX operations table matched. No native
driver has been installed or executed. Existing RTL8153Module.wms and console
network settings remain unchanged. The separate Settings -> Menu hang remains
unresolved; the old probe's display crash was localized and removed.

## Target and evidence

Update after TCP syslog capture: Native Probe launch reached main/inspect and
faulted on its first WHBLogConsoleDraw -> OSScreenClearBufferEx, before
Mocha_InitLibrary or IOS-NET reads. Headless revision 2 has been built locally
with OSReport/SD output and no graphics symbols. Uploaded replacement was
downloaded back and SHA256-verified, then manually launched at PC time 02:36.
It reached Inspection complete at 02:36:03 (console uptime 00:04:14.678).
The separate Settings -> Menu hang remains unexplained. See DEBUGGING.md.

Firmware obtained read-only via the owner's FTP from the installed CafeOS title
00050010/1000400a/code/fw.img. Encrypted image SHA256:
FCC988D6713455C90D48F08FCD72F3ACB2ACE5674ACC5683F06ACB46F5EB8DAC.
Local decryption used the owner's existing SD backup OTP; no keys are printed,
embedded in source, or included in archives. Private inputs/output are ignored
under evidence/firmware-5.5.6. Do not distribute these files.

The ancast helper uses zero IV and documents the first block limitation. The
embedded ELF starts at decrypted-body offset 0x604, beyond that first block.
scripts/inspect_ios_firmware.py validates ELF32 big-endian program headers and
extracts USB/NET segments; scripts/ios_xrefs.py finds ARM literal references.
Symbol interpretations below are based on disassembly, not an SDK contract.

## Architecture established from image

- IOS-USB (UID 4): text 0x10100000, UHS service.
- IOS-NET (UID 7): text 0x12300000..0x12431843, rodata at 0x12440000,
  mutable data at 0x12469000/0x1246a000, BSS 0x124c6000..0x1288d027.
- USB Ethernet chip driver is inside IOS-NET; source strings identify
  usb_eth_interface.c, usb_eth_main.c, usb_eth_controller.c, usb_eth_asix.c.
- /dev/ifnet is the receive resource manager. /dev/net/ifmgr/usbeth receives
  native Ethernet link events. /dev/net/ifmgr/ncl supports status query 0x14
  according to both Stroopwafel and MochaPayload service clients.
- Native Ethernet interface opens the USB Ethernet abstraction and registers
  RX, link and release callbacks. This is beneath sockets/DNS/TLS.

## Located boundary

Full version-bound symbols and 64-byte SHA256 fingerprints:
native/firmware_556.json. Generated probe uses FNV-1a of those same 64 bytes;
this is a mismatch detector, not cryptographic authenticity verification.

| Address | Interpretation | Evidence |
|---|---|---|
| 0x1230886c | ifnet_eth_input_write | literal reference and Ethernet copy/dispatch |
| 0x1232acf0 | native Ethernet interface setup | callback assignments |
| 0x1232b084 | native Ethernet TX callback | forwards frame to 0x123b464c |
| 0x1232b0dc | open native USB Ethernet interface | invokes 0x123b4924 with callbacks |
| 0x1232b13c | RX callback | posts completed batch into ifnet queue |
| 0x1232b1a0 | link callback | opens /dev/net/ifmgr/usbeth and sends up/down |
| 0x123b464c | send Ethernet frame | queues/copies into TX aggregation |
| 0x123b4924 | USB Ethernet open API | registers MAC destination and callbacks |
| 0x123b8e7c | USB discovery registration | loads ASIX operation table |
| 0x12456dd0 | ASIX driver operations | VID 0b95, PID 7720, seven function entries |
| 0x1288d020 | USB Ethernet context pointer | singleton getter 0x123b5618 |
| 0x128090bc | native interface pointer table | indexed by slots 0/1 |

ASIX table: VID/PID, controller allocation 0x534, TX capacity 0x4000,
TX header size 4, TX pending 8, RX capacity 0x4000, reserved 0, RX pending 2;
operations at +0x20 open, +0x24 close, +0x28 get link, +0x2c async TX,
+0x30 async RX, +0x34 URB completion, +0x38 post link interrupt.
native/ios_net_abi.h records 32-bit layouts with compile-time assertions.
Do not assume raw address fields can be invoked from PPC.

Native RX batch: count +0x38, status +0x3c, up to 128 pointer/length entries
at +0x40, unknown word +0x440, transfer buffer pointer +0x444. Native TX is
different: aggregate bytes +0x40, vectors +0x44, inline payload +0x448.
Evidence: 123b964c builds RX vectors at +0x40; 123b88bc writes TX length +0x40;
123b88d8 builds TX vectors at +0x44; 123b88a4..123b88b8 computes inline payload.
TX frame records include chip header space; Realtek needs 8 bytes rather than
ASIX's 4. Native TX packing rounds frame lengths to two bytes (123b87fc/8804),
so setting header size to 8 alone does not satisfy Realtek aggregate alignment.
RX Realtek descriptors require stripping 24 bytes and FCS, eight-byte alignment.
native/frame_adapter.c validates aggregates and creates views without copying.

## Implemented locally

- Native ARM926EJ-S big-endian chip core, independent of WUT/lwIP/nsysnet,
  reusing the existing RTL8153 register implementation through an aligned
  callback transport. build/native/librtl8153_ios_core.a is an intermediate
  archive, not a loadable driver.
- ASan/UBSan tests for native RX views, multi-frame TX alignment and bounds;
  callback core initialization/link/enable/disable and transfer error injection.
- scripts/build_native_core.sh uses existing devkitARM without installing or
  modifying toolchain files. Rough object text size ~9 KiB before final linking.
- Read-only tools/native_probe/main.c compiled into a WUHB and uploaded to
  SD:/wiiu/apps/rtl8153_native_probe.wuhb. It uses Mocha kernel read32,
  compares code/ops fingerprints and reads bounded interface/context state.
  Status ioctl is query-only; it does not patch memory or claim USB.
- Headless probe completed one runtime inspection. A read-only kernel memory
  operation can still fault or stall; one successful run is not a general
  reliability guarantee. The original probe crash was in graphics, before
  memory inspection. The separate title-transition hang still needs evidence.

## Runtime inspection, headless revision 2

TCP log is retained locally in evidence/2026-11-09/iopshell/console.log.
22/22 64-byte code fingerprints and all 15 ASIX table words matched. Interface
table contains slot 0 = 12808e1c (wlan name bytes w l 0 NUL), slot 1 = 12808efc
(USB Ethernet, name starts USBE). Wi-Fi address is 192.168.31.144; Ethernet
IPv4 is zero. NCL ioctl 0x14 returned 0 for both: out[2] is 1 for Wi-Fi,
2 for Ethernet; numeric state semantics are not yet established here.
USB Ethernet singleton = 124c6080. At singleton+0x8294 the first words are
00000001 12456dd0 00000001 00000000, confirming the ASIX ops reference in
the discovery area. This does not establish RTL8153 native attachment.

## Loader constraint and next implementation

MochaPayload already injects small ARM patches into IOS-NET. Its link.ld reserves
0x12431900..0x12432000 (1792 bytes), immediately before Hokaku, which is far too
small for this core plus buffers. Do not overwrite it or use arbitrary zero RAM.
Need verified allocated memory, executable mapping, persistence across title
transitions, cache coherency and an IOS-NET execution context. Stroopwafel has
such infrastructure but requires its own boot environment; do not assume it is
installed or require changing the owner's boot chain without a concrete design.

Native USB ops adapter must bind verified UHS control/bulk APIs, implement
completion and link notifications, and preserve batch ownership/cancellation.
The existing PPC module must release the USB interface and stop hooking sockets
when native mode becomes active. Never run both USB owners concurrently.

Next: inspect post-reboot logs; establish the hang trigger; validate memory map
and interface state; finish native ops transport binding and loader design.
Wi-Fi-off/browser tests follow only after native data path installation, not
after a fake network-state report. DHCP optimization and dual-interface FTP
changes are explicitly outside current scope per user direction.

## Reference projects

- StroopwafelCFW/stroopwafel commit f2ebe5250ad6632ef7bebeeac4d43687e389f099:
  public IFMGRNCL query, IOS UID/layout and ARM patch-loading examples.
- wiiu-env/MochaPayload: IOS kernel read/write semantics, IOS-NET injection
  region and system call imports. Local checkout under upstream/MochaPayload.
