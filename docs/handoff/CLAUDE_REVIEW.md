# Ревью Claude — первое изучение (2026-11-09)

Режим: только чтение. Исходники не менялись, сборки/тесты не запускались, консоль,
FTP и TCP-очередь не трогались. Единственная выполненная операция кроме чтения —
сверка SHA256 со snapshot.json: 164/164 файла совпадают, дерево не менялось после
передачи.

Уровни доказательства ниже: **[console]** — проверено на приставке; **[host]** —
host sanitizer-тест; **[ARM-emu]** — Unicorn с owner ios.elf; **[src]** — только
исходник, не собран и не исполнялся.

## 1. Архитектура и готовность

Есть два пути. Рабочий промежуточный: PPC WUMS → nsysnet shim → свой lwIP → UHS → RTL8153
**[console]**. Целевой: IOS-NET → ops-таблица 0x3c → ARM-код RTL → native UHS
**[ARM-emu/src]**.

Native-ядро разбито на слои:
- `completion_tokens` подменяет metadata.buffer непрозрачным токеном и восстанавливает
  исходные kind/batch/DMA/cookie в шестиаргументном callback ops+0x34.
- `firmware_rx`/`firmware_tx` — адаптеры batch, которыми владеет вызывающая сторона.
  Они используют bounce DMA и откладывают done/drain в after_delivery.
- `*_binding` вызывает реальные функции owner 5.5.6 после проверки 64-байтных
  отпечатков.
- `firmware_io` направляет kind1/kind2 одной очереди токенов в нужный адаптер.

Loader сейчас главный барьер: на консоли подтверждён только путь
allocation/data/free **[console]**. Исполнение кода из heap, mapping/domain, cache,
возврат после entry и время жизни образа не доказаны. Installable native package нет.

## 2. Компонент / доказательство / что осталось

| Компонент | Доказательство | Что осталось |
|---|---|---|
| PPC WUMS + lwIP | [console] DHCP .13, ping, FTP, браузер | Промежуточный путь; уступить USB для native |
| Native Probe / Capability Probe | [console] 22 fingerprints; stroopwafel IOS_Open=-6 | — |
| Mocha allocation diagnostic | [console] stage=4 alloc=1 data=1 free=1 после reload; official восстановлен | Не доказывает исполнение кода или retention |
| RTL core, framing, batch_bridge | [host] | Реальный USB/cache |
| uhs_transport (bulk/control/cancel) | [host] + [ARM-emu] owner submit/close | Семантика signed_result для bulk на железе |
| completion_tokens | [host] + [ARM-emu] owner worker → скомпилированный callback | Отсутствие очереди сообщений после detach на железе |
| firmware_rx + binding | [host] + [ARM-emu] 8 сценариев (real pending/rx_done/refill) | Cache maintenance, teardown, error recovery |
| firmware_tx | [host] только standalone, ASan/UBSan | Реальный 123b8034 нигде не исполнялся |
| **firmware_tx_binding** | **[src]** — ни host, ни ARM | Сборка, отпечаток e2eb4f4b, интеграция |
| **firmware_io** | **[src]** — нет ни одного теста | Объединённый RX/TX lifecycle |
| ops_table / discovery_guard | [host] | Не вызывались на консоли |
| Relocator | [ARM-emu] 307 relocations ДО добавления TX; текущий ELF устарел | Пересобрать и сравнить заново |
| Mapper 08124f88 | [ARM-emu] | Реальный MMU/TLB, соседние mappings в 1 MiB |
| Live loader / entry / ops install / USB handoff / DHCP без Wi-Fi | нет | Всё |

## 3. Замечания

### Доказанные по коду

**F1. Новые TX binding и router вообще не покрыты тестами.**
- [scripts/test.sh:50-53](../../scripts/test.sh) собирает `firmware_tx.c`, но не
  `firmware_tx_binding.c` и не `firmware_io.c`.
- Их компилирует только [scripts/build_native_core.sh:16-18](../../scripts/build_native_core.sh)
  для ARM (при передаче не запускался).
