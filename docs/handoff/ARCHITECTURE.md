# Архитектура и карта кода

## Два разных сетевых пути

Работающий промежуточный путь:

    Aroma/PPC WUMS → nsysnet shim → собственный lwIP → UHS client → RTL8153 USB

Конечный путь:

    системные сокеты/службы → CafeOS IOS-NET → USB Ethernet abstraction
        → RTL ops (ARM) → native UHS → RTL8153 USB

Это разные CPU/context/API. Адрес функции IOS-NET нельзя вызывать из PPC, а
Mocha_IOSUCallSVC исполняет MCP context, не создаёт NET worker. Нельзя одновременно
дать PPC .wms и ARM-драйверу владение одним USB interface.

## Каталоги

| Путь | Назначение |
|---|---|
| aroma_module/ | WUMS lifecycle, FunctionPatcher shim, IOSU patch, SD logging |
| driver/rtl8153.c и *.inc | PPC UHS transport + RTL register/device code |
| driver/rtl8153_framing.* | Общее framing/parsing |
| net/ax_net.* и net/port/ | PPC lwIP integration, timers/heartbeat, sys_arch |
| native/ | ARM core, ABI, ownership, adapters, loader models |
| tools/native_probe/ | Read-only headless native fingerprints/interfaces probe |
| tools/mocha_boot_state/ | Bounded fixed-address read-only boot/diagnostic report |
| tools/loader_capability_probe/ | Read-only Stroopwafel availability query |
| scripts/ | Сборка, relocation, test runners, diagnostic packaging/FTP, TCP receiver |
| tests/ | C mocks/sanitizers, owner firmware ARM emulation, relocation oracle |
| upstream/ | Reference checkouts MochaPayload, Stroopwafel, AX88179 |
| vendor/ | Локальные зависимости; SDK installations не менять |
| evidence/ | Приватные аппаратные журналы/owner firmware, не публиковать |
| build/, dist/ | Generated и потенциально устаревшие артефакты |

## Native модули

- rtl8153_ios_core: callbacks transport для инициализации/линка RTL, без WUT/lwIP.
- uhs_transport: control_sync и bulk_async ABI, cancellation native opcode 2.
  Запрос отмены не равен completion; pending/DMA не освобождаются по return=0.
- frame_adapter, batch_bridge: BE native header ↔ LE RTL descriptors; TX native
  records align2, RTL align4; RX descriptor 24 bytes, FCS strip, aggregate align8.
- transfer_owner/native_glue: прежняя модель собственных двух RX batches и восьми
  TX slots. Полезные тесты core, но это не готовая модель firmware-owned RX.
- completion_tokens: original metadata сохраняется в десяти pending slots.
  UHS data остаётся реальным DMA pointer; metadata.buffer становится request token.
  Worker передаёт его callback без dereference. Callback восстанавливает original
  kind/id/batch/DMA/cookie. Counter не wrap, failed submit burns token.
- completion_bridge/ios_completion_adapter: более ранние one-shot validators.
  Их сигнатуры НЕ firmware ops entry; не подключать напрямую в ops table.
- firmware_rx: настоящее receive_async(context,controller,batch), два borrow slots,
  alloc64 bounce, copy/parse into caller-owned inline allocation. Batch не наш.
- firmware_rx_binding: проверяет 5 code fingerprints и строит C-вызовы owner
  aligned alloc/free, RX pending helper, RX done и final drain. Ничего не устанавливает.
- firmware_tx: настоящее send_async(context,controller,batch), восемь borrow slots,
  repack в bounce16384, ожидает completion, пишет native batch status, deferred done.
- firmware_tx_binding: новое, НЕ ПРОВЕРЕНО: common RX binding для alloc/free/drain,
  TX pending kind2, TX done 123b8034, собственный fingerprint done.
- firmware_io: новое, НЕ ПРОВЕРЕНО: routes kind1/2, запоминает финализатор, вызывает
  только его после clearing token busy. Требует объединённого lifecycle-теста.
