#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export DEVKITPRO="${DEVKITPRO:-/opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro}"
export DEVKITPPC="$DEVKITPRO/devkitPPC"
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITPPC/bin:$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
task_root="$PWD"
make -C upstream/libstroopwafel -j4
make -C vendor/libmocha -j4
# LIBDIRS is passed only to this project build; no install or SDK writes.
make -C upstream/MochaPayload -j4 \
    LIBDIRS="$DEVKITPRO/wut $DEVKITPRO/wut/usr $task_root/vendor/libmocha $task_root/upstream/libstroopwafel"
echo 'LOCAL BASELINE ONLY: no console upload or boot replacement.'
