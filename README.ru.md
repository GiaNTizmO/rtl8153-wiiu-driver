# RTL8153 USB Ethernet для Wii U (Aroma)

[English](README.md) | **Русский**

Проводной Ethernet для Wii U через USB-адаптеры Realtek **RTL8153 / RTL8153B**
(VID:PID `0BDA:8153`). Цель: Wii U 5.5.6 с актуальной [Aroma](https://aroma.foryour.cafe/).

## Как это работает сейчас

Драйвер работает **не на уровне всей системы**. Это модуль Aroma
`RTL8153Module.wms`, который работает на PowerPC рядом с приложением:

```
приложение / игра / Wii U Menu
        │  socket(), connect(), sendto(), recvfrom(), select(), DNS …
        ▼
перехват nsysnet (FunctionPatcher)  ──►  собственный стек lwIP (DHCP, ARP, TCP, UDP, DNS)
                                               │
                                               ▼
                                   UHS (USB host) → RTL8153 → кабель → роутер
```

Как это устроено:
- Модуль сам находит адаптер, инициализирует RTL8153, поднимает линк и получает
  **собственный IP по DHCP**. MAC читается из адаптера.
- Сокеты, которые создаёт текущее приложение (процесс GAME) или Wii U Menu,
  перехватываются и идут через кабель. Сокеты, созданные до перехвата, и
  процесс ROOT_RPX остаются на штатной сети.
- Системный сетевой стек приставки (IOS-NET) про адаптер **не знает**. Системные
  настройки, системный DHCP, фоновые службы Nintendo и всё, что работает
  внутри IOS, продолжают использовать Wi-Fi.
- **Wi-Fi нужно оставить настроенным.** Многие приложения перед работой
  проверяют подключение через системный стек.
- После смены приложения модуль по умолчанию заново получает DHCP-аренду
  (`mode=always`).

### Что проверено на приставке

- IP по DHCP, ответ на ping, FTP.
- Браузер открывает сайты.
- Переходы приложение → Menu.

### Что не проверено или не поддерживается

- Работа без Wi-Fi.
- Системные службы, eShop, Download Manager.
- Все игры. Moonlight и другие стриминговые клиенты отдельно не проверялись.
- Скорость не измерялась. USB 2.0 даёт до 480 Мбит/с до накладных расходов;
  гигабитный линк RJ45 не означает гигабитного трафика.
- Старая база сертификатов Nintendo TLS даёт предупреждения на части сайтов.
- vWii, IPv6, RTL8156 и другие VID:PID не поддерживаются.

## Установка (PPC-модуль)

Готовый архив — на странице [Releases](https://github.com/GiaNTizmO/rtl8153-wiiu-driver/releases):
`RTL8153Module-<версия>-sd.zip`.

1. Распакуйте архив в корень SD-карты. Он содержит
   `wiiu/environments/aroma/modules/RTL8153Module.wms` и
   `wiiu/rtl8153/config.ini`. Если `config.ini` уже настроен, не перезаписывайте его.
2. Нужны модули Aroma **FunctionPatcher** и **Mocha**. Не используйте
   одновременно AX88179Module или другой модуль, перехватывающий сеть.
3. Подключите адаптер к Wii U до загрузки, кабель — к роутеру с DHCP.
4. Журнал пишется в `SD:/wiiu/rtl8153/driver.log`.

Чтобы удалить модуль, удалите `.wms` с SD и перезагрузите приставку. В NAND
ничего не записывается: патч UHS применяется только в RAM и только после
проверки исходной инструкции.

### Настройка (`config.ini`)

| Ключ | Значение |
|---|---|
| `[dhcp] mode` | `always` — новая аренда после смены приложения; `keep_first` — повторно использовать IP |
| `[compat] route` | `ax` — новые сокеты идут через адаптер; `native` — через штатную сеть |
| `[compat] dns` | `ax` — DNS через адаптер; `system` — системный DNS |
| `[compat] nssl` | `bridge` — мост Nintendo TLS (NSSL) через адаптер |
| `[debug] shim_trace` | `0` выкл., `1` общий, `2` подробный журнал сокетов |

`ax` — имя режима, унаследованное от исходного AX88179-проекта; здесь оно
означает путь RTL8153/lwIP.

## Нативный драйвер IOS-NET (в разработке)

Конечная цель — настоящий системный драйвер внутри IOS-NET (ARM). Тогда
адаптер будет работать как штатный LAN для всей системы: системный DHCP,
настройки, все приложения, работа без Wi-Fi. **Этот путь пока не работает и
не устанавливается.**

| Часть | Состояние |
|---|---|
| ARM-ядро RTL8153, framing, RX/TX-адаптеры, completion tokens | Исходники; host-тесты и ARM-эмуляция с реальной прошивкой 5.5.6 |
| TX binding / I/O router | Исходники, ещё не проверены |
| Загрузка кода в IOS-NET (Aroma/Mocha) | Выделение памяти в NET проверено на приставке. Исполнение кода из NET heap подготовлено как диагностический кандидат, на железе не проверено |
| Регистрация ops, передача USB от PPC, системный DHCP | Не начато на железе |

Подробнее:
- [docs/handoff/README.md](docs/handoff/README.md) — текущее состояние;
- [docs/NATIVE_LOADER_DESIGN.md](docs/NATIVE_LOADER_DESIGN.md) — загрузчик;
- [docs/MOCHA_ENTRY_DIAGNOSTIC.md](docs/MOCHA_ENTRY_DIAGNOSTIC.md) — entry-диагностика;
- [docs/NATIVE_COMPLETION_ABI.md](docs/NATIVE_COMPLETION_ABI.md) — ABI completion.

Диагностические сборки Mocha меняют boot-payload. Их нельзя ставить без понимания
процедуры восстановления.

## Сборка

Нужны devkitPro (devkitPPC, devkitARM, wut) и Linux/WSL.

```bash
git clone --recursive https://github.com/GiaNTizmO/rtl8153-wiiu-driver.git
```

Если репозиторий уже склонирован без `--recursive`:

```bash
git submodule update --init
```

Сборка PPC-модуля (результат — `dist/sd/`):

```bash
DEVKITPRO=/opt/devkitpro bash scripts/build.sh
```

Та же сборка выполняется в GitHub Actions (`.github/workflows/build.yml`) в образе
`ghcr.io/wiiu-env/devkitppc`. Это происходит при каждом push в `main` и в
pull request'ах; собранный модуль сохраняется как artifact сборки.

### Выпуск версии

```bash
git tag v0.2.0
git push origin v0.2.0
```

По тегу `v*` CI собирает модуль с этой версией (её видно в Aroma) и создаёт
GitHub Release с файлами:
- `RTL8153Module-<версия>-sd.zip` — готовая структура SD;
- `RTL8153Module.wms`;
- `SHA256SUMS.txt`.

Тег с дефисом (`v0.2.0-beta1`) публикуется как pre-release.

Host-тесты (gcc с ASan/UBSan):

```bash
bash scripts/test.sh
```

ARM-ядро native-драйвера:

```bash
bash scripts/build_native_core.sh
```

Скрипты собирают зависимости только внутри `vendor/` и `build/` проекта:
`make install` нет, toolchain не изменяется. Тестам с ARM-эмуляцией нужен
Python с Unicorn и **собственный** дамп прошивки IOSU 5.5.6. Дамп в репозиторий
не входит и никогда не будет туда добавлен.

## Структура

| Путь | Содержимое |
|---|---|
| `aroma_module/` | WUMS-модуль: жизненный цикл, перехват nsysnet, патч UHS, журнал |
| `driver/` | Транспорт UHS и регистры/инициализация RTL8153 (PPC) |
| `net/` | Интеграция lwIP, таймеры, sys_arch |
| `native/` | ARM-код для IOS-NET, ABI, адаптеры RX/TX, загрузчик и диагностики |
| `tools/` | Read-only пробы для приставки (native probe, boot/entry state) |
| `scripts/`, `tests/` | Сборка, проверки, host-тесты и ARM-эмуляция |
| `docs/` | Исследования, ABI, состояние и журнал решений |
| `references/` | Справочные драйверы (Linux r8152, OpenBSD/FreeBSD ure, U-Boot) |
| `vendor/` | lwIP; submodule'ы WUMS, libmocha, libfunctionpatcher |
| `upstream/` | Submodule'ы MochaPayload, Stroopwafel, libstroopwafel, AX88179 Aroma Driver |

## Благодарности и лицензии

- Основа Aroma-модуля: [wozt/ax88179_aroma_driver](https://github.com/wozt/ax88179_aroma_driver) (GPL-3.0).
- Инициализация RTL8153 по [OpenBSD ure(4)](https://github.com/openbsd/src/blob/master/sys/dev/usb/if_ure.c)
  (BSD, уведомления сохранены в `driver/`). Справочно — Linux `r8152`.
- [lwIP](https://savannah.nongnu.org/projects/lwip/) (BSD); изменения описаны в `vendor/lwip/UPSTREAM.md`.
- [wiiu-env](https://github.com/wiiu-env): WUMS, libmocha, libfunctionpatcher, MochaPayload.
- [Stroopwafel](https://github.com/StroopwafelCFW/stroopwafel).

Проект распространяется под **GPL-3.0** (см. [LICENSE](LICENSE)). Сторонний код
сохраняет свои лицензии.

Проект не связан с Nintendo или Realtek. Используйте на свой риск.
