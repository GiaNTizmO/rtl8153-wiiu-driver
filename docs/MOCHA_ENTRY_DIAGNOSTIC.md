# Диагностика исполнения кода из NET heap (Aroma/Mocha, Wii U 5.5.6)

Статус на 2026-11-09: **кандидат собран и проверен локально, на консоль не
устанавливался**. `activation_allowed=false`. Аппаратный запуск требует
отдельного согласования с владельцем (раздел 6).

Уровни доказательства: **[console]** — подтверждено на приставке ранее;
**[firmware]** — прочитано из закреплённого owner `ios.elf`
(SHA256 `8ac1ae36…`); **[ARM-emu]** — реальные ARM-инструкции в Unicorn;
**[host]** — host-тест под ASan/UBSan; **[hw-unverified]** — не проверено.

## 1. Решение

Небольшой ARM entry probe кладётся в **одну страницу 4 KiB local heap процесса
NET (heap id 0xCAFE)**. Код выполняет один вызов из того же проверенного места,
где уже работал allocation diagnostic: из reload-only хука на вызов NET init
`0x12300ae4`. После этого страница освобождается, а результат записывается в
owned mailbox `0x1288d040`.

Порядок действий:
1. Выделить страницу из local heap.
2. Проверить, что она целиком лежит внутри local heap.
3. Скопировать 28-байтный PIC-blob и проверить его чтением обратно.
4. `IOS_FlushDCache(page, 4096)` (svc 0x52).
5. Вызвать страницу один раз и сверить marker, PC и return.
6. `IOS_Free`, затем commit mailbox.

Почему этот путь, а не другие:

| Вариант | Вывод |
|---|---|
| Stroopwafel / `/dev/stroopwafel` | Отсутствует в текущей Aroma (IOS_Open=-6) **[console]**; смена boot environment не согласована |
| Область Mocha `0x12431900..0x12432000` | Probe помещается (1545 из 1792 байт), но драйвер нет; уже исполняемый NET text не отвечает на вопрос о heap |
| Mapper `0x08124f88` | Меняет domain всей L1-области 1 MiB **[ARM-emu]**; для этого пути **не нужен** — память уже отображена для NET |
| `Mocha_IOSUCallSVC` | Исполняется в контексте MCP, не NET |
| **Local heap NET (выбран)** | Память принадлежит NET, отображена NET и уже выделялась на приставке; меняется только сам вопрос — исполнение |

Boot environment не меняется: используется тот же патчер ELF при reload, что и
в успешном allocation diagnostic.

## 2. Происхождение памяти, VA/PA, владение, domain, cache

**Происхождение [firmware].** В main NET:
- `0x12300aac: ldr r0,[pc,#356]` → literal `0x12300c18 = 0x124c6060`;
- `0x12300ab0: mov r1,#0x40000`;
- `0x12300ab4: bl 0x123e4374` (svc 0x24) создаёт local process heap в буфере
  `0x124c6060..0x12506060`;
- `0x12300ac4` создаёт cross-process heap (svc 0x25, 0xc0000).

Затем идёт вызов init `0x12300ae4`, который мы перехватываем. Значит, к моменту
хука local heap уже существует. Probe работает в main-потоке NET после возврата
original init. Часть NET-потоков к этому моменту может уже работать (ifnet
startup `0x12308640` вызывается из `0x12301190`; точный порядок не
устанавливался). Probe не трогает общих структур NET, кроме вызовов
heap-syscall, своей страницы и mailbox.

**Сегмент [firmware].** Буфер heap целиком внутри PT_LOAD NET BSS:
`vaddr = paddr = 0x124c6000`, memsz `0x3c7028`, flags `0x700006` (uid 7 = NET,
RW). Отображение identity, то есть VA = PA. С другими PT_LOAD сегментами не
пересекается: это проверяет `check_mocha_entry_candidate.py`.

**Подтверждение [console].** Прошлый allocation diagnostic получил
`page=124c7000` из heap 0xCAFE. Адрес лежит внутри этого диапазона, и NET
успешно писал и читал страницу из собственного контекста.

