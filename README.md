# RTL8153 USB Ethernet for Wii U (Aroma)

**English** | [Русский](README.ru.md)

Wired Ethernet for the Wii U through Realtek **RTL8153 / RTL8153B** USB
adapters (VID:PID `0BDA:8153`). Target: Wii U 5.5.6 with a current
[Aroma](https://aroma.foryour.cafe/) environment.

## How it works today

The driver is **not system-wide yet**. It is an Aroma module,
`RTL8153Module.wms`, that runs on the PowerPC side next to the running title:

```
title / game / Wii U Menu
        │  socket(), connect(), sendto(), recvfrom(), select(), DNS …
        ▼
nsysnet hooks (FunctionPatcher)  ──►  own lwIP stack (DHCP, ARP, TCP, UDP, DNS)
                                            │
                                            ▼
                                UHS (USB host) → RTL8153 → cable → router
```

How it is set up:
- The module finds the adapter, initialises the RTL8153, brings up the link
  and obtains **its own IP address via DHCP**. The MAC address is read from the
  adapter.
- Sockets created by the running title (the GAME process) or by the Wii U Menu
  are intercepted and go over the cable. Sockets created before the hooks were
  installed, and the ROOT_RPX process, stay on the console's normal network.
- The console's system network stack (IOS-NET) **does not know about the
  adapter**. System settings, system DHCP, Nintendo background services and
  everything else that runs inside IOS keep using Wi-Fi.
- **Keep Wi-Fi configured.** Many titles check connectivity through the system
  stack before they start networking.
- By default the module requests a fresh DHCP lease after every title switch
  (`mode=always`).

### Tested on hardware

- DHCP address, ping replies, FTP.
- The browser opens websites.
- Switching from a title back to the Menu.

### Not tested or not supported

- Running without Wi-Fi.
- System services, eShop, Download Manager.
- Every game. Moonlight and other streaming clients have not been tested.
- Throughput has not been measured. USB 2.0 tops out at 480 Mbit/s before
  overhead; a gigabit RJ45 link does not mean gigabit traffic.
- Nintendo's old TLS certificate store causes warnings on some websites.
- vWii, IPv6, RTL8156 and other VID:PIDs are not supported.

## Installation (PPC module)

Download `RTL8153Module-<version>-sd.zip` from the
[Releases](https://github.com/GiaNTizmO/rtl8153-wiiu-driver/releases) page.

1. Extract the archive to the root of the SD card. It contains
   `wiiu/environments/aroma/modules/RTL8153Module.wms` and
   `wiiu/rtl8153/config.ini`. If you already have a tuned `config.ini`, keep it.
2. The Aroma **FunctionPatcher** and **Mocha** modules are required. Do not
   run AX88179Module or any other module that hooks networking at the same time.
3. Plug the adapter into the Wii U before booting, and connect the cable to a
   router with DHCP.
4. The log is written to `SD:/wiiu/rtl8153/driver.log`.

To uninstall, delete the `.wms` from the SD card and reboot. Nothing is written
to NAND: the UHS patch is applied in RAM only, after the original instruction
has been verified.

### Configuration (`config.ini`)

| Key | Meaning |
|---|---|
| `[dhcp] mode` | `always` — new lease after each title switch; `keep_first` — reuse the first IP |
| `[compat] route` | `ax` — new sockets use the adapter; `native` — the console's network |
| `[compat] dns` | `ax` — DNS over the adapter; `system` — system DNS |
| `[compat] nssl` | `bridge` — route Nintendo TLS (NSSL) over the adapter |
| `[debug] shim_trace` | `0` off, `1` general, `2` verbose socket log |

`ax` is a mode name inherited from the original AX88179 project; here it means
the RTL8153/lwIP path.

## Native IOS-NET driver (in development)

The end goal is a real system driver inside IOS-NET (ARM). The adapter would
then work as the console's own LAN interface for the whole system: system DHCP,
settings, every title, no Wi-Fi needed. **This path does not work yet and
cannot be installed.**

| Part | State |
|---|---|
| ARM RTL8153 core, framing, RX/TX adapters, completion tokens | Source; host tests and ARM emulation against the real 5.5.6 firmware |
| TX binding / I/O router | Source, not yet tested |
| Loading code into IOS-NET (Aroma/Mocha) | Memory allocation inside NET verified on hardware. Executing code from the NET heap is prepared as a diagnostic candidate, not verified on hardware |
| Ops registration, USB handoff from PPC, system DHCP | Not started on hardware |

Details (in English and Russian):
- [docs/handoff/README.md](docs/handoff/README.md) — current state;
- [docs/NATIVE_LOADER_DESIGN.md](docs/NATIVE_LOADER_DESIGN.md) — loader;
- [docs/MOCHA_ENTRY_DIAGNOSTIC.md](docs/MOCHA_ENTRY_DIAGNOSTIC.md) — entry diagnostic;
- [docs/NATIVE_COMPLETION_ABI.md](docs/NATIVE_COMPLETION_ABI.md) — completion ABI.

The Mocha diagnostic builds replace the boot payload. Do not install them
without understanding the recovery procedure.

## Building

You need devkitPro (devkitPPC, devkitARM, wut) and Linux/WSL.

```bash
git clone --recursive https://github.com/GiaNTizmO/rtl8153-wiiu-driver.git
```

If you cloned without `--recursive`:

```bash
git submodule update --init
```

Build the PPC module (output in `dist/sd/`):

```bash
DEVKITPRO=/opt/devkitpro bash scripts/build.sh
```

GitHub Actions (`.github/workflows/build.yml`) runs the same build in the
`ghcr.io/wiiu-env/devkitppc` image on every push to `main` and on pull
requests; the built module is kept as a workflow artifact.

### Releasing a version

```bash
git tag v0.2.0
git push origin v0.2.0
```

A `v*` tag makes CI build the module with that version (shown in Aroma) and
create a GitHub Release with:
- `RTL8153Module-<version>-sd.zip` — ready-to-copy SD layout;
- `RTL8153Module.wms`;
- `SHA256SUMS.txt`.

A tag with a hyphen (`v0.2.0-beta1`) is published as a pre-release.

Host tests (gcc with ASan/UBSan):

```bash
bash scripts/test.sh
```

Native driver ARM core:

```bash
bash scripts/build_native_core.sh
```

The scripts build dependencies only inside the project's `vendor/` and
`build/`: there is no `make install` and the toolchain is never modified. The
ARM emulation tests need Python with Unicorn and **your own** dump of the 5.5.6
IOSU firmware. The dump is not part of this repository and never will be.

## Layout

| Path | Contents |
|---|---|
| `aroma_module/` | WUMS module: lifecycle, nsysnet hooks, UHS patch, logging |
| `driver/` | UHS transport and RTL8153 registers/initialisation (PPC) |
| `net/` | lwIP integration, timers, sys_arch |
| `native/` | ARM code for IOS-NET, ABI, RX/TX adapters, loader and diagnostics |
| `tools/` | Read-only console probes (native probe, boot/entry state) |
| `scripts/`, `tests/` | Build, checks, host tests and ARM emulation |
| `docs/` | Research, ABI, project state and decision log |
| `references/` | Reference drivers (Linux r8152, OpenBSD/FreeBSD ure, U-Boot) |
| `vendor/` | lwIP; submodules WUMS, libmocha, libfunctionpatcher |
| `upstream/` | Submodules MochaPayload, Stroopwafel, libstroopwafel, AX88179 Aroma Driver |

## Credits and licences

- Aroma module base: [wozt/ax88179_aroma_driver](https://github.com/wozt/ax88179_aroma_driver) (GPL-3.0).
- RTL8153 initialisation follows [OpenBSD ure(4)](https://github.com/openbsd/src/blob/master/sys/dev/usb/if_ure.c)
  (BSD; notices kept in `driver/`). Linux `r8152` used as a reference.
- [lwIP](https://savannah.nongnu.org/projects/lwip/) (BSD); changes are listed in `vendor/lwip/UPSTREAM.md`.
- [wiiu-env](https://github.com/wiiu-env): WUMS, libmocha, libfunctionpatcher, MochaPayload.
- [Stroopwafel](https://github.com/StroopwafelCFW/stroopwafel).

This project is licensed under **GPL-3.0** (see [LICENSE](LICENSE)).
Third-party code keeps its own licence.

Not affiliated with Nintendo or Realtek. Use at your own risk.
