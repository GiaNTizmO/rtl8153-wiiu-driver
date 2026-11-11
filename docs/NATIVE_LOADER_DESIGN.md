# Native loader: constraints and staged implementation

This is a design record, not a claim that native mode can be installed yet.
The console currently has Aroma and the PPC RTL8153 module. No dependency on
Stroopwafel is introduced. Toolchain installations remain untouched.

## Code image implemented offline

scripts/build_native_core.sh emits an ARM926EJ-S, big-endian ET_REL file,
build/native/rtl8153_ios.rel.elf, in addition to the static archive.
The ARM image contains only IOS-NET-side code. `native/stroopwafel_stage.c`,
`native/loader_contract.c` and `native/ios_profile.c` are loader-side PPC
orchestration and validation; they remain covered by host tests and are not
linked into the ARM image because their callback API is not an IOS-NET ABI.
scripts/relocate_ios_image.py parses allocated sections, zeroes BSS, resolves
symbols and applies R_ARM_ABS32/CALL/JUMP24. Imported calls use absolute ARM
literal veneers, so external functions need not be within 32 MiB of the code.
Unknown relocations, missing imports, unsupported alignment and overflow fail.
It produces a byte image and export/import/section/hash manifest.

The supplied load address is an offline parameter ONLY. An arbitrary example
address is not a reservation and must never be copied into the console.
The core still has no installed native ops table or boot entrypoint.

native/entry_probe.c is a separate, dependency-free entry probe. It writes
the marker 0x52544c50 (RTLP) to a caller-provided word and returns the same
value. make native-entry-probe builds it with the existing devkitARM,
relocates it offline at an explicit test base, and checks its manifest export.
This artifact is suitable for a future mapping test; it is not uploaded or
executed by the Aroma module.

tools/loader_capability_probe is a separate read-only PPC application for a
later console check. It opens /dev/stroopwafel and queries only the documented
API version ioctl. Mapping, IOSU writes, IOS execution, USB access and
ops-table changes are disabled in the source. Its purpose is to distinguish an
Aroma installation with no Stroopwafel backend from an environment where the
native loader path can be tested.
The current packaged artifact is
`dist/loader-capability-probe/wiiu/apps/rtl8153_loader_capability_probe.wuhb`.
A successful result is only `version=0x010000`; it does not imply that mapping
or execution is safe. The next probe must still use a disposable mapped page and
the dependency-free entry marker before any native callback or discovery change.

tests/test_relocation.py independently links the same object with GNU ld at
three bases, compares all allocated core bytes except rewritten import branches
and checks the final destination of every branch through each veneer. It also
checks BSS zeroing, bad ELF inputs, overflow and missing imports. This verifies
relocation arithmetic, not execution permissions, caches or runtime correctness.

## Allocation and execution are separate

Mocha exposes Mocha_IOSUCallSVC via an MCP service. SVC calls made this way run
in the MCP caller context; creating a thread there does NOT create an IOS-NET
thread. Local heap 0xCAFE and cross-process heap 0xCAFF also must not be confused
with permanently reserved executable memory. Cross-process accessibility alone
does not prove an IOS-NET execution mapping or persistence/ownership.

Public MochaPayload maps its own reserved MCP regions with kernel function
0x08124f88, a 24-byte descriptor {paddr,vaddr,size,domain,type,cached}. Analysis
of the owner's kernel confirms the function is present and checks page
alignment. Stroopwafel's plugin loader also changes domains/permissions; those
patches cannot be assumed on a plain Aroma installation. No mapping or domain
patch has been made during this work.

Mocha's IOS-NET extension at 0x12431900..0x12432000 has only 1792 bytes, before
Hokaku at 0x12432000. It is neither large enough nor unowned space. Existing
code must not be replaced merely because the driver needs room.

Required before deployment:

1. Obtain an actual allocated block with known owner and lifetime; retain it
   while ARM code, callbacks or USB DMA can reference it.
2. Verify virtual/physical mapping, permissions and IOS-NET domain access.
3. Relocate into that allocation, copy coherently, flush data/instruction
   caches and verify the copied image before exposing any callback pointer.
4. Enter code from a verified IOS-NET worker context, with a bounded control
   path and error reporting. No execution from a PPC address or MCP context.
5. Register RTL ops and discovery without disturbing the original ASIX ops.
6. Release PPC UHS ownership and disable its socket hooks before native claim.
7. Install volatile native activation only after these pieces are validated;
   retain enough bookkeeping to stop submissions and drain callbacks on error.

The shared loader contract now rejects unaligned/overlapping result words,
incomplete entry instructions at the image end, undefined mapping flag bits and
unknown loader backends. Invalid entry geometry/marker leaves both phase and
entry unchanged. Stroopwafel preparation uses the same geometry validation
before asking a backend to map memory. These are local validation checks; a
caller still has to prove permissions, coherency and allocation ownership.