**Владение и время жизни.** Владелец — процесс NET, срок жизни — до следующего
reload IOS. Probe не удерживает страницу: освобождает её после возврата и не
обращается к ней после `free`.

**Domain и права.** Используется уже существующее отображение NET. Mapper не
вызывается, domain и PTE не меняются. ARM926EJ-S (ARMv5) не имеет XN: выборка
инструкций проверяется теми же AP и domain, что и чтение. NET уже читал и писал
эту память **[console]**, поэтому архитектурно выборка разрешена. Аппаратно это
**[hw-unverified]**, и это главный вопрос теста.

**D-cache [firmware].** В NET есть стабы svc 0x51 InvalidateDCache
(`0x123e44dc`) и svc 0x52 FlushDCache (`0x123e44e4`). NET сам вызывает
`0x123e44e4` в `0x1236a3c4`, то есть syscall доступен NET. Probe записывает код
через D-cache, затем делает FlushDCache страницы и только потом вызывает код.
Ненулевой rc означает, что код не исполняется. Семантика clean и то, что успех
возвращается как 0: **[hw-unverified]**.

**I-cache.** Инвалидация I-cache — привилегированная операция, у NET для неё
нет syscall. Страница в этом экземпляре IOS никогда не исполнялась: BSS — это
данные, а в 5.5.6 по этому адресу нет кода. Значит, в I-cache не должно быть
строк с этим VA, и первая выборка пойдёт из памяти. Инвалидация кэшей при
reload/загрузке IOS предполагается, но не проверялась. **[hw-unverified]**. Если выборка всё же
окажется устаревшей, это даст stage 9, а не ложный успех: blob возвращает
`nonce ^ RTLX` и публикует свой PC. Для будущего драйвера повторное
использование страницы под код потребует отдельного решения: страница, на
которой уже выполнялся код, может иметь строки в I-cache.

## 3. Реализация (исходники)

| Файл | Назначение |
|---|---|
| native/mocha_entry_status.h | Mailbox ABI 2 (64 байта, magic RTLE), стадии, границы local heap |
| native/mocha_heap_entry_blob.S | 7 слов PIC-кода: `mov r3,pc; ldr r2,=RTLX; str r3,[r0]; str r2,[r0,#4]; eor r0,r1,r2; bx lr` |
| native/mocha_entry_probe.c | alloc → проверка границ → copy+readback → FlushDCache → вызов → проверка → free → commit |
| native/mocha_entry_probe_entry.S | Тот же 7-инструкционный shim, что прошёл на консоли (SP mod 8, r4–r8, LR) |
| scripts/prepare_mocha_alloc_experiment.py | Добавлен `--variant entry` (по умолчанию `alloc`, вывод байт-в-байт прежний) |
| scripts/build_mocha_entry_experiment.sh | Изолированная сборка в build/mocha-entry-experiment |
| scripts/check_mocha_entry_candidate.py | Статическая проверка и review JSON |
| tools/mocha_entry_state/ + scripts/build_mocha_entry_state.sh | Read-only PPC-инспектор; лог `wiiu/rtl8153/mocha-entry-state.log` |
| scripts/make_mocha_boot_profile.py | Добавлены `--candidate-dir`/`--out` (значения по умолчанию прежние) |
| tests/test_mocha_entry_arm.py, tests/test_mocha_entry_state.py | Новые тесты |
| tests/test_mocha_boot_arm.py, tests/test_mocha_boot_guard.py | Добавлен необязательный аргумент с каталогом эксперимента (по умолчанию прежний) |

Boot guard для варианта entry дополнительно требует точные слова:
- стабы FlushDCache `0x123e44e4`/`e8`;
- инструкции создания heap `0x12300aac`/`b0`/`b4`;
- literal `0x12300c18`.

При любом несовпадении хук и расширение BSS не ставятся. Исправление
MCP timeout-указателя (`rtl_timeout_word`), унаследованное из allocation
experiment, присутствует и здесь.

Стадии mailbox:

