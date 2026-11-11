# Validation status — 2026-11-09

Target: Wii U 5.5.6, latest Aroma as reported by the owner. Initial hardware
bring-up is now partially confirmed; application socket routing is not yet
confirmed. Importing a tested AX88179 networking layer does not establish
RTL8153 game compatibility.

## Initial console evidence — 2026-11-09

- Owner installed the module; router reports hostname wiiu-rtl8153, address
  192.168.31.13 and adapter MAC 00:E0:4C:68:07:2B. This supports successful DHCP.
- Assistant ran Windows ping from the development PC: 4 sent, 4 received,
  0% loss; minimum 3 ms, maximum 29 ms, average 9 ms; reply TTL 255.
- Windows neighbor table on the PC Wi-Fi interface confirms 192.168.31.13
  resolves to 00-E0-4C-68-07-2B and is Reachable. A separate unreachable
  neighbor entry on the PC Ethernet interface does not negate the successful
  Wi-Fi path.
- These observations confirm a working basic Ethernet data path, ARP and ICMP
  through this adapter. They do not prove DNS, TCP, UDP application routing,
  Nintendo TLS, game compatibility or full system network replacement.
- Exact TCR, negotiated speed and hook installation require driver.log.

## FTP inspection — 2026-11-09

- Retrieved driver.log and config.ini read-only from the console through FTP
  control endpoint 192.168.31.144:21. Anonymous login succeeds.
- Passive mode advertises 192.168.31.144 and times out opening the data
  connection. Active FTP to PC 192.168.31.14 succeeds; curl reports the
  incoming data connection originates from 192.168.31.13. This establishes
  an application TCP data transfer through the RTL8153 path.
- Direct FTP control connection to 192.168.31.13:21 is refused; the old
  native listener remains on Wi-Fi. Console config has ftp_handoff=off.
- Hardware log identifies TCR=6010 (RTL8153B), bulk maxpacket=512, factory
  MAC 00:e0:4c:68:07:2b, guarded IOSU patch applied, and 1000 Mbps full-duplex
  Ethernet link. The USB transport remains USB 2.0.
- Latest boot: open 370 ms, net startup 3 ms, DHCP lease after approximately
  12.7 s, 36 shim hooks installed, and title traffic routed through RTL8153.
- Earlier log sections include DHCP timeout/recovery and one subsequent
  title transition without confirmed DHCP completion. Lifecycle reliability,
  DNS, TLS and real game Internet use remain unvalidated.
- Original downloaded evidence: evidence/2026-11-09/driver.log and
  evidence/2026-11-09/console-config.ini. No console files were changed.

## Recovery-fix hardware follow-up — 2026-11-09

Owner reports 16 minutes in Menu with the system responsive after installing
0.1.1-recovery-fix. Read-only active FTP then retrieved driver.log and the
installed RTL8153Module.wms. Installed file SHA256 matches the local 0.1.1
artifact: 3168679E6D6BB8439C728CD411F486167DF20545E448379432A67364D791C666.
Driver log confirms RTL8153B TCR 6010, maxpacket 512, 1000 Mbps full duplex,
DHCP 192.168.31.13 and 36 shim hooks. PC ping: 4/4 replies, 3–5 ms.
No recovery loop is present in this retrieved driver log. Thus this is a
successful idle/network smoke check, not hardware proof that repeated
recovery no longer leaks timers. A controlled title transition remains next;
native IOS-NET installation and Wi-Fi-off operation remain unfinished.

## Mocha runtime identity — 2026-11-09

The owner launched the headless RTL8153 Boot State WUHB, waited, closed it and
confirmed Menu. Its SD report was downloaded through active FTP and retained
at evidence/2026-11-09/mocha-boot-state/runtime.log. It contains:

    reload hook=eb003005 NET init call=ebffffe2
    Mocha kernel text: official=MATCH allocation_candidate=no
    Mocha NET text: official=MATCH allocation_candidate=no
    NET allocation hook absent.
    Inspection complete. HOME -> Close to return.

This verifies the expected official runtime kernel/NET code and reload branch
at that observation. It does not prove a reload occurred, an allocation worked
or native driver entry was executed. No boot/module file was changed.

## Loader geometry and real ARM mapper checks — 2026-11-09

The shared loader contract now refuses entry/result bounds errors before
advancing its phase. Result storage must be aligned, fully within the mapping
and outside the image; entry must include a complete ARM word within the image.
Unknown backends and undefined mapping flags are rejected. Tests also assert
invalid entry transitions do not modify state and invalid Stroopwafel layout
never reaches version/map/write/execute callbacks. scripts/test.sh passed after
these changes, including the existing framing/ownership/native lifecycle tests.

