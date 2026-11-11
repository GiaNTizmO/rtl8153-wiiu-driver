# IOS-NET completion boundary — pinned owner 5.5.6

This is offline firmware evidence, not a callable binding or an installed RTL
driver. tests/test_ios_completion_arm.py pins the original owner ELF SHA256 and
executes completion 0x123b94d8 and counter helper 0x123b6f28 as ARM big endian.
Synthetic context/controller/batch memory replaces live IOS state. Delivery
and shutdown drain helpers are stubs; no real USB submission is performed.

## Arguments and result convention

Completion receives context in r0, controller in r1, kind in r2, batch in r3,
then an additional argument at incoming SP and a signed USB result at SP+4.
The first stack argument is used as a byte buffer in kind 3 (link notification)
and has not been generalized here. RX kind 1 and TX kind 2 consume the signed
result: negative means error; nonnegative is stored in batch+0x3c. RX uses the
nonnegative value as the byte count for ASIX parsing. Do not feed the internal
glue's split status/done convention directly to this firmware ABI without
conversion and matching metadata.

## Pending and batch lifetime

Before dispatch, 0x123b94fc calls 0x123b6f28(context, controller, kind, -1).
The helper updates controller+0x1a8 for RX, +0x1ac for TX and +0x1a4 for link.
It does not reject an underflow; the future adapter must validate outstanding
ownership/cookies before accepting callbacks. Submit helpers increment exactly
once after successful UHS submission. Never decrement both in a wrapper and in
the driver completion callback for one event.

TX stores the result at batch+0x3c and calls 0x123b8034(context,controller,batch).
RX resets count at +0x38, stores result at +0x3c, parses only a successful
transfer, then calls 0x123b8174(context,controller,batch,other). Both paths then
call 0x123b936c(context,controller) to consider shutdown cleanup. This order
means the downstream batch delivery/release and pending counter lifetime must
be coordinated; invoking a cleanup helper too early may free controller DMA.

The native RTL glue currently owns two RX batches itself and explicitly waits
for release_rx. Firmware receive_async is invoked with batches from firmware
queues. Those allocation/release models are not yet adapted to each other.
Do not install ops_table simply because glue lifecycle mocks pass. A real
adapter must decide which layer owns each batch and preserve exactly one
release and one pending retirement per accepted event, including cancellation.

## Test scope

Twenty-four cases exercise RX -64/-1/0 and TX -64/-1/0/48/16384, with pending
counts 1/2/8. Assertions cover real counter helper execution, unchanged other
counters, batch count/status, error-stat counters, delivery then drain order,
r4-r11 and SP preservation. Allowed writes are bounded to stack, the specific
pending/status fields, error stats, and the observed RX timing field +0x530.
Callbacks may clobber caller-saved registers. Positive ASIX RX parsing, kind 3
link completion, actual queue dispatch, real cancellation, DMA cache effects
and hardware ABI reliability remain unverified.

## Worker dispatch and lost cookie

tests/test_ios_completion_dispatch_arm.py now executes owner worker
0x123b83b0 and the real resolver 0x123b3ecc. The original failing fixture had
incorrect message offsets; correcting those offsets is a test-model fix, not
evidence of a firmware defect. Resolver input is the USB interface handle at
message+0x3c, matched against controller+0x18. It is separate from the network
interface id in interface+0. Synthetic values deliberately differ.

The five copied metadata words start at message+0x24: kind at +0x24, network
interface id at +0x28, batch at +0x2c, buffer at +0x30, cookie at +0x34. The
request allocation to free is at +0x38. The signed USB result is at +4.
Worker dispatch forwards context/controller/kind/batch in r0-r3 and
buffer/result on the stack, calling ops+0x34. It does not forward the cookie
or a pointer to the metadata. A cookie retained in a message is consequently
not proof that the current internal native_glue completion API is callable
from that firmware callback. Its signature and result convention differ.

Twelve metadata cases cover RX/TX with results -64/-1/0/48/16384/32768. Nine
additional cases chain the actual ASIX completion and pending counter helper
from real dispatch: RX -64/-1/0 and all six TX results. The integrated cases
prove exactly one pending retirement before delivery, then shutdown drain;
TX lock release follows the driver return, and request free is last. The
metadata message remains byte-identical. Network delivery, queue locking and
request free are still stubs; no hardware/DMA behavior is implied.

