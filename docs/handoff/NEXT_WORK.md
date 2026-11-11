# Следующие задачи и совместная работа

## Первое задание Claude

Изучение и ревью без изменения кода, запусков, сети и аппаратных действий. Результат:
карта понимания; противоречия документации/кода; 3–5 наиболее важных замечаний с
file:line; следующий конкретный шаг и evidence, который его закроет. Никакой
копии старого плана без проверки кода. Не отмечать цель завершённой.

## Приоритет после отдельного поручения продолжать разработку

1. Проверить последний непроверенный checkpoint: firmware_tx_binding, firmware_io,
   обновлённые scripts/test.sh/build_native_core.sh. Собрать текущий ARM image;
   проверить unresolved symbols и GNU ld relocation; не пользоваться старым ELF.
2. Добавить firmware-TX ARM integration: compiled send_async, настоящий owner TX
   delivery 123b8034, synchronous refill, queue/native completion, pending kind2,
   error/cancel/short result и возврат original batch. C standalone тест уже есть.
3. Соединить RX/TX через firmware_io в одной token queue: одновременно 2 RX/8 TX,
   out-of-order completion, duplicate/stale, partial-open, cancel failure/retry,
   synchronous refill и final drain. После final drain ни один слой не трогает
   потенциально освобождённые adapter/router/tokens/controller.
4. Проектировать shared controller storage/teardown с пониманием stock worker,
   native held batches и callback attachment. Реальный UHS open/discovery/endpoint
   admin/link timer, MAC/get_link и shutdown. Не путать copied token с DMA.
5. Главный аппаратный барьер: реализовать bounded loader/entry для Aroma/Mocha.
   Allocation probe уже доказан — не гонять его снова без нового вопроса. Нужны
   retention, VA/PA/domain, cache sync и entry return до exposure ops pointer.
   Возможные boot-time routes исследовать в isolated source, не в live kernel.
6. Подготовить minimal reviewable executable-entry diagnostic и согласовать
   hardware activation в конце. Результат должен быть конкретным, с восстановлением.
7. Только затем USB handoff (PPC stops), native ops/discovery, link и штатный DHCP,
   Wi-Fi-off/browser/system traffic и lifecycle validation, install/rollback package.

Офлайн можно продвигать loader и I/O исследования независимо, но не подменять
конечный драйвер бесконечными отдельными моделями. Каждый test должен проверять
реальный следующий runtime contract, а не повторять private helper/constants.

## Что отдельно перепроверить при ревью

Это вопросы для аудита, НЕ утверждения о доказанных дефектах:

- Потеря owner state/free failure: RX binding free_error подавляет drain, но нужны
  дальнейшие error reporting/recovery и удержание unsafe controller.
- Какие allocations/байты задаёт реальный TX allocator; fixed entry geometry
  firmware_tx предполагает capacity0x4000/header8 и нуждается в ARM-тесте callsite.
- Хватает ли out capacity16384 при RTL short-frame padding и aggregate repack;
  отказ допустим, но его propagation должен корректно вернуть native TX batch.
- Token counter lifetime: нельзя reinit/reuse old namespace до доказанной queue
  quiescence. Attach/detach checks не устанавливают её сами.
- Bound callback возвращает consumed при invalid token, чтобы worker не free batch;
  предусмотреть диагностику/обработку невалидного собственного сообщения без leak
  и незамеченного зависания pending. Kind3/link пока не реализован.
- Status/length: UHS nonnegative signed_result interpretation, zero-length,
  oversized/malformed aggregate; short control transfer semantics на hardware.
- RX64 bounce устраняет различие inline32, но cache maintenance ещё не реализована.
  Не принимать emulated memcpy за cache/DMA proof.
- Native held count и final worker request-free после controller drain: реальный
  teardown должен сохранять всё, к чему worker обращается после callback return.
- discovery_guard rollback/readback failures, registration сохранение ASIX,
  ops parameter limits, thread/worker serialization и reentrancy actual call chain.
- Loader domains: mapper меняет L1 coarse domain для 1 MiB; allocation page alone
  не даёт права менять соседние mappings. Не угадывать PA из «похожего» VA.

## Разделение Codex/Claude

Владелец определяет кто редактирует какие файлы. Практический старт:
Claude — ревью TX binding/router и их tests; Codex — loader/entry investigation.
Это предложение, не автоматическое распределение и не разрешение запустить второго
агента. На первом шаге Claude только читает.

Нельзя одновременно редактировать shared completion_tokens, build/test scripts,
ABI headers, firmware_io или один build/native. Один агент собирает в shared build
за раз. Для параллельных сборок нужны раздельные каталоги/выходы. Корень не Git
checkout; worktree/branch isolation автоматически не появится. Не запускать git
init/clean/reset, не копировать untracked-файлы поверх другой работы без сверки.

Перед и после работы сохранять file hashes/diff, перечень changed files, commands
и результаты. snapshot.json — исходная точка передачи, не immutable source truth.
Отчёт Claude: что сделал/нашёл, exact paths, что проверил/не смог, remaining gates.
Codex не будет переписывать эти же файлы до согласования возврата работы.

## Критерии конечной готовности

Native image действительно исполняется в IOS-NET; mapping/cache/lifetime доказаны;
PPC relinquishes USB/hooks, native alone owns RTL; valid RTL ops зарегистрированы;
системный DHCP и DNS, browser/приложения без Wi-Fi; transitions/reboot/unplug/link
recovery не висят, буферы/counters не теряются; официальный rollback проверен;
дистрибутив и исходники/лицензии соответствуют. Пока этого нет, цель не завершена.