tests/test_ios_map_arm.py executes the pinned owner's mapper instructions in
Unicorn against synthetic page tables: 23 cases passed for page/domain/access/
cache encoding, L2 creation, errors, descriptor copying and register/stack
preservation. It demonstrates that mapping changes the whole enclosing L1
region's domain, affecting neighbors despite preserving their PTE words.
Cache/TLB operations are stubs; there is no hardware/MMU verification.

tests/test_ios_reload_command_arm.py passed eight cases on the actual Thumb
shell handler. Its title argument layout is established; its return value
masks downstream errors. Prepare/configure/relaunch calls are stubs, so these
results neither trigger nor prove a real IOS reload.

Owner then opened System Settings and returned to Menu successfully; controls
work. Retrieved driver-after-settings.log records stop, a Settings-side start
interrupted during shutdown, and fresh Menu initialization with DHCP .13 and
36 hooks. PC ping after return: 3/3, 3–9 ms. TCP log shows ROOT issuing reboot
on the Settings return and IOS uptime resetting. This validates that transition
but does not establish same-IOS-session teardown/reinitialization. The Settings
shutdown-time DHCP failure line is not an independently observed lease failure.

Owner then opened the Wii U Browser, closed the preloaded Nintendo guide, and
loaded example.com successfully. This confirms working browser DNS/TCP/HTTP
access with the current network setup. The browser displayed an expected stale
CA-certificate warning; CA database refresh is a separate future Aroma package
and is outside this iteration. Wi-Fi remained enabled, so this result does not
isolate the browser's packets from native Wi-Fi; the active module was configured
with route=ax and had a live RTL8153 lease before the test. No browser crash or
post-test OSFatal was reported.

## Local test coverage

- Existing UbuntuCafeF devkitPPC 16.1.0 cross compilation and WUMS linking.
- Native C tests under AddressSanitizer and UndefinedBehaviorSanitizer:
  framing/FCS/aggregation/order, short-frame zero padding, CRC drops, oversized
  packets, malformed lengths, truncated descriptors and 20,000 deterministic
  random malformed aggregates.
- Production register/initialization code against a USB register model:
  byte enables, bank separation, endian conversion, six TCR revisions, invalid
  MAC, unsupported revision, bounded boot timeout, injected USB failure at every
  transfer of every initialization path. The model supplies ready bits; passing
  does not validate the analog PHY or timing.
- Production UHS transport against mock CafeOS calls: sparse endpoint profiles,
  multiple interfaces, raw USB endpoint byte order, duplicate open, acquisition
  errors, guarded-patch failure, endpoint refusal, link up/down, short TX
  completion, three async RX slots, out-of-order callbacks delivered in order,
  invalid/empty completions, partial submission failure, close/cancel, title
  abandonment without UHS calls and cancellation at shutdown.
- Actual guarded IOSU patch code under a Mocha mock: expected branch,
  already-patched instruction, unexpected instruction refusal, initialization,
  read/write IPC failures and readback mismatch. No real IOSU memory was touched.

## Hardware questions still open

1. Adapter enumeration on UHS controller 0 with the expected interface.
2. Actual TCR revision of this 0bda:8153 REV_3100. Unknown silicon is rejected.
3. Guarded endpoint ownership patch on this console's IOSU.
4. PHY boot, ALDPS exit, OOB exit and autonegotiation over USB 2.
5. UHS callback lengths, ordering and cancellation behavior on the real system.
6. RX aggregation, TX USB boundary lengths, CRC and packet checksums under load.
   Firmware workarounds beyond the BSD path may be needed for some revisions.
7. DHCP DISCOVER/OFFER/REQUEST/ACK, ARP, ping, DNS, TCP, UDP and Nintendo TLS.
8. Repeated Menu → app → Menu, cable loss, USB replug, sustained traffic,
   link renegotiation and actual DHCP lease renewal/rebinding.
9. Coexistence with the owner's actual Aroma plugins, Inkay and games.

## First console session

Keep native networking configured. Install this Ethernet module alongside
normal Aroma modules. Use a DHCP router; record its lease table. Preserve
SD:/wiiu/rtl8153/driver.log. Expected markers:

1. RTL8153 TCR=...
2. RTL8153 IOSU patch: applied / already applied
3. RTL8153 link up ...
4. lease DHCP ...
5. shim ready hooks=...
6. title traffic routed through RTL8153

If initialization fails, preserve the full register/UHS error line. If DHCP
fails, capture traffic on the router or mirrored switch port. PC ping to the
adapter's own DHCP IP tests reception/transmission independently of game
socket hooking. Then test an application; the System Settings connection test
alone does not establish this module's route. Remove the .wms and reboot if
the module interferes with normal use.

Process coverage remains GAME and Wii U Menu. Native socket infrastructure is
required and higher network-state APIs are not emulated. A successful game
session would not prove full system-wide replacement.
