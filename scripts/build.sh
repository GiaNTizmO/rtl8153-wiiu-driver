#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export DEVKITPRO="${DEVKITPRO:-/opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro}"
export DEVKITPPC="$DEVKITPRO/devkitPPC"
export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH"
jobs="${JOBS:-4}"
if [[ "${CLEAN:-0}" == 1 ]]; then
    make -C aroma_module clean
fi
for dep in wums libmocha functionpatcher; do
    make -C "vendor/$dep" -j"$jobs"
done
make -C aroma_module SHIM="${SHIM:-1}" -j"$jobs"
mkdir -p dist/sd/wiiu/environments/aroma/modules dist/sd/wiiu/rtl8153
cp aroma_module/RTL8153Module.wms dist/sd/wiiu/environments/aroma/modules/
cp aroma_module/config.ini dist/sd/wiiu/rtl8153/