| Стадия | Значение | Что произошло |
|---|---|---|
| 2 | INIT_FAILED | Ничего не делалось |
| 3 | NO_MEMORY | Страница не выделена |
| 4 | OUT_OF_RANGE | Страница не записывалась и не исполнялась |
| 5 | COPY_MISMATCH | Не исполнялось |
| 6 | FLUSH_FAILED | Не исполнялось |
| 8 | RETURNED_OK | Успех |
| 9 | RETURNED_BAD | Код вернулся, но значения не совпали |

Стадии 1 и 7 переходные и в commit не попадают. Успех засчитывается, только
если сходятся все поля: `pc = page+8`, `marker = RTLX`,
`ret = nonce ^ RTLX`, `nonce = page ^ 0x5a17c0de`, `copy = 1`, `flush_rc = 0`,
все счётчики вызовов равны 1, а page лежит в local heap.

## 4. Локальные проверки (2026-11-09)

Все команды запускались из корня проекта.

| Команда | Результат | Уровень |
|---|---|---|
| `wsl … bash scripts/build_mocha_entry_experiment.sh` | exit 0; NET code 1545 байт, `.text` до 0x12431f09 (< 0x12432000); RPX, kernel, NET собраны | compiled |
| `check_mocha_entry_candidate.py` | exit 0: hash firmware, BL, 3 SVC-стаба, heap внутри BSS, DLP, mailbox, shim, точный blob | static |
| `test_mocha_boot_guard.py build/mocha-entry-experiment` | exit 0: 13 сигнатурных слов, metadata, границы | [host] |
| `python tests/test_mocha_entry_arm.py` | exit 0: 90 сценариев (5 init × 2 SP × 9) | [ARM-emu] |
| `python tests/test_mocha_boot_arm.py build/mocha-entry-experiment` | exit 0: 11 сценариев | [ARM-emu] |
| `wsl … bash scripts/build_mocha_entry_state.sh` + `test_mocha_entry_state.py` | exit 0 | compiled + [host] |
| Регрессия alloc: `test_mocha_alloc_arm.py`, `test_mocha_boot_arm.py`, `test_mocha_boot_guard.py`, `test_mocha_boot_state.py`, `test_mocha_alloc_candidate.py`, `test_mocha_boot_report.py` | все exit 0 | [ARM-emu]/[host] |
| Prepare `alloc` на свежем archive против build/mocha-alloc-experiment | 8 файлов overlay идентичны | — |

Что покрывает каждый тест:

- **test_mocha_entry_arm.py.** Реальный shim, C-код и скопированный blob
  исполняются в heap-странице.
  - Проверяются: return и r4–r11/SP; порядок вызовов; точные записи blob; копия
    до flush; flush до входа; вход только при page в local heap.
  - Только 6 инструкций blob, каждая один раз, без выхода за конец.
  - Сценарии: устаревшая выборка → stage 9; flush rc ≠ 0 → stage 6, без входа;
    порча копии → stage 5; commit последним.
  - Четыре мутационные проверки (неверный nonce, устаревшая выборка как OK,
    изменённый blob, исполнение вне heap) тест ловит.
- **test_mocha_boot_arm.py (entry).** Подписи и шаги boot-патчера:
  - baseline: 28 записей;
  - кандидат: 29 записей плюс расширение BSS до 0x1288d080;
  - 5 негативных подписей (init BL, alloc SVC, FlushDCache, heap BL, heap
    literal): хук не ставится;
  - полный `kernel_launch_ios`: 88 записей, 234 копии, RLDE/HDRP/NETP;
  - негатив полного пути: 87 записей; отклонённый header: 0 записей.
  - PT_LOAD/PHDR совпадают с независимым oracle.
- **test_mocha_entry_state.py.** Идентичности, точный успех, 13 отказов по
  одному полю, границы heap, стадии отказа, torn и absent статус, границы адресов.

**Заглушки:**
- в entry-тесте: original NET init, IOS_AllocAligned, IOS_Free, IOS_FlushDCache,
  printf;
