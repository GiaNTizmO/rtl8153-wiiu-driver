# Среда, сборки и проверки

## Среда

Windows PowerShell, корень E:/PROJECTS/usbethwiiu; WSL distro UbuntuCafeF,
путь проекта /mnt/e/PROJECTS/usbethwiiu. SDK:
/opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro, devkitARM и devkitPPC.
Установленные toolchain не менять ни при каких исправлениях проекта.

Windows Python использовался для Unicorn ARM tests и FTP. WSL python3 — relocation
и генераторы; Linux gcc — sanitizer tests. Не путать Windows gcc с Linux:
Windows MinGW здесь не поддерживает тот же -lasan/-lubsan, и aligned_alloc в ручной
Windows-сборке test_native_glue.c был недоступен даже при -std=c11. Это не
доказательство дефекта C-кода. Основной sanitizer path — gcc в WSL.

Python Unicorn доступен в Windows; его наличие в WSL не предполагать. Не ставить
новые зависимости/SDK ради первого изучения. Никакого make install.

## Локальные команды для последующей работы (не запускались при передаче)

В PowerShell из корня проекта:

    wsl -d UbuntuCafeF -- bash -lc 'cd /mnt/e/PROJECTS/usbethwiiu && bash scripts/test.sh'
    ./scripts/test_native_rx_arm.ps1

Первый скрипт сейчас включает новый test_firmware_tx.c, но полный suite после
добавления этой строки ещё не запускался. Второй пересобирает native core, сравнивает
relocation с GNU ld и запускает шесть групп owner ARM tests. Он НЕ включает
пока отдельный firmware-TX ARM integration или объединённый router test — их нет.

Для проверки только текущего ARM core:

    wsl -d UbuntuCafeF -- bash -lc 'cd /mnt/e/PROJECTS/usbethwiiu && bash scripts/build_native_core.sh && python3 tests/test_relocation.py build/native/rtl8153_ios.rel.elf /opt/wiiu-rnd/devkitppc-root/rootfs/opt/devkitpro/devkitARM/bin/arm-none-eabi-ld'

Не использовать Makefile-escape $$ в прямой bash-команде: $$ там PID, а не $.
Следить за LF у .sh: Python write_text на Windows однажды превратил их в CRLF,
что ломало set -o pipefail. Писать LF. Старый build не считать новым по наличию файла.

PPC (только когда есть задача на него):

    wsl -d UbuntuCafeF -- bash -lc 'cd /mnt/e/PROJECTS/usbethwiiu && bash scripts/build.sh'

Выходы native: build/native/librtl8153_ios_core.a и rtl8153_ios.rel.elf.
Выходы PPC: aroma_module/RTL8153Module.wms и dist/sd/.
Ни одна сборка сама не является установкой на SD/приставку.
Корень не Git repository: git status здесь завершается ошибкой; отдельные upstream
могут быть Git. Не выполнять git init/reset/clean для «исправления» без задачи.

## Что именно проверяют тесты

| Проверка | Доказательство | Не доказывает |
|---|---|---|
| scripts/test.sh | C framing/register mocks, ownership, tokens, RX/TX sanitizer lifecycle | USB hardware, execution permissions |
| test_relocation.py | bytes/branches/BSS против GNU ld на 3 базах | резервирование памяти, runtime cache |
| test_ios_map_arm.py | owner mapper, PTE/domain/neighbor handling | реальный MMU/TLB |
| test_mocha_alloc_arm.py | probe ARM, alloc/free calls/status order | hardware allocator semantics |
| test_mocha_boot_arm.py | boot patcher/section writes/guards with stubs | live reload/cache |
| test_ios_submit_close_arm.py | actual owner submit/close instructions, pending increment, cancel callsite | UHS cancel acknowledgement |
| test_ios_uhs_metadata_arm.py | real UHS constructs real DMA separately from metadata token | IOS-USB consumer/device DMA |
| test_ios_completion_dispatch_arm.py | actual worker/resolver/stack args/error ordering | native delivery hardware |
| test_ios_tokens_worker_arm.py | owner worker → compiled relocated callback | real network/queue lifetime |
| test_ios_rx_lifetime_arm.py | owner allocator/input/release geometry + counters | реальные heap/input callbacks |
| test_ios_firmware_rx_arm.py | compiled receive/callback/binding + owner allocator/pending/delivery/refill | live USB/cache/final teardown |
| test_firmware_tx.c | caller-owned TX repack/short/error/cancel host model | firmware-TX binding/router integration |

ARM tests pin evidence/firmware-5.5.6/analysis/ios.elf hash and load its segments.
Missing private owner firmware means test unavailable; do not silently skip and
report a pass. No copying that firmware into a public package.

## Последний зелёный baseline

До последних TX additions: full host suite; 307 relocation oracle; seven RX lifetime
cases, 16 submit/close cases, 4 UHS metadata cases, 27 dispatch cases, 8 cases/16
transfers compiled token worker, 8 integrated RX cases. Standalone firmware TX
sanitizer test then passed. Current TX binding/router source is newer and untested.
Любой новый результат обязан иметь команду, exit status и точный coverage.
