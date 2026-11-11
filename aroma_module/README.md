# RTL8153 Aroma module

Current implementation, installation and limitations: ../README.md and
../docs/VALIDATION.md. The inherited AX88179 README was historical and
did not describe this RTL8153 port.

Build through ../scripts/build.sh. Output: RTL8153Module.wms.
SHIM=1 enables GAME + Wii U Menu nsysnet interception after DHCP; SHIM=0
is for driver/lwIP diagnostics. Only install one variant at a time.

Source base: wozt/ax88179_aroma_driver, GPL-3.0, commit
558f3a6a4c995590c725dce08eca009e192cff88. Hardware-driver code is replaced.
