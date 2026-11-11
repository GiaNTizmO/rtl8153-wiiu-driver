.PHONY: all test native-core native-probe native-entry-probe loader-capability-probe native-check mocha-alloc-candidate mocha-boot-state
all:
	bash scripts/build.sh
test:
	bash scripts/test.sh
native-core:
	bash scripts/build_native_core.sh
native-probe:
	bash scripts/build_native_probe.sh
native-entry-probe:
	bash scripts/build_native_entry_probe.sh
	python3 tests/test_entry_probe.py
loader-capability-probe:
	bash scripts/build_loader_capability_probe.sh
mocha-alloc-candidate:
	bash scripts/build_mocha_alloc_candidate.sh
mocha-boot-state:
	bash scripts/build_mocha_boot_state.sh
native-check: native-core
	python3 tests/test_relocation.py build/native/rtl8153_ios.rel.elf $${DEVKITPRO:-/opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro}/devkitARM/bin/arm-none-eabi-ld
