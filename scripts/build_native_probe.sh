#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export DEVKITPRO="${DEVKITPRO:-/opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro}"
export DEVKITPPC="$DEVKITPRO/devkitPPC"
export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH"
make -C vendor/libmocha -j4
make -C tools/native_probe -j4
mkdir -p dist/native-probe/wiiu/apps
wuhbtool tools/native_probe/rtl8153_native_probe.rpx dist/native-probe/wiiu/apps/rtl8153_native_probe.wuhb --name="RTL8153 Native Interface Probe" --short-name="RTL8153 Native Probe" --author="RTL8153 project"
