#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export DEVKITPRO="${DEVKITPRO:-/opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro}"
export DEVKITPPC="$DEVKITPRO/devkitPPC"
export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH"
python3 scripts/make_mocha_boot_profile.py
make -C vendor/libmocha -j4
make -C tools/mocha_boot_state -j4
mkdir -p dist/mocha-boot-state/wiiu/apps
wuhbtool tools/mocha_boot_state/rtl8153_mocha_boot_state.rpx \
  dist/mocha-boot-state/wiiu/apps/rtl8153_mocha_boot_state.wuhb \
  --name="RTL8153 Mocha Boot State" --short-name="RTL8153 Boot State" --author="RTL8153 project"