- ops_table/discovery_guard: BE 0x3c RTL ops builder/validation и guarded discovery
  write/readback/rollback. Транзакция ещё не вызвана на консоли.

## Ключевые ABI-факты

Полное описание и доказательства: ../NATIVE_COMPLETION_ABI.md; layouts:
../../native/ios_net_abi.h. Адреса относятся ТОЛЬКО к закреплённой owner 5.5.6.

- Ops: +20 open, +24 close, +28 get_link, +2c send_async, +30 receive_async,
  +34 completion, +38 post_link_interrupt; размер 0x3c.
- Callback: r0 context, r1 controller, r2 kind, r3 batch; stack buffer/token,
  signed_result. Cookie из пятого metadata word штатный callback НЕ получает.
- Worker message metadata +24 kind, +28 interface id, +2c batch, +30 buffer/token,
  +34 cookie; +38 request pointer, +3c USB handle; +4 signed_result.
- USB handle и network interface id — разные значения.
- Pending controller offsets: link +1a4, RX +1a8, TX +1ac. Real helper
  123b6f28 обновляет их, не защищает underflow. Каждый accepted request +1/-1 ровно раз.
- Отрицательный callback return включает worker error path, способный освободить
  metadata.batch. Bound RTL callback поглощает duplicate/stale без повторной доставки
  и отрицательного return; иначе возможен double-free используемого снова batch.
- RX batch: count +38, status +3c, vectors +40, interface id +440, inline ptr +444.
  Firmware allocator 123b75ac: bytes=reserved18+0x467+rx_capacity;
  inline=align_down(batch+reserved18+0x467,32). Буфер бывает 32 mod64!
- Текущий firmware_rx entry требует rx_capacity=0x4000, reserved18=0 и allocation
  geometry именно этого профиля. Bounce64 нужен для строгого RTL UHS transport.
- RX завершение: validate metadata, pending -1, copy validated data into original
  storage, free bounce. rx_done/discard ОТЛОЖЕН до token after_delivery. Real
  rx_done 123b8174 → pacing 123b80ac может синхронно вызвать следующий receive_async.
  Поэтому и token busy, и adapter busy должны быть сняты до native delivery.
- Native input удерживает original batch; release 123b4430 уменьшает interface+14,
  рассматривает refill и frees original. Adapter не должен освобождать его повторно.
- TX batch отличается: total bytes +40, vectors +44, inline +448. Native header=8
  для RTL, native record align2 требует repack. Short TX — failed batch.
- after_delivery/final drain может уничтожить controller. После drain нельзя читать
  ни adapter, ни token state, ни router. Отсоединить active pointers до reclaim.
- cancel native admin callsite: 123baee4, operation2, endpoint bitmask, два нуля на
  стеке. PPC enum3 сюда не переносить. ASIX close masks admin errors; не повторять
  это в новом контроллере. Return из cancel не доказывает drain/queue quiescence.

## Loader и MMU

Mocha NET reserved area 12431900..12432000 = 1792 bytes; Hokaku начинается 12432000.
Полный core туда не помещается. Нельзя писать в «пустой» RAM без владельца.

Allocation probe уже исполнялся в NET init, но выделенная страница была освобождена.
Это не executable heap proof. Нужны retained allocation, известная VA/PA/ownership,
NET domain7 access, cache/TLB maintenance, I-cache coherency, bounded entry/return.

Owner mapper 08124f88 локально проверен эмуляцией: изменяет enclosing L1 domain
для всей области 1 MiB, не только для одной страницы. Соседние mappings нужно
аудировать; не менять domain arbitrary shared allocation. ARM926 не имеет отдельного
small-page XN; отсутствие XN не доказывает безопасное исполнение.

stroopwafel_stage/loader_contract — модели альтернативного backend, не доступный
на этой Aroma runtime loader. MemoryMappingModule PPC не предоставляет ARM NET domain.
Relocation arithmetic, simulated mapping и entry marker object не равны live loader.
