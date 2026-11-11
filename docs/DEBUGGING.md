# TCP syslog / IOPShell — console owner protocol

PC: 192.168.31.14, TCP listener port 7965. Console observed endpoints:
192.168.31.144 (native Wi-Fi) and 192.168.31.13 (RTL8153 private stack).
Console initiates TCP to the PC after a UDP beacon to port 4445 containing
WIIU_TCP_SYSLOG_BEACON_V1. Heartbeat from PC: one null byte per second.
Commands are ASCII plus CRLF. Run only one receiver on this port.

scripts/wiiu_tcp.py implements this protocol using only Python stdlib.
Active receiver for this session:

    python -u scripts/wiiu_tcp.py --console 192.168.31.144 --console 192.168.31.13 --directory evidence/2026-11-09/iopshell

Log: evidence/2026-11-09/iopshell/console.log. Command queue: commands.txt in
the same directory. Send one command, await its response, then proceed.
COMMAND_SENT proves socket transmission only; an echo alone is not completion.
Do not automatically repeat unanswered commands. Owner guide requires at
least eight seconds between coretrace commands. No automated console reboot,
title launch, memory write, or debug-mode change without explicit permission.
No FTP during a running game/loading/hang; confirm title closed first.
Logs may contain console identifiers: retain locally, do not package/publish.

## Captured failure: Native Probe first display — 2026-11-09

Receiver connected from 192.168.31.144 and retrieved buffered system logs.
At console uptime 00:01:19.493, process 15 Core1 suffered invalid access:

    SRR0 0103B0D4 coreinit.rpl|OSScreenClearBufferEx+0x44
    DAR  00384000
    WHBLogConsoleDraw+0x38 -> inspect+0x68 -> main+0x8c

Disassembly of the matching local executable places inspect+0x68 immediately
after its first WHBLogConsoleDraw and before Mocha_InitLibrary. Therefore the
failure occurred while drawing the first diagnostic line, before IOS-NET reads.
This is a probe graphics bug, not evidence that the IOS-NET ABI or RTL8153
device transport failed. The exact reason screen allocation/setup was invalid
has not been established. Headless probe revision 2 removes WHBGfx, WHB console
and OSScreen calls; it emits OSReport and SD logs instead. After owner menu
confirmation, the revision 2 WUHB was uploaded and verified by readback SHA256:
80BDD4C63FB77BF62C55511436726FB1724A7B4C09B69ABDEB527B36EB7D1E16.
At PC time 02:36:00 it entered main; at 02:36:03 it completed inspection with
22/22 code fingerprints and the ASIX ops table matching. No graphics crash
occurred during this inspection. TCP logs subsequently show Wii U Menu startup
at PC time 02:36:47; no probe graphics crash appears during that return.

Separate owner report: earlier System Settings -> Menu transition hung without
ever launching Native Probe. This remains unresolved. Need a captured trace
of that transition rather than attributing it to the probe display crash.

One cos coretrace was sent at PC log time 02:28:40; only command echo has been
observed, no core trace response. The fatal application already exited and
system logs report an unrecoverable COS error. Do not queue more user-mode
commands into this failed state. Console can be manually rebooted by owner.

## Captured failure: PPC recovery timer accumulation — 2026-11-09

At PC time 02:42:12, console uptime 00:10:23.432, resident module panicked:

    RTL8153: sys_timeout: timeout != NULL, pool MEMP_SYS_TIMEOUT is empty
    OSPanic on thread RTL8153 network
    ax_net_start+0x16c -> recover_adapter.part.0+0x88 -> run_network+0x97c

Beforehand UHS release/reacquire repeated about every 36 seconds. Independent
of the reason each recovery failed to obtain an address, setup_netif added a
self-rescheduling beat_cb timer on each successful setup, and stop_netif never
removed it. Thus failed recoveries accumulated one permanent diagnostic timer
each until the 16-entry pool exhausted. This is not DHCP latency optimization.

Fix: net/port/net_heartbeat.c owns exactly one diagnostic timer; setup removes
any old chain, stop cancels it under the core lock, and title-forget clears its
state after old thread/pools are abandoned. tests/test_heartbeat.c uses the real
lwIP timeout implementation with a 16-entry allocator and 1000 recovery cycles;
peak live heartbeat allocations = 1 and final live allocations = 0.

Patched WMS built locally; not uploaded. No FTP, reboot or shell memory writes
were performed after observing this fatal state. Need owner-confirmed menu
after reboot before deploying the replacement. Earlier Settings transition
hang has not been proven to share this cause.