A future bridge must carry immutable per-submission identity across the actual
callback boundary and retain it until completion/cancellation is acknowledged.
Looking up a slot's newest cookie by batch or DMA address cannot distinguish
an old callback after reuse. One possible design is a dedicated completion
token in the batch argument, translated back to the real network batch only
inside the RTL callback. Token allocation, non-reuse, cancellation and firmware
error paths must be audited before choosing that design or registering ops.

native/completion_bridge.c and ios_completion_adapter.c remain offline one-shot
tuple validators. Their reset checks local ownership only; this does not prove
that queued/late firmware callbacks are impossible. Neither helper has the
six-argument ops+0x34 signature. They are not installed on the console and are
superseded for repeated transfers by the token wrapper described below.

## RX submit boundary

The owner receive_async entry at 0x123ba6b0 receives the controller and RX
batch used by the caller. It reads the DMA buffer pointer from batch +0x444,
submits a 0x4000 byte bulk-IN transfer with kind 1, and passes the batch and
buffer into the copied completion metadata. The callback later receives that
same pair in the worker's kind, batch, buffer, signed-result boundary.
The pair must stay valid through completion, including failed and cancelled
transfers. tests/test_ios_submit_close_arm.py executes both actual firmware
submit functions and confirms the RX offset, size, metadata words, endpoint,
queue and real pending increment. It does not prove cache maintenance, USB DMA,
or the firmware allocator's ownership rules.

rtl_ios_native_complete_pair() exposes the same firmware tuple to the offline
RTL glue. It performs no cookie lookup: the owner state must have exactly one
pending slot with matching kind, batch, and DMA address. The native glue
lifecycle test exercises RX through this path; the metadata API remains for
the UHS-side mock where the cookie is available. Context and controller
validation is mandatory above this pair handoff. The sustained path below
instead restores the original immutable metadata and uses the cookie API.

## Sustained callbacks through opaque buffer tokens

native/completion_tokens.c wraps the UHS binding. Actual DMA continues to be
passed through bulk_async's data argument. Only the metadata.buffer word is
replaced by a monotonically increasing request token. Original kind, interface
id, real batch, real DMA, and owner cookie are retained in one of ten pending
slots. Completion validates context, controller, kind, batch and token before
restoring that original record to the glue. A failed submit burns its token
and removes the pending slot. Tokens never wrap: exhaustion refuses further
submissions. These are opaque integers, never addresses to dereference.

The actual six-argument entry is rtl_ios_tokens_callback(context, controller,
kind, batch, token, signed_result). It splits negative results into error/zero
length and nonnegative results into status zero/completed bytes. Token lookup
and delivery outcome are distinct: a consumed USB error is not an unhandled
worker message. Stock worker's negative callback path frees metadata.batch;
therefore the bound RTL callback consumes stale/duplicate tokens without
delivering them or returning a negative status that could free a reused batch.
Foreign controller/context still fails lookup. Kind 3/link is not implemented.

Local drain is NOT permission to reinitialize the token counter. Keep the same
token source across reopen unless USB and the worker queue are independently
quiescent. init refuses the currently attached instance. Attach is limited to
one RTL controller; detach requires closing/drained state and external proof
that no more messages can call into the old object. No loader calls attach on
the console. The original host glue owns its own RX batches; the new firmware
RX adapter below separately handles caller-owned batches and native release.

Evidence (all offline):

- test_ios_uhs_metadata_arm.py executes owner UHS 0x123bc0d4, checking that
  actual DMA vectors and cached DMA fields use the real buffer while copied
  metadata can contain an unmapped, unaligned opaque token. Allocator, memcpy,
  request free and IOS ioctlv remain stubs; this is not an IOS-USB hardware test.
- test_ios_completion_dispatch_arm.py includes six opaque-buffer cases through
  the actual resolver/worker, with a callback stub.
- test_ios_tokens_worker_arm.py runs the real owner worker into the relocated,
  compiled RTL callback, then validates restored metadata at the delivery
  boundary, signed-result conversion, repeated buffer use, duplicate rejection
  without batch-free, TX lock ordering and final request-free ordering. USB,
  locks, request free and network delivery are stubbed.
- test_native_glue.c uses the wrapper with real RTL register-model init and
  frame publication for twenty RX cycles reusing one batch/DMA pair. Stale and
  duplicate tokens do not reach the owner, and closing waits for both RX
  completions. The tests do not install ops or exercise real cache/DMA.

## Close, cancellation request and drain

