#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
task_sdk="${DEVKITPRO:-/opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro}"
task_arm="$task_sdk/devkitARM/bin/arm-none-eabi"
mkdir -p build/native
flags=(-std=c11 -Wall -Wextra -Werror -O2 -g -mcpu=arm926ej-s -marm -mbig-endian -mfloat-abi=soft -ffreestanding -fno-builtin -ffunction-sections -fdata-sections)
"$task_arm-gcc" "${flags[@]}" -c native/rtl8153_ios_core.c -o build/native/rtl8153_ios_core.o
"$task_arm-gcc" "${flags[@]}" -c native/frame_adapter.c -o build/native/frame_adapter.o
"$task_arm-gcc" "${flags[@]}" -c native/batch_bridge.c -o build/native/batch_bridge.o
"$task_arm-gcc" "${flags[@]}" -c native/uhs_transport.c -o build/native/uhs_transport.o
"$task_arm-gcc" "${flags[@]}" -c native/transfer_owner.c -o build/native/transfer_owner.o
"$task_arm-gcc" "${flags[@]}" -c native/completion_bridge.c -o build/native/completion_bridge.o
"$task_arm-gcc" "${flags[@]}" -c native/completion_tokens.c -o build/native/completion_tokens.o
"$task_arm-gcc" "${flags[@]}" -c native/firmware_rx.c -o build/native/firmware_rx.o
"$task_arm-gcc" "${flags[@]}" -c native/firmware_rx_binding.c -o build/native/firmware_rx_binding.o
"$task_arm-gcc" "${flags[@]}" -c native/firmware_io.c -o build/native/firmware_io.o
"$task_arm-gcc" "${flags[@]}" -c native/firmware_tx_binding.c -o build/native/firmware_tx_binding.o
"$task_arm-gcc" "${flags[@]}" -c native/firmware_tx.c -o build/native/firmware_tx.o
"$task_arm-gcc" "${flags[@]}" -c native/ios_completion_adapter.c -o build/native/ios_completion_adapter.o
"$task_arm-gcc" "${flags[@]}" -c native/ops_table.c -o build/native/ops_table.o
"$task_arm-gcc" "${flags[@]}" -c native/discovery_guard.c -o build/native/discovery_guard.o
"$task_arm-gcc" "${flags[@]}" -c native/native_glue.c -o build/native/native_glue.o
"$task_arm-gcc" "${flags[@]}" -c native/freestanding_memory.c -o build/native/freestanding_memory.o
"$task_arm-gcc" "${flags[@]}" -c driver/rtl8153_framing.c -o build/native/rtl8153_framing.o
# Recreate the archive: ar rcs alone retains obsolete loader-side members.
rm -f build/native/librtl8153_ios_core.next.a
"$task_arm-ar" rcs build/native/librtl8153_ios_core.next.a build/native/rtl8153_ios_core.o build/native/frame_adapter.o build/native/batch_bridge.o build/native/uhs_transport.o build/native/transfer_owner.o build/native/completion_bridge.o build/native/completion_tokens.o build/native/firmware_rx.o build/native/firmware_rx_binding.o build/native/firmware_tx.o build/native/firmware_tx_binding.o build/native/firmware_io.o build/native/ios_completion_adapter.o build/native/ops_table.o build/native/discovery_guard.o build/native/native_glue.o build/native/freestanding_memory.o build/native/rtl8153_framing.o
mv build/native/librtl8153_ios_core.next.a build/native/librtl8153_ios_core.a
"$task_arm-readelf" -h build/native/rtl8153_ios_core.o
# Relocatable code image; no fixed load address or console installation.
"$task_arm-ld" -EB -r build/native/rtl8153_ios_core.o build/native/frame_adapter.o build/native/batch_bridge.o build/native/uhs_transport.o build/native/transfer_owner.o build/native/completion_bridge.o build/native/completion_tokens.o build/native/firmware_rx.o build/native/firmware_rx_binding.o build/native/firmware_tx.o build/native/firmware_tx_binding.o build/native/firmware_io.o build/native/ios_completion_adapter.o build/native/ops_table.o build/native/discovery_guard.o build/native/native_glue.o build/native/freestanding_memory.o build/native/rtl8153_framing.o -o build/native/rtl8153_ios.rel.elf
"$task_arm-objcopy" --strip-debug build/native/rtl8153_ios.rel.elf
if "$task_arm-nm" -u build/native/rtl8153_ios.rel.elf | grep -q .; then
    echo "native core has unresolved external symbols" >&2
    "$task_arm-nm" -u build/native/rtl8153_ios.rel.elf >&2
    exit 1
fi
"$task_arm-readelf" -r build/native/rtl8153_ios.rel.elf > build/native/relocations.txt
