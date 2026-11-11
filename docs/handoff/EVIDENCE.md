# Доказательства, хеши и исправления истории

## Аппаратная последовательность 2026-11-09

1. Первое allocation experiment: candidate code был виден, но отчёт после reload
оказался byte-identical старому. Свежий запуск не подтверждался, mailbox не читался
из-за отсутствующего hook. Этот опыт сам по себе не доказывает post-reload результат.
См. evidence/2026-11-09/mocha-allocation-test/RESULT.md и audit correction.
2. Новый diagnostic candidate с kernel data markers RLDE/HDRP/NETP. FTP overwrite
не поддерживается (550 File exists); active official не удалялся по FTP. Старый
00_mocha.experiment-run.rpx выключен переименованием в .rpx.disabled.
3. Владелец выключил Wii U, подключил SD как G:. Кандидат и Boot State установлены
прямой записью SD с readback/hash. Старый report архивирован до cold boot.
4. Pre-reload fresh report сохранён: evidence/mocha-diagnostic/20261109T113552028526Z/.
5. Одна команда iosu reload действительно отправлена рабочим receiver; владелец
наблюдал reboot. Затем новый запуск Boot State.
6. Fresh post-reload: evidence/mocha-diagnostic/20261109T114201520106Z/boot-state.log.
7. Официальный Mocha восстановлен напрямую SD; диагностические backup только .disabled.
Владелец cold boot + новый Boot State. Official report:
evidence/mocha-diagnostic/20261109T115136125509Z/boot-state.log.

Отдельного удаления SD report между pre/post reload не было: свежесть этого опыта
основана на подтверждённом новом запуске, различающихся ticks/тексте/хеше и сохранённых
обоих файлах. Parser --after-archive указывал более ранний sd-pre-diagnostic archive;
не выдавать это за отдельную per-reload archival гарантию.

## Точные наблюдения

Pre-reload ticks=00bb28bd6dec092e, identity=candidate, reload=eb0030b0,
NET init=ebffffe2, markers zero. Report SHA256:
947686ccb2c41a54ead2817920276ed76d7dbfeaccfd7589464eeaf12c88b794.

Post-reload ticks=00bb28c39698cccf, identity=candidate, reload=eb0030b0,
NET init=eb04c385; diag=524c4445 48445250 4e455450 00000000;
status stage=4 init=0 page=124c7000 data=1 alloc=1 free=1. SHA256:
14c2647a8a03696a6927a360d6ba6422998059f0e48f3ac06481cf137dbb192d.
Это означает, что путь allocation/data/free call вернулся. НЕ executable memory proof,
не heap code entry, не драйвер, не доказательство внутренней allocator cleanup.

Official restore ticks=00bb28cc399c4f5d, reload=eb003005, NET init=ebffffe2;
kernel/NET official=MATCH allocation_candidate=no. SHA256:
7d52e6cff6adcf42b9f4ca80b5ff99c18c5c072ffbead104e4a72959d13a0087.

## Бинарники и приватный образ

| Объект | SHA256 |
|---|---|
| Official Mocha 00_mocha.rpx (v0.2.5, 66079 bytes) | 16921b4059bdc434b481c16ef8380e16c89ff8b2ad78da62d19a2d372d0bc529 |
| Diagnostic candidate RPX | be19f9fd20688feb398f5fdead4370178fea683a49603afdba118fd06b8336df |
| Diagnostic Boot State WUHB | a67f4634402998e8440ebe0c7df558b18d8c8028f6a3f147d9699fab247c7c6a |
| Old retained experiment | 95ff0d1d26c8928f77df878725569d2601a1c9631cd10ff4c839b10f71f8e919 |
| Owner ios.elf | 8ac1ae366b2925f66f19b7f50033103f0ecee45847c9198aa78343a8cd9c4e23 |

Recovery: dist/mocha-allocation-review/restore/00_mocha.rpx; независимая исходная
копия evidence/2026-11-09/mocha-installed/00_mocha.rpx. Проверять actual bytes.
Кандидаты/manifest ZIP после правок могли устареть. Не регенерировать пакет и
не объявлять installable только потому, что есть directory dist/.

Firmware files local: evidence/firmware-5.5.6/analysis/{ios.elf,map.json,net-arm.txt,
kernel-text.bin}. Firmware/keys не включать в handoff archive или public repo.
Для адресов/fingerprints native/firmware_556.json/.h и ios_profile.c.

## Адресная карта (owner 5.5.6, не разрешение писать)

- NET text 12300000..12431843; BSS 124c6000..1288d027.
- Native interface table 128090bc; USB context pointer storage 1288d020.
- ASIX discovery slot 124ce314, original ops 12456dd0.
- NET init call 12300ae4, original BL ebffffe2.
- Completion 123b94d8; worker 123b83b0; resolver 123b3ecc.
- Pending helper 123b6f28; RX allocator 123b75ac; RX done 123b8174;
  TX done 123b8034; drain 123b936c; native RX release 123b4430.
- UHS bulk 123bc0d4; control sync 123bdbd4; endpoint admin 123baee4.
- Aligned alloc 123e4394; free 123e439c; heap 0xCAFF.
- NET patch 12431900..12432000 reserved; Hokaku 12432000.
- Experimental-only status BSS 1288d040..1288d07f; stock image там не писать.
- Mapper 08124f88; enclosing L1 domain scope 1 MiB.

## Не повторять ошибочные выводы

- DHCP IP/MAC не доказывает native stack: PPC lwIP тоже получает IP.
- UHS trace acquired by pid21 относится к существующему PPC пути, не NET ARM owner.
- Background loading music/вечная загрузка не означает выполнение Native Probe main.
  Старый display crash был WHBLogConsoleDraw/OSScreenClearBufferEx до IOS reads;
  headless probe это устранила. Другой Settings→Menu hang отдельно не объяснён.
- SVC MCP execution ≠ NET worker, PPC mapping API ≠ ARM mapping.
- Init marker/zero markers/command echo/socket write не доказывают reload completion.
- UDP beacon/IP reachability не доказывает idle Menu.
- Local drain ≠ absence of queued stale completions; token counter не restart.
- Эти tests не разрешают hardware activation; native_activation_allowed=false.