Owner ASIX close 0x123b9e54 cancels its timer, builds a mask from endpoint
descriptors at controller+0x18a/+0x193/+0x19c, and calls native endpoint admin
0x123baee4 with operation 2 and two zero stack words. This native operation
number differs from the PPC UHS enum. flags bit 0 skips admin and chip-specific
disable. Close clears controller+0x1b0 and calls drain 0x123b936c. It does NOT
retire pending RX/TX/link itself, and does NOT propagate an admin error.
test_ios_submit_close_arm.py verifies these instructions with timer/admin/
ASIX-disable/drain stubs. An accepted cancel does not prove USB quiescence.

rtl_ios_uhs_cancel uses the recovered native callsite ABI for the RTL bulk
endpoint mask and preserves errors. rtl_ios_native_close requests cancellation
once after acceptance, permits retry after failure, stops new submissions, and
retains pending DMA until completion. Retained RX also blocks final close.
An absent admin binding keeps the previous externally-driven drain mode; it
does not synthesize cancellation. Real endpoint administration/claim/disable,
pending counter integration and final firmware batch release remain unbound.

## Firmware-owned RX adapter (offline implementation)

Owner allocator 0x123b75ac allocates ops.reserved18 + 0x467 + ops.rx_capacity
bytes, zeros the 0x448-byte batch, writes interface id at +0x440, and sets
batch+0x444 to align_down(batch + reserved18 + 0x467, 32). This can be 32 modulo
64. A blanket 64-byte alignment requirement on firmware's original buffer would
therefore reject legitimate batches. test_ios_rx_lifetime_arm.py executes this
allocator and its failed-submit free path, native input retention at 0x123b3f30,
and native release 0x123b4430: release decreases interface+0x14, considers refill
and frees the original batch. Allocator/free/receiver/input/refill are stubbed.

native/firmware_rx.c implements the exact three-argument receive_async entry
and a separate caller-owned RX state. It checks ops.rx_capacity=0x4000 and
reserved18=0 before using the pinned allocation geometry. Two pending requests
are allowed. Each request borrows the original batch/inline allocation and
uses a separate 64-byte aligned bounce allocation for USB. Failed submission
frees only that bounce, returns the error and leaves batch ownership with the
firmware caller. On completion the original immutable metadata is validated,
pending decreases once, and valid RTL bytes are copied into original inline
storage. Published vectors reference this original storage; opaque fields,
interface id and batch+0x444 remain unchanged. Invalid results/frames publish
count=0 and a negative status instead of exposing invalid vectors.

After bounce-free, completion defers rx_done/discard to firmware_rx_finish via
the token wrapper's after_delivery hook. This is necessary because real
rx_done 0x123b8174 calls pacing 0x123b80ac, which can synchronously allocate and
submit the NEXT RX through receive_async. RX/token busy must already be clear.
The previous batch can then be retained by native input, independently of USB
pending. Native input subsequently releases it through the stock API. The new
adapter never retains an original batch after handoff or resubmits it itself.

Final drain runs after delivery and no adapter/token access follows it: drain
may detach and destroy all controller state. ASan explicitly tests this case.
Closing rejects new submissions and discards each completed original batch;
an endpoint cancel request remains an independent controller-close operation.

native/firmware_rx_binding.c constructs the real ARM host calls after checking
five 64-byte owner code fingerprints: aligned alloc 0x123e4394, heap free
0x123e439c, counter 0x123b6f28, RX delivery 0x123b8174, drain 0x123b936c. It
does not attach/install anything. Memory read failure/mismatch leaves outputs
unchanged; this fingerprint is a version guard, not permission or cache proof.
Heap-free failure is recorded and suppresses final drain; controller error
recovery remains required. The loader must validate the full firmware/profile,
retain executable code, provide coherent DMA and detach before teardown frees
RX/token state. No unverified pointer is installed on the console.

tests/test_ios_firmware_rx_arm.py executes the owner allocator into compiled
receive_async, tokenized UHS submission, the owner resolver/worker, compiled RTL
callback, C firmware binding, real pending helper, real RX delivery, pacing/
refill and native input dispatcher. Eight cases cover a valid frame, empty RX,
USB error, oversized result, malformed aggregate, failed submit, closing, and
actual synchronous refill. Duplicate messages do not decrement/free twice.
Heaps/UHS/cache/USB/input callback/final drain remain stubbed. This proves the
offline call chain and ownership sequencing, not live native Ethernet.

Reproduce with scripts/test_native_rx_arm.ps1, using the existing WSL toolchain
and locally retained owner firmware. The removed test_ios_rx_abi.c merely
mirrored constants in a private validation helper; actual ARM allocation and
integration tests now cover that boundary with stronger evidence.
