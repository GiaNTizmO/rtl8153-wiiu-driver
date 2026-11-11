#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
task_sdk="${DEVKITPRO:-/opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro}"
export DEVKITPRO="$task_sdk"
export DEVKITPPC="${DEVKITPPC:-$task_sdk/devkitPPC}"
export PATH="$DEVKITPPC/bin:$task_sdk/tools/bin:$PATH"
make -C tools/loader_capability_probe -j4
mkdir -p dist/loader-capability-probe/wiiu/apps
wuhbtool tools/loader_capability_probe/rtl8153_loader_capability_probe.rpx \
  dist/loader-capability-probe/wiiu/apps/rtl8153_loader_capability_probe.wuhb \
  --name="RTL8153 Loader Capability Probe" \
  --short-name="RTL8153 Loader Probe" \
  --author="RTL8153 project"