- в boot-тесте: memcpy/reverse_memcpy (как bounded memmove), CP15/MRC,
  прерывания, bootrom handoff;
- в state-тесте: `Mocha_IOSUKernelRead32` и coreinit.

В Unicorn нет MMU, прав, кэшей и prefetch abort.

**Это не доказательство исполнения на железе.** Allocation, relocation и
эмуляция не подменяют аппаратный результат.

Артефакты (локальные, не установлены):

| Файл | Размер | SHA256 |
|---|---|---|
| build/mocha-entry-experiment/00_mocha.rpx | 67417 | `919a99de83343b10b9086fcf41e2d3f23023e4a4a1c389bd8f8979192ddf09c1` |
| build/mocha-entry-experiment/source/ios_net/ios_net.elf | — | `8d0b43476220b70679587e062d7e0d26675d3d5c0e7cc18d855385db05eb41da` |
| build/mocha-entry-experiment/source/ios_kernel/ios_kernel.elf | — | `afa0852a5dacfd0be783e18cdb2c25454945cbfcf4562de3de88759cd891bb41` |
| dist/mocha-entry-state/wiiu/apps/rtl8153_mocha_entry_state.wuhb | 72616 | `78bf0b92f5ac1e401b910d04c844a9d56e197eae4d25da278a9294468aac74a8` |
| heap blob (28 байт) | — | `553a8bb6…dd63` |

Слово хука `0x12300ae4`: `0xebffffe2` → `0xeb04c385`. Оно **совпадает** со словом
прежнего allocation-кандидата: точка входа shim лежит по тому же адресу.
Поэтому старое приложение Boot State не различает эти два кандидата.
Использовать только Entry State: он сверяет весь `.text` и ABI 2 mailbox.

## 5. Не проверено на железе

1. Выборка инструкций из NET BSS/heap: prefetch abort или исполнение.
2. Семантика clean и rc у `IOS_FlushDCache` для пользовательского диапазона;
   выталкивание write buffer.
3. Отсутствие устаревших строк I-cache для свежей страницы.
4. Поведение при abort: ожидается падение IOS при reload, восстановление через
   cold boot (раздел 6).
5. Обнуление расширенного BSS bootloader'ом и видимость mailbox для PPC-чтения.
6. Наличие printf на этом этапе (на результат не влияет).
7. Удержание памяти под драйвер (десятки KiB из общей 256 KiB local heap NET),
   повторное использование страниц под код, вход из NET worker, а не из init.
8. USB, ops, discovery — не затрагиваются и не проверяются.

## 6. Аппаратный тест (только после отдельного разрешения владельца)

**Что меняется:**
- Файл SD `wiiu/environments/aroma/modules/setup/00_mocha.rpx`: official
  `16921b40…` заменяется на кандидат `919a99de…`.
- Добавляется `wiiu/apps/rtl8153_mocha_entry_state.wuhb`.

В IOS при **reload** (и только при нём):
- слово хука `0x12300ae4`;
- расширение NET BSS на 88 байт (mailbox `0x1288d040`, в той же странице 4 KiB);
- kernel diag-слова;
- одна страница local heap NET временно выделяется, исполняется и
  освобождается.

NAND, SLC/MLC, настройки, USB и сеть не затрагиваются.

**При cold boot хук не ставится.** `instant_patches` копируют payload, но слово
`0x12300ae4` меняет только `kernel_launch_ios` на reload. Это уже наблюдалось на
приставке: до reload было `NET init=ebffffe2`.

**Предусловия:**
- Владелец рядом и умеет вынуть и подключить SD.
- Есть official RPX: evidence/2026-11-09/mocha-installed/00_mocha.rpx и
  dist/mocha-allocation-review/restore/00_mocha.rpx. Хеш проверить заново.
- Приставка в Menu, игр и браузера нет.
- Состояние TCP-receiver проверено по CONSOLE.md, иначе команда не посылается.

**Порядок:**
1. Владелец выключает приставку и подключает SD к ПК. Букву диска проверить.
2. Проверить, что текущий `setup/00_mocha.rpx` имеет хеш official. Скопировать
   его вне `setup` с суффиксом `.disabled`.