- [scripts/test_native_rx_arm.ps1](../../scripts/test_native_rx_arm.ps1) запускает
  только RX-сценарии.

Значит, router, общая очередь токенов и отпечаток TX_DONE `0xe2eb4f4b`
([firmware_tx_binding.c:41](../../native/firmware_tx_binding.c)) не исполнялись ни разу.
Неверный отпечаток не опасен (bind откажет), но и не проверен.

**F2. Если доставка падает после принятия токена, completion молча теряется.**
- [completion_tokens.c:79](../../native/completion_tokens.c) очищает слот токена до
  `deliver`.
- [completion_tokens.c:116-123](../../native/completion_tokens.c) отбрасывает
  `delivery_result` и всегда возвращает 0.
- [firmware_io.c:6-11](../../native/firmware_io.c) возвращает -1, если `rx`/`tx`
  равен NULL (partial-open), если `finishing` ещё не сброшен или если адаптер
  отказал (busy/needs_drain/cookie).

В этих случаях токен уже сожжён, но слот адаптера остаётся pending. Счётчик
controller+0x1a8/0x1ac не уменьшается, bounce DMA и batch не возвращаются, а close
никогда не дойдёт до drained. Никакого флага или диагностики нет. При правильной
связке это маловероятно, но частичная инициализация и забытый `set_after` дают
именно это.

**Исправление:** отказ доставки после принятия токена должен ставить фатальный флаг
контроллера, который виден close/teardown.

**F3. Флаг `free_error` не общий для контроллера.**
[firmware_tx_binding.c:44-46](../../native/firmware_tx_binding.c) создаёт свою копию
`RtlIosFirmwareRxBinding` через `rtl_ios_firmware_rx_bind`. Поэтому у RX и TX
раздельные `free_error`. Ошибка освобождения в TX подавляет drain
([firmware_rx_binding.c:44](../../native/firmware_rx_binding.c)) только на TX-пути.
Следующее RX completion всё равно вызовет 123b936c. Задуманное «удержание
небезопасного контроллера» так не работает. Нужен один общий binding или общий
флаг ошибки на контроллер.

### Предполагаемые риски (не доказаны)

**R1. Значение TX status и геометрия TX не совпадают со stock и не проверены.**
- [firmware_tx.c:85-86](../../native/firmware_tx.c) пишет в batch+0x3c длину после
  RTL-перепаковки. В тесте это 141 при native `transfer_bytes`=140
  ([test_firmware_tx.c:63,80](../../tests/test_firmware_tx.c)). Stock ASIX пишет
  результат USB-передачи native-буфера, то есть значение +0x40.
- Неизвестно, использует ли 123b8034 этот status дальше знака (статистика,
  сравнение с +0x40). Реальный 123b8034 нигде не исполнялся; во всех ARM-тестах
  это заглушка.
- [firmware_tx.c:128-133](../../native/firmware_tx.c) жёстко принимает размер
  allocation = 0x448+0x4000. Реальный TX allocator не проверен (NEXT_WORK это отмечает).

**R2. Закрытие RX и знак результата bulk.**
- При закрытии RX вызывает `discard`
  ([firmware_rx.c:115](../../native/firmware_rx.c)), а это сырой
  `free(0xcaff, batch)` ([firmware_rx_binding.c:65](../../native/firmware_rx_binding.c)).
  Stock ASIX completion всегда передаёт batch в 123b8174, даже при ошибке
  (NATIVE_COMPLETION_ABI.md:29-33). Учёт в rx_done/pacing (+0x530, refill-логика)
  при этом обходится. Куча совпадает (allocator 123e438c, heap 0xcaff), так что
  это расхождение со stock, а не доказанная утечка.
- Весь TX/RX считает неотрицательный signed_result числом байт. Если реальный
  bulk на железе возвращает 0 при успехе, как IOS ioctl (см. комментарий
  [uhs_transport.c:16-18](../../native/uhs_transport.c) про control), каждый TX
  станет short=-1, а каждый RX — пустым. Ответ даст только реальный bulk-тест;
  ARM-эмуляция этого не покажет.

