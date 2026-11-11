# Native progress after official Mocha restoration

The latest console evidence is official kernel/NET identity with original NET
init ebffffe2, report invocation 00bb28cc399c4f5d. The preceding approved
allocation diagnostic reached RLDE/HDRP/NETP and stage=4/init=0/data=1/alloc=1/
free=1. Reports are retained under evidence/mocha-diagnostic. No subsequent
console command, memory write, boot replacement or native activation was made.
Toolchain files were not changed.

## Implemented locally

- Native UHS endpoint-cancel wrapper using the observed operation 2, bulk-IN/
  OUT endpoint mask and two zero stack arguments. Error propagation is kept;
  stop/cancel predicates do not block the shutdown request itself.
- Glue close requests cancellation once after acceptance, allows retry after
  failure and keeps all pending/retained buffers until their individual
  completion or native-input release. Partial-open failure uses the same drain.
- Tokenized completion wrapper supports sustained reuse of real batch/DMA
  memory while preserving immutable original metadata/cookie. A token is
  opaque and never dereferenced, never wraps and is burned on submit failure.
  Wrong context/controller/kind/batch and duplicate/stale tokens cannot reach
  delivery. It is linked into the ARM relocatable image.
- A real six-argument callback entry attaches one RTL controller. Stock worker
  frees metadata.batch on a negative callback return, so the entry consumes
  rejected duplicate/stale messages for its bound controller without delivery.
  This prevents a duplicate from triggering worker-side free of a reused batch.
  Internal delivery failure is separate from message consumption.

## Evidence and limits

The full WSL ASan/UBSan suite covers token submit failure, no wrap, metadata
restoration, twenty RX reuse cycles, short/success/cancelled TX, retained RX,
failed cancellation/retry and no DMA free before acknowledgement.

Actual pinned-owner ARM execution covers 8 RX/TX submit, 8 ASIX close, 4 UHS
bulk metadata, and 27 worker-dispatch scenarios. The close test proves that
ASIX close does not retire pending counters or propagate endpoint-admin errors.
The UHS test proves the real DMA vector remains separate from opaque metadata.
The worker test confirms that the opaque field is forwarded without a read.

test_ios_tokens_worker_arm.py executes the actual owner worker/resolver into
the compiled and relocated RTL callback. Eight cases cover sixteen transfers,
signed errors, repeated buffer reuse, duplicates, metadata restoration and
lock/free ordering. UHS, locks, request free and network delivery are stubs;
cache, MMU, USB DMA and firmware queue quiescence are not hardware-tested.
GNU ld comparison verifies the current ARM relocation arithmetic at three bases.

The older completion_bridge/ios_completion_adapter remain one-shot offline
validators, not sustained runtime or exact ops callback functions. Their local
drained/reset checks do not prove there are no queued old messages. The new
token module does not restart its counter during a live controller's lifetime.
Detach/reinit requires external proof of USB and worker quiescence.

## Required next work

1. Firmware-owned RX is now implemented offline in firmware_rx.c and
   firmware_rx_binding.c. Owner allocator/worker plus compiled adapters, real
   pending helper, delivery/pacing/refill and input dispatcher pass eight ARM
   cases. Original batch release is verified separately. Hardware cache/DMA
   and final controller drain/teardown remain unverified and gated.
2. Complete the matching firmware TX adapter and shared controller lifecycle,
   including RX/TX completion routing, pending counters and final teardown.
3. Implement real controller discovery/open, endpoint setup/disable, link
   notification/timer and error handling around the RTL register core.
4. Complete Aroma/Mocha loader allocation/mapping/cache/entry before exposing
   callback pointers. Allocation probe is not executable mapping proof.
5. Prepare a reviewable next diagnostic package before requesting hardware
   activation; retain official SD recovery. Handoff from the PPC driver,
   native ops registration, DHCP and native system traffic remain unimplemented.
