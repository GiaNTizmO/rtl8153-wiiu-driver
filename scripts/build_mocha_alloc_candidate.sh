#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
task_sdk="${DEVKITPRO:-/opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro}"
task_arm="$task_sdk/devkitARM/bin/arm-none-eabi"
task_out=build/mocha-alloc-candidate
mkdir -p "$task_out"
flags=(-std=c11 -Wall -Wextra -Werror -Os -mcpu=arm926ej-s -marm
       -mbig-endian -mfloat-abi=soft -ffreestanding -fno-builtin)
"$task_arm-gcc" "${flags[@]}" -c native/mocha_alloc_probe.c -o "$task_out/probe.o"
"$task_arm-gcc" "${flags[@]}" -c native/mocha_alloc_probe_entry.S -o "$task_out/entry.o"
# Link the existing DLP patch functions too: never assume this range is empty.
"$task_arm-gcc" "${flags[@]}" -Iupstream/MochaPayload/source/ios_net/source \
    -c upstream/MochaPayload/source/ios_net/source/main.c -o "$task_out/dlp.o"
cat > "$task_out/candidate.ld" <<'EOF'
INCLUDE "imports.ld"
PROVIDE(rtl_net_original_init = 0x12300a74);
PROVIDE(rtl_net_alloc = 0x123e4394);
PROVIDE(rtl_net_free = 0x123e439c);
PROVIDE(rtl_net_printf = 0x123ccfbc);
SECTIONS {
    . = 0x12431900;
    .text : { *(.text*) *(.rodata*) *(.data*) }
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    .rtl_alloc_status 0x1288d040 (NOLOAD) : { *(.rtl_alloc_status) }
    /DISCARD/ : { *(.ARM.attributes) *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.bss) == 0, "candidate requires unreserved BSS");
ASSERT(SIZEOF(.rtl_alloc_status) == 64, "status ABI mismatch");
ASSERT(ADDR(.text) + SIZEOF(.text) < 0x12432000, "candidate overlaps Hokaku");
EOF
"$task_arm-ld" -EB -L upstream/MochaPayload/source/ios_net \
    -z noexecstack -T "$task_out/candidate.ld" "$task_out/dlp.o" "$task_out/probe.o" "$task_out/entry.o" \
    -Map "$task_out/candidate.map" -o "$task_out/candidate.elf"
"$task_arm-objdump" -d -m armv5te "$task_out/candidate.elf" > "$task_out/candidate.disasm.txt"
"$task_arm-size" "$task_out/candidate.elf"
"$task_arm-nm" -n "$task_out/candidate.elf" > "$task_out/candidate.symbols.txt"
python3 scripts/check_mocha_alloc_candidate.py "$task_out/candidate.elf"
python3 tests/test_mocha_alloc_candidate.py
echo 'OFFLINE ONLY: no boot payload patched, packaged or installed.'