3. Записать кандидат в `setup/00_mocha.rpx` и сверить хеш после чтения.
4. Записать WUHB и сверить хеш.
5. Убедиться, что `wiiu/rtl8153/mocha-entry-state.log` **отсутствует**. Если файл
   есть, перенести его в архив на ПК и удалить с SD.
6. Cold boot.
7. Необязательно: запустить Entry State. Ожидается `entry_candidate=MATCH`,
   `NET entry hook absent`. Это pre-reload identity, файл потом перезапишется.
8. Отправить **одну** команду `iosu reload` через проверенный receiver. Владелец
   наблюдает перезагрузку. Не повторять автоматически.
9. Запустить Entry State один раз, затем HOME → Close.
10. Выключить приставку, SD на ПК, скопировать лог в
    `evidence/mocha-entry-diagnostic/<UTC>/`.
11. Восстановить official RPX (`16921b40…`), сверить хеш. Кандидат держать вне
    `setup` с суффиксом `.disabled`.
12. Cold boot. Запустить Entry State или Boot State: ожидается `official=MATCH`,
    хук отсутствует. Этот лог тоже сохранить.

**Ожидаемый отчёт при успехе** (шаг 9):

    reload hook=<candidate word> NET init call=eb04c385
    Mocha kernel text: official=no entry_candidate=MATCH
    Mocha NET text: official=no entry_candidate=MATCH
    NET entry hook present; execution not proven by code inspection.
    reload diag=524c4445 48445250 4e455450 00000000
    NET entry status: stage=8 init=0 page=124cX000 copy=1 flush_rc=0 ret=<page^5a17c0de^52544c58>
    NET entry observed: pc=<page+8> marker=52544c58 nonce=<page^5a17c0de> calls alloc=1 flush=1 entry=1 free=1
    NET local-heap page code executed and returned (marker, PC, nonce match). ...

**Условия остановки** (без повторов и без новых записей в IOS):

| Ситуация | Действие и вывод |
|---|---|
| Нет возврата в Menu или зависание дольше ~3 мин после reload | Выключить кнопкой питания, cold boot (хук не ставится), выполнить шаги 10–12. Вероятно, abort при выборке из heap или проблема cache. Ответ отрицательный, повтор этого кандидата не нужен |
| stage 9 | Код исполнился, но результат не тот: cache или выборка ненадёжны. Стоп |
| stage 4, 5 или 6 | Код не исполнялся. Стоп, анализ (heap вне диапазона, readback, rc у flush) |
| Нет status, нет хука или неизвестная identity | Результат не принимается. Проверить SD и порядок; повтор только с новым техническим основанием |
| Любое расхождение хешей при записи на SD | Не загружать приставку с этим файлом, вернуть official |

**Восстановление:** шаг 11 и cold boot. Official RPX существует в двух
независимых копиях. Любой backup в `setup` должен иметь не-`.rpx` суффикс:
Aroma загружает любой `.rpx`.

## 7. Если путь окажется неработоспособным

| Результат | Что это значит | Следующий шаг |
|---|---|---|
| Зависание при reload (abort) | Для текущего отображения NET BSS/heap выборка запрещена на практике, несмотря на ARMv5 AP | Код можно размещать только в исполняемом сегменте NET text. Кандидат: boot-time расширение исполняемого PT_LOAD NET (uid 7, R+X) через тот же `section_write` при reload. Нужно доказать физическую свободу и владение диапазоном (сейчас за `0x12432000` лежит Hokaku, следующий PT_LOAD — `0x12440000` R) |
| stage 9 | Нужна полная I-cache инвалидация | Только в kernel-контексте: через Mocha kernel `invalidate_icache 0x0812DCF0` при загрузке образа на reload, а не из NET |
| Успех | Исполнение кода из local heap NET в контексте init доказано | Retained allocation под relocated core (замер размера), вход из NET worker, затем ops — по NATIVE_LOADER_DESIGN.md |
