#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export DEVKITPRO="${DEVKITPRO:-/opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro}"
export DEVKITPPC="$DEVKITPRO/devkitPPC"
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITPPC/bin:$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
task_root="$PWD"
task_source="build/mocha-entry-experiment"
task_commit="383a334fc3e304d403dd69b994baaee3752c01dc"
if [[ "$(git -C upstream/MochaPayload rev-parse HEAD)" != "$task_commit" ]]; then
    echo 'Unexpected Mocha source revision; review the experiment before building.' >&2
    exit 1
fi
make -C upstream/libstroopwafel -j4
make -C vendor/libmocha -j4
# This directory is generated experiment output. Refresh tracked upstream
# sources before applying the overlay; an existing directory is not evidence
# that it incorporates the current probe/guard. Retain build intermediates.
mkdir -p "$task_source"
git -C upstream/MochaPayload archive HEAD | tar -x -C "$task_source"
python3 scripts/prepare_mocha_alloc_experiment.py --variant entry "$task_source"
make -C "$task_source" -j4 \
    LIBDIRS="$DEVKITPRO/wut $DEVKITPRO/wut/usr $task_root/vendor/libmocha $task_root/upstream/libstroopwafel"
python3 scripts/check_mocha_entry_candidate.py "$task_source/source/ios_net/ios_net.elf"
python3 tests/test_mocha_boot_guard.py "$task_source"
echo 'EXPERIMENTAL HEAP-ENTRY BUILD ONLY: not uploaded, not approved for console boot.'