Offline execution of the owner's actual 0x08124f88 small-page mapper is covered
by tests/test_ios_map_arm.py. Twenty-three scenarios check PTE access/cache
bits, fresh L2 allocation, invalid alignment/size, descriptor copying and saved
registers. Cache/TLB helpers are stubbed and no real MMU is exercised. Crucially,
the wrapper passes update=1 to 0x08124c24, which rewrites the enclosing L1 coarse
descriptor's domain before installing a PTE. A test with an existing domain 1
and neighboring PTE confirms it changes that entire 1 MiB region to domain 7,
while leaving the neighboring PTE bits unchanged. Thus a standalone aligned
page cannot be assigned a different domain without auditing all neighbors in
its L1 region. Do not map an arbitrary shared/local allocation into a new
domain or assume page-level domain isolation. ARM926 has no separate small-page
XN bit; access/domain/alias/cache proof is still required before heap entry.

## Native USB transport and ownership implemented locally

The recovered firmware completion boundary and its distinct batch ownership
requirements are recorded in NATIVE_COMPLETION_ABI.md. Actual owner ARM code
has passed 24 completion/counter scenarios offline with downstream helpers
stubbed. This does not bind the glue to the firmware worker or settle who owns
the RX batches passed to receive_async.

native/uhs_transport.c adapts register control requests to the recovered
internal sync UHS ABI and prepares native async bulk metadata. Native USB calls
return status rather than the PPC byte-count convention: status zero is treated
as requested control length, consistent with ASIX's native wrapper. The actual
short-control-transfer behavior remains a hardware question. No firmware
addresses are bound by this component.

native/batch_bridge.c reads big-endian native TX memory, validates each inline
frame address and repacks two-byte native alignment into four-byte Realtek
alignment. RX vectors reference validated payloads inside the completed DMA
buffer; opaque IPC fields remain unchanged. These helpers do not transfer
ownership or invoke firmware callbacks.

native/transfer_owner.c models two RX and eight TX slots, prevents reuse while
USB or native input owns a buffer, accepts out-of-order completion, rejects
stale metadata after slot reuse using a non-repeating submission cookie and
requires cancellation acknowledgements before draining. The fifth metadata
word is copied as part of the 20-byte record by the recovered UHS bulk code
(123bc22c..123bc240), but stock worker dispatch does not pass that word or a
metadata pointer to ops+0x34. The completion record therefore cannot be
reconstructed safely by looking up the latest cookie for a reused batch.
native/completion_tokens.c now preserves per-submission identity through the
metadata.buffer word that actually crosses the callback boundary, independently
of the real DMA pointer passed to UHS. The exact six-argument callback has been
executed by the real owner worker in ARM emulation, including duplicate error
paths. Owner UHS builds real DMA vectors while copying this opaque word intact.
See NATIVE_COMPLETION_ABI.md for the proof and lifetime restrictions. Hardware
mapping, registration, pending counter integration and caller-owned RX batch
adaptation remain absent. Serialization is the native worker's responsibility.

Still required: actual open/close/getlink/send/receive/completion/postlink
operations, endpoint acquisition/administration, native pending-counter updates,
timer/link notification, error completion/release and the loader. These are
not replaced by a passing host state-machine test.

## Ops table and activation transaction

native/ops_table.c builds the 60-byte big-endian RTL8153 driver table. It
validates every callback address against the relocated image range.
native/discovery_guard.c is a read/compare/write/readback transaction for the
observed discovery slot at 0x124ce314: it requires the exact old ASIX pointer
0x12456dd0 and zero sentinel, writes the new pointer and sentinel, and restores
both words on any failure. Host tests cover stale contents, partial writes,
readback and rollback.

The transaction is intentionally not called by the PPC Aroma module. Callback
addresses are meaningful only after the image is executable in IOS-NET; writing
the table before that would direct the native worker into unmapped memory.

## Current deployment gate

The owner ran Loader Capability Probe on 2026-11-09 and returned to Menu.
The report was retrieved over FTP at 192.168.31.144 and saved locally as
`evidence/2026-11-09/loader-capability-probe.log`. It records:

    Stroopwafel unavailable; IOS_Open rc=-6; no native action
    Inspection complete. HOME -> Close to return.

This confirms IOS_ERROR_NOEXISTS for the Stroopwafel resource-manager node
in this session. Do not require another identical probe run without a change
to the boot environment. The existing Stroopwafel stage is an alternative
backend only; it cannot load native code in the observed Aroma session.

