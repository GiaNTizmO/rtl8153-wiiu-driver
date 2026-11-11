#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Use the existing bundled devkitARM only. This probe is intentionally built
# separately from the driver core so it can be validated before any native
# USB ownership or IOS-NET dispatch is changed.
task_sdk="${DEVKITPRO:-/opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro}"
task_arm="$task_sdk/devkitARM/bin/arm-none-eabi"
mkdir -p build/native

flags=(-std=c11 -Wall -Wextra -Werror -O2 -g -mcpu=arm926ej-s -marm
       -mbig-endian -mfloat-abi=soft -ffreestanding -ffunction-sections
       -fdata-sections)

"$task_arm-gcc" "${flags[@]}" -c native/entry_probe.c \
    -o build/native/entry_probe.o
"$task_arm-ld" -EB -r build/native/entry_probe.o \
    -o build/native/entry_probe.rel.elf
"$task_arm-objcopy" --strip-debug build/native/entry_probe.rel.elf
"$task_arm-readelf" -r build/native/entry_probe.rel.elf \
    > build/native/entry_probe.relocations.txt

echo "native entry probe object: build/native/entry_probe.rel.elf"
