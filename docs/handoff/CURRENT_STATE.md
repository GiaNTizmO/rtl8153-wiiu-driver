# Текущее состояние

## Цель владельца

Полноценный системный RTL8153B Ethernet на Wii U 5.5.6 с актуальной Aroma через
USB 2.0, включая штатный IOS-NET, системный DHCP и работу приложений без необходимости
держать Wi-Fi. Существующий PPC shim — промежуточный работающий путь, не окончательный
результат. Замена boot environment на Stroopwafel не согласована и не требуется.

## Устройство

- Realtek RTL8153B, USB VID:PID 0BDA:8153.
- TCR 0x6010, bcdDevice 0x3100. USB revision не равна silicon TCR.
- Заводской MAC 00:E0:4C:68:07:2B, считывается из адаптера, не зашит.
- USB 2.0 High Speed; RJ45 link наблюдался 1000 Mbps full duplex.
- Это не обещает 1 Gbit/s полезного трафика: USB2 имеет 480 Mbit/s до overhead.

## Что проверено на приставке

PPC WUMS-модуль с собственным lwIP получил 192.168.31.13, отвечал на ping,
обслуживал FTP, браузер открыл example.com. Владелец наблюдал рабочие переходы
приложение → Menu после исправлений. Старая CA-база вызывает предупреждение TLS;
обновление сертификатов отложено. Это не доказывает native Ethernet или все игры.

Read-only headless Native Probe совпала с 22 fingerprints/ASIX ops.
Loader Capability Probe получила IOS_Open=-6 для /dev/stroopwafel: backend отсутствует.

Отдельный согласованный Mocha allocation diagnostic после iosu reload выполнил
ограниченный NET allocation/data/free путь; точные результаты в EVIDENCE.md.
Затем официальный Mocha восстановлен через SD, после холодного старта и нового
Boot State получена official runtime identity. Это последнее аппаратное состояние;
нынешнюю доступность консоли/SD нужно уточнять заново, а не выводить из этой записи.

## Что существует локально

- RTL register core ARM926 big endian, framing, RX parsing/TX repacking.
- UHS control/bulk wrappers, endpoint cancel request, owner/retained-RX logic.
- completion_tokens: immutable metadata/cookie restore через opaque token,
  настоящее шестипараметровое firmware callback entry, защита duplicate/stale.
- firmware_rx + firmware_rx_binding: caller-owned native batch, выровненный
  bounce DMA, публикация в исходный inline buffer, real pending +/-1,
  deferred delivery для синхронного firmware refill и final drain.
- firmware_tx: caller-owned batch, перепаковка, pending, short/error/cancel result,
  deferred done/drain. Его standalone ASan/UBSan тест прошёл до паузы.
- Relocator, loader contracts, mapping ARM emulation, отдельный entry probe.
- ops table builder и discovery read/compare/write transaction; не активированы.

## Последний подтверждённый checkpoint сборки

Последний полный прогон host sanitizer suite и scripts/test_native_rx_arm.ps1 прошёл
ДО добавления firmware_tx_binding.c/h и firmware_io.c/h.
Тогда ARM core включал firmware_rx/binding, completion_tokens и старые модули:
307 relocations на трёх базах совпали с GNU ld; прошли 8 связанных firmware-RX ARM
сценариев, включая real pacing/refill, и дополнительные ABI/lifetime тесты.

Затем добавлен firmware_tx.c/h и standalone tests/test_firmware_tx.c: этот тест
прошёл ASan/UBSan отдельной командой. Затем добавлены firmware_tx_binding.c/h и
firmware_io.c/h, обновлены scripts/build_native_core.sh и scripts/test.sh для TX.
ЭТИ ПОСЛЕДНИЕ ИЗМЕНЕНИЯ НЕ СОБРАНЫ И НЕ ПРОВЕРЕНЫ после добавления.
Текущий build/native/rtl8153_ios.rel.elf старее этих исходников. Не представлять его
как сборку текущего дерева и не использовать старый count relocations как новый результат.

## Что ещё отсутствует/не доказано

- Полный loader Aroma/Mocha: выделение постоянной памяти под образ, права/domain,
  coherency, bounded execution/return и lifetime на реальном IOS-NET.
- Общий проверенный firmware RX/TX controller lifecycle и teardown.
- Native discovery/open/endpoint claim/setup, link notification/timer, error recovery.
- Передача USB от PPC к IOS-NET и остановка socket hooks PPC.
- Установка native ops, штатный DHCP, системный трафик без Wi-Fi, hardware stability.

Сейчас это исходники и промежуточные артефакты разработки. Installable native package
нет. Существующие dist/mocha-allocation-review и .zip — старый диагностический комплект,
не дистрибутив native RTL-драйвера.