The Aroma/Mocha alternative and its unresolved boot/runtime boundaries are
recorded in MOCHA_BOOTSTRAP_INVESTIGATION.md. It is not an active loader.

The approved 2026-11-09 follow-up allocation experiment did run on hardware:
evidence/mocha-diagnostic/20261109T114201520106Z/boot-state.log records
RLDE/HDRP/NETP, NET hook eb04c385 and allocation stage 4, init=0, page=124c7000,
data=1, alloc=1, free=1. This demonstrates the limited NET init allocation path,
not heap code execution or an allocated driver image. Official Mocha was then
restored and a fresh official-runtime Boot State was retrieved at
evidence/mocha-diagnostic/20261109T115136125509Z/boot-state.log. No new native
code has been installed since restoration.

The next Aroma/Mocha investigation must establish allocation lifetime,
NET execution context and cache coherence before preparing an active probe.
Mocha's MCP IOCTL_SVC handler invokes MCP_SVC_BASE + svc_id * 8; its thread
creation therefore does not establish a NET worker. Neither an out-of-range
SVC index nor an arbitrary zero-filled firmware region is a verified loader.

The owner setup is Aroma/Mocha, not a verified Stroopwafel IOS plugin
environment. Mocha can call MCP SVCs and kernel read/write services, but that
does not establish an IOS-NET thread or an IOS-NET executable mapping. No
evidence in this task proves that a shared allocation can be entered by
IOS-NET or survive the relevant IOS lifecycle.

Native installation remains gated on proof of allocation and physical address,
mapping into IOS-NET domain 7 with execute permission, cache coherency, a
bounded call from the IOS-NET worker, and USB ownership handoff. Only then may
the discovery transaction run.

The official Aroma MemoryMappingModule was checked as a possible backend. Its
public API allocates and translates Cafe OS/PPC effective addresses, uses the
PPC kernel module and page tables, and exposes areas beginning at 0x80000000.
It does not expose the IOSU /dev/stroopwafel interface, IOS-NET domain 7
mapping, or an ARM entry call. It is therefore useful for PPC memory work but
cannot satisfy this native loader gate. native/loader_contract.c now models
the required Stroopwafel-side contract and rejects wrong domains, unaligned or
overflowing mappings, invalid entry ranges, and any attempt to arm before the
entry probe has returned its marker.

`native/native_glue.c` provides the next offline-tested layer above the
RTL8153 chip core: it pre-arms two RX transfers, repacks TX batches with the
Realtek header, validates completion cookies through the ownership state
machine, publishes validated RX vectors, reports link state, and refuses to
free buffers while callbacks remain pending. It receives host and UHS
callbacks from an external adapter; it does not call recovered IOS-NET
addresses by itself and is not installed by Aroma.

The glue now retains delivered RX memory until an explicit release carrying
the matching batch address and submission cookie. The host receives that cookie
with delivery; it must preserve it until the native input release callback.
No RX resubmission occurs while native input owns the DMA buffer. Closing
waits for both USB completions and retained input batches. TX completion returns
the original batch and the owner's normalized result, including short-transfer
failure, exactly once. Host callbacks are serialized and must not re-enter the
driver. The future firmware adapter must implement this contract; it is not yet
bound to IOS-NET callbacks or deployed.

The lifecycle test now runs the RTL8153B register model through successful
open, two initial RX submissions, retained RX/release, stale completion/release,
TX success/short failure, partial-allocation cleanup and shutdown drain.
These host checks establish the glue contract, not hardware cancellation or
real IOS-NET worker execution. Cancellation requests must still be supplied by
the firmware adapter, and a rejected/failed RX is reported to that adapter.

## Discovery detail

The existing registration helper at 0x123b8e7c embeds the ASIX table pointer
0x12456dd0 (literal at 0x123b9014). Its r1 is an enable/disable selector,
not a caller-supplied operations table. Calling it with a Realtek table address
would not register that table. A new path must use the lower-level discovery
API/slot bookkeeping or a guarded dispatch extension; spoofing VID/PID is
insufficient because ASIX hardware commands are incompatible with RTL8153.

## Heap-entry diagnostic candidate (2026-11-09, local only)

The next gate after the console allocation result is implemented as an
isolated variant: one page of NET's own local heap (created by NET main from
NET BSS 0x124c6060..0x12506060, uid 7, identity mapped) receives a 28-byte
position-independent blob, is cleaned with NET's IOS_FlushDCache (svc 0x52)
and entered once from the reload-only NET init hook; marker, executing PC and
return are committed to the owned mailbox. No mapper, domain change, USB or
ops. Locally built and tested only; see MOCHA_ENTRY_DIAGNOSTIC.md for evidence,
stubs, the hardware procedure and stop conditions. Not installed.