Мелочи, не вошедшие в пятёрку:
- Kind3/link не реализован, а callback поглощает все сообщения своего контроллера
  ([completion_tokens.c:123](../../native/completion_tokens.c)).
- `rx_done` всегда получает pacing=0 ([firmware_rx.c:116](../../native/firmware_rx.c)).
- Cache maintenance для bounce→inline memcpy отсутствует (уже известно).

### Противоречия документации

Существенных не найдено. ARCHITECTURE/CURRENT_STATE правильно помечают TX binding и
router как непроверенные. BUILD_AND_TEST:28 честно говорит, что TX ARM-теста нет.

## 4. Ближайший блок работ: закрыть непроверенный TX/IO checkpoint

1. Собрать текущее ARM-ядро (`build_native_core.sh`) и прогнать relocation oracle
   на трёх базах. Критерий: exit 0, `nm -u` пуст, сообщено новое число relocations
   (не 307).
2. Добавить в test.sh host-тест `tests/test_firmware_io.c`: одна очередь токенов,
   до 2 RX и 8 TX одновременно.
   - Completion приходят не по порядку; есть duplicate и stale.
   - rx_done синхронно делает refill, tx_done синхронно отправляет следующий TX.
   - Partial-open: `rx` или `tx` равен NULL.
   - Close с pending, final drain уничтожает весь контроллер (ASan ловит доступ
     после drain).

   Критерий: для каждого принятого события pending +1/-1 ровно один раз, ровно
   один done/discard, ноль утечек; отказ доставки виден (после исправления F2).
3. Добавить `tests/test_ios_firmware_tx_arm.py`: скомпилированный send_async →
   токен UHS → owner worker → callback → firmware_tx_binding → **реальный**
   123b8034 и pending helper (kind 2).
   - Сценарии: success, short, -64, cancel/closing, failed submit, синхронный
     следующий TX.
   - Проверить фактический отпечаток e2eb4f4b.
   - Зафиксировать, что 123b8034 делает со status (закрывает R1).

   Критерий: owner-ELF hash закреплён; без evidence тест падает явно, а не
   пропускается.
4. Исправить F2 и F3 (общий error latch и binding на контроллер) с тестами из п.2.
5. Записать результат: команды, exit status, покрытие. Уровень остаётся
   [host]/[ARM-emu], не [console].

Блок не требует консоли и не меняет toolchain.

## 5. Предлагаемое разделение Claude/Codex

| Владелец | Файлы |
|---|---|
| Claude (I/O блок) | native/firmware_tx.*, firmware_tx_binding.*, firmware_io.*, tests/test_firmware_tx.c, новые tests/test_firmware_io.c и tests/test_ios_firmware_tx_arm.py |
| Codex (loader/entry) | native/loader_contract.*, stroopwafel_stage.*, mocha_alloc_probe*, entry_probe.c, tools/mocha_boot_state/, tools/loader_capability_probe/, scripts/build_mocha_*, scripts/*mocha*.py, tests/test_mocha_*, test_ios_map_arm.py, test_ios_reload_command_arm.py, docs/NATIVE_LOADER_DESIGN.md, docs/MOCHA_*.md |
| Только по согласованию (один писатель за раз) | native/completion_tokens.*, firmware_rx.*, firmware_rx_binding.* (F2/F3 их задевают), ios_net_abi.h, uhs_transport.*, scripts/test.sh, build_native_core.sh, test_native_rx_arm.ps1, build/native/ |

Предлагаю на время I/O блока отдать Claude правки test.sh, build_native_core.sh и
completion_tokens/firmware_rx_binding. Codex в это время собирает loader-артефакты в
отдельный каталог (например build/loader-codex/), не в build/native. Перед и после
работы — хеши изменённых файлов относительно snapshot.json.
