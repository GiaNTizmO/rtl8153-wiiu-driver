# Консоль, сеть, SD и аппаратные ограничения

Эта документация описывает прежнюю сессию, не живой status. До контакта/записи
уточнить актуальное состояние; не выводить его из старого «готово».
В рамках первого изучения Claude консоль вообще не трогать.

## Сеть

PC 192.168.31.14; Wii U Wi-Fi/FTP control обычно 192.168.31.144;
RTL8153 private lwIP 192.168.31.13. Оба адреса реально обслуживали FTP в разные
моменты. Исторически control был .144, data connection происходила через .13.
Нельзя утверждать, что FTP выключен, лишь по timeout одного адреса.

FTPiiU port21, anonymous login, PC source .14; active mode ftp.set_pasv(False)
работал с двумя interfaces. scripts/mocha_diagnostic_deploy.py поддерживает
--host .144/.13, --menu-confirmed. Флаг фиксирует настоящее подтверждение человека,
не заменяет его. Не выполнять FTP во время игры/loading/hang.

setup remote: /fs/vol/external01/wiiu/environments/aroma/modules/setup/00_mocha.rpx
app: /fs/vol/external01/wiiu/apps/rtl8153_mocha_boot_state.wuhb
report: /fs/vol/external01/wiiu/rtl8153/mocha-boot-state.log
PPC log/config: /fs/vol/external01/wiiu/rtl8153/driver.log и config.ini.

FTP rename-overwrite НЕ поддерживается: 550 File exists. Не удалять active boot
перед upload и не обходить это direct STOR/truncate. Проверенная процедура при
необходимости — owner powers off, SD to PC, staged copy/hash/recovery. SD была G:,
сейчас букву/доступность проверить; не угадывать и не форматировать носитель.
Любой backup RPX в setup с окончанием .rpx может грузиться Aroma независимо от имени.
Использовать .disabled suffix. Official recovery держать вне setup scan.

## TCP syslog/IOPShell

Один receiver слушает .14:7965; console connects после UDP beacon .4445:
WIIU_TCP_SYSLOG_BEACON_V1; heartbeat один NUL/sec, commands ASCII CRLF.
Скрипт: scripts/wiiu_tcp.py. Историческая рабочая команда:

    python -u scripts/wiiu_tcp.py --console 192.168.31.144 --console 192.168.31.13 --directory evidence/2026-11-09/iopshell

Исторический PID 7660 НЕ считать текущим. Перед любыми действиями inspect listener,
CIM CommandLine и actual --directory. Working commands queue/log были в
evidence/2026-11-09/iopshell/{commands.txt,console.log}.

В прошлый раз попытка создать receiver в evidence/mocha-diagnostic/reload-session
завершилась bind conflict: порт уже слушал старый receiver с ДРУГИМ directory.
Записанная туда команда не ушла. Она позже архивирована как unsent-command.archived.txt
и удалена из queue, чтобы не запустить её случайно при новом listener.
Только одна фактическая iosu reload ушла через working iopshell и owner наблюдал reboot.

Не писать очередь, пока не определён actual process/directory и current live peer.
COMMAND_SENT — socket transmission, не выполнение. Не повторять автоматически
неподтверждённый reload; не посылать reboot/title launch/IOS writes/debug mode change
без нового разрешения на соответствующее действие. Coretrace interval минимум 8 sec.

## Следующий аппаратный шаг

Сначала закончить и локально проверить reviewable candidate; описать что делает,
какие bytes/addresses меняет, expected results, recovery и stop conditions. Только
потом согласовывать hardware test. Разрешение на прошлый allocation diagnostic
уже использовано и не распространяется на USB claim/ops installation/heap entry.

Перед заменой owner подтвердит idle Menu/game/browser closed. Для boot-эксперимента
owner рядом и умеет восстановить SD. После установки нужен cold boot; возврат в Menu
не удаляет resident patches. После теста official restore + cold boot + fresh report.

Журнал freshness: сохранять/архивировать прошлый report до каждого запуска;
требовать подтверждённую новую invocation, complete marker, code identity, ticks/hash.
Не принимать byte-identical старый report за результат новой операции.

По последней проверке official RPX и official NET/kernel runtime восстановлены.
Native ownership/ops/DHCP/system Ethernet не включались. Wi-Fi не выключать до
согласованного системного теста после реального native data path.
