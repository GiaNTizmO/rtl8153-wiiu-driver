#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export DEVKITPRO="${DEVKITPRO:-/opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro}"
export DEVKITPPC="$DEVKITPRO/devkitPPC"
export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH"
python3 scripts/make_mocha_boot_profile.py --candidate-dir build/mocha-entry-experiment --out build/mocha-entry-state
make -C vendor/libmocha -j4
make -C tools/mocha_entry_state -j4
mkdir -p dist/mocha-entry-state/wiiu/apps
wuhbtool tools/mocha_entry_state/rtl8153_mocha_entry_state.rpx \
  dist/mocha-entry-state/wiiu/apps/rtl8153_mocha_entry_state.wuhb \
  --name="RTL8153 Mocha Entry State" --short-name="RTL8153 Entry State" --author="RTL8153 project"
