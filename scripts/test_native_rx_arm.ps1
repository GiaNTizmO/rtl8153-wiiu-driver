# Offline only. Uses installed devkitARM read-only through the owner's WSL.
# Owner firmware evidence must already be present; no network or console calls.
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
wsl -d UbuntuCafeF -- bash -lc 'cd /mnt/e/PROJECTS/usbethwiiu && bash scripts/build_native_core.sh && python3 tests/test_relocation.py build/native/rtl8153_ios.rel.elf /opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro/devkitARM/bin/arm-none-eabi-ld'
if($LASTEXITCODE -ne 0){throw 'ARM build/relocation failed'}
foreach($test in @('test_ios_rx_lifetime_arm.py','test_ios_submit_close_arm.py',
    'test_ios_uhs_metadata_arm.py','test_ios_completion_dispatch_arm.py',
    'test_ios_tokens_worker_arm.py','test_ios_firmware_rx_arm.py')){
    python (Join-Path 'tests' $test)
    if($LASTEXITCODE -ne 0){throw "ARM test failed: $test"}
}
