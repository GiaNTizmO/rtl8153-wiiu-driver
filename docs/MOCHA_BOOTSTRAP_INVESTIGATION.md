# Aroma/Mocha native bootstrap investigation

Latest local revision includes an explicit owned 64-byte BSS status mailbox.
Earlier 1116-byte/zero-BSS measurements below describe the superseded prototype.
Current measurements and deployment limits are recorded at the end of this file.

Status: offline design investigation, not an installed or verified loader.
The console capability report confirms /dev/stroopwafel is absent (rc=-6).
No console memory writes, thread creation, USB claim or boot payload installation
were performed for this investigation.

## Evidence in local sources and owner firmware

- MochaPayload/source/ios_mcp/source/ipc.c, IOCTL_SVC: calls a stub at
  MCP_SVC_BASE + svc_id * 8. It does not change process identity to NET.
- MochaPayload/source/ios_kernel/source/kernel_patches.c: custom syscall
  0x81 implements kernel read32/write32/memcpy and OTP read. It does not
  expose general kernel function invocation or NET thread creation.
- The owner's kernel at 0x0812ca44 copies the current context pid (+0x54)
  into a created thread at 0x0812cb94..0x0812cb98. Changing the address of
  the SVC stub does not establish a NET process context.
- MochaPayload/source/ios_net/imports.ld and owner disassembly agree on
  IOS_AllocAligned=0x123e4394 (SVC 0x28), IOS_Free=0x123e439c (0x29),
  IOS_CreateThread=0x123e4254 (0x00), IOS_StartThread=0x123e428c (0x07).
  These are callable by NET code, not directly by PPC.
- Existing NET code calls IOS_AllocAligned with alignment 64 at
  0x1230d098. Presence of that allocator does not prove allocated heap memory
  is executable, coherent with PPC/kernel access, or persistent across reload.
- ifnet startup at 0x12308640 creates the worker 0x12308a14, priority 0x45,
  stack size 0x4000, flag 1, then starts it and installs queue callbacks.
  This is an existing live networking worker, not an available trampoline.
- MochaPayload/source/ios_net/link.ld reserves 0x12431900..0x12432000;
  it contains DLP patches and borders Hokaku. It must not be overwritten
  with the driver image or treated as unused memory.
- Mocha's section_write in ios_kernel/source/elf_patcher.c extends ELF
  file/memory sizes and shifts later file segments. This is boot-time ELF
  manipulation; it is not a live heap allocation or mapping API.

## Candidate to investigate

A deliberately integrated, small NET bootstrap in a custom Mocha build could
allocate code/data from NET and publish an owned mailbox. Such a bootstrap
would run from an explicit NET initialization hook and preserve all existing
Mocha/DLP/Hokaku behavior. The native driver image could then be copied into
the allocation, relocated and entered only after execute permission and cache
coherence are established. The reserved 1792-byte text range would contain
bootstrap code only, if the combined code fits.

This is a candidate requiring a custom boot payload, not a demonstrated
solution for the installed Mocha. Do not change the boot payload merely to
test this idea. A concrete build, hook ABI, ownership proof, marker-only test
and recovery package must precede any proposed console installation.

Open questions:

1. Installed release identified as official v0.2.5 (see verified backup below);
   local complete-RPX build still differs in MCP/PPC portions.
2. Select a NET initialization hook with proven arguments, saved registers,
   stack use, return behavior and a single initialization lifetime.
3. Prove heap permission, physical alias, coherency and allocation lifetime.
4. Provide a bounded, identity/version-checked command path to the NET worker.
5. Separate marker-only entry testing from USB driver installation.
6. Bind real USB cancellation, native RX release, TX completion, pending
   counters and link events before switching ownership away from PPC.

No currently collected evidence permits native activation on the console.

## Installed Mocha identified and backed up

After the owner confirmed Menu with no active application, the installed
SD:/wiiu/environments/aroma/modules/setup/00_mocha.rpx was downloaded twice
read-only. Both copies match: 66079 bytes, SHA256
16921b4059bdc434b481c16ef8380e16c89ff8b2ad78da62d19a2d372d0bc529.
It is byte-identical to the official MochaPayload_v0_2_5.zip release RPX
downloaded from github.com/wiiu-env/MochaPayload/releases/download/v0.2.5/.
The v0.2.5 tag resolves to the recorded source commit. The verified backup and
manifest are private local evidence under evidence/2026-11-09/mocha-installed/.
The console file was not replaced.

scripts/compare_mocha_rpx.py decompresses RPX sections and searches for each
baseline ARM payload without modifying the saved RPX. NET, kernel, FS and USB
payloads are byte-identical to the local baseline. MCP is not byte-identical;
the precise compiler/dependency cause remains unproven. Do not describe the
local complete RPX as a reproducible copy of the installed release.

## Offline allocation-only candidate

native/mocha_alloc_probe.c and mocha_alloc_probe_entry.S implement a candidate
wrapper for the no-argument NET initialization call at 0x12300ae4. The original
BL is 0xebffffe2, targeting 0x12300a74. On a successful original return, the
candidate allocates one 4096-byte page from local heap 0xCAFE with 4096-byte
alignment, checks the first/last words of that allocation and frees it. It
preserves the original initialization result even if the allocation fails.
It neither copies nor executes code in the heap and does not claim USB.

The original NET main has 32 bytes of register saves plus 52 bytes of local
stack before this call. A seven-instruction ARM shim preserves incoming SP
modulo 8 for the original firmware call, aligns SP to 8 for the C diagnostic,
and restores saved SP and r4-r8/LR. The shim assumes a word-aligned incoming
stack and a no-argument original target. Flags/r0-r3/r12 are caller-saved under
this call boundary; the next firmware instruction compares r0 explicitly.

scripts/build_mocha_alloc_candidate.sh links the candidate together with all
four existing upstream DLP functions, retaining imports from that checkout.
The combined .text is 1116 bytes, starts at 0x12431900, ends at 0x12431d5c
and has zero BSS. The linker asserts the Hokaku boundary at 0x12432000.
The source checkout is MochaPayload commit
383a334fc3e304d403dd69b994baaee3752c01dc; this is not yet established as the
exact build installed on the console.

scripts/check_mocha_alloc_candidate.py checks owner firmware SHA256, the
original BL, allocator/free SVC words, required DLP exports, import bindings,
allocated section bounds and the exact seven-instruction shim. It emits a
review JSON with activation_allowed=false, including the proposed hook word
for review only. It has no patch/write/upload/console operations. The small
SP model verifies only the recognized save/align/restore sequence at both
word-aligned SP congruences; it is not a general ARM emulator or hardware test.
The negative test rejects damaged shim instructions, truncated candidate ELF
and mismatched firmware. The host allocation test checks failure, alignment,
word bounds, cleanup and original result preservation.

Build this local, non-installable artifact with make mocha-alloc-candidate in
UbuntuCafeF. This standalone candidate is not a boot payload. A separate
local experiment now integrates it into the ELF-reload patcher as described
below. No candidate hook is installed by the instant patch path. A hook at
initial startup does not run on the already-started IOS-NET when Aroma first
loads; boot/reload ordering remains a material unknown on the owner console.

## Complete local Mocha experiment and ARM execution checks

scripts/build_mocha_baseline.sh builds the unchanged MochaPayload checkout
using the existing SDK and project-local libmocha/libstroopwafel libraries.
No make install or toolchain modification is needed. The additional
libstroopwafel checkout is pinned in docs/dependencies.json; using it during
compilation does not install Stroopwafel on the console.

scripts/build_mocha_alloc_experiment.sh refreshes an isolated git archive under
build/mocha-alloc-experiment and applies scripts/prepare_mocha_alloc_experiment.py.
The modified net_run_patches checks seven original NET instruction words before
adding its hook to the boot ELF. Existing DLP patch calls use newly generated
symbol addresses, including their 28-byte shift from the assembly-entry ordering.
The combined NET section remains 1116 bytes with zero BSS. The complete PPC
00_mocha.rpx and ARM kernel/NET payloads build successfully. These local
artifacts are not uploaded and are not approved for console boot.

tests/test_mocha_boot_guard.py extracts the actual generated C guard and runs
it against an endian-converted private ELF fixture under ASan/UBSan. It tests
signature failures, invalid metadata and unmapped/unaligned address rejection.
This host fixture is not a bootable firmware image and must not be packaged.
The guard is for the enclosing Mocha loader's trusted in-memory boot ELF; it is
not a general parser for arbitrary untrusted buffers with a supplied length.

The Windows Python environment already provides Unicorn. No package was
installed in UbuntuCafeF. Two additional offline tests execute actual linked
ARM big-endian instructions:

- tests/test_mocha_alloc_arm.py: 30 allocation scenarios with stubbed external
  IOS calls, including both word-aligned SP congruences, preservation of r4-r11
  and the original return value, failed/misaligned allocation, exact page writes
  and one free before logging.
- tests/test_mocha_boot_arm.py: executes net_run_patches, its guard and
  section_write from both baseline and experimental ARM payloads on a private
  copy of the owner ELF. Kernel memcpy/reverse_memcpy are bounded memmove stubs.
  The 28 baseline writes and 29 experimental writes are whitelisted; corrupt
  hook/allocator signatures omit the extra hook. Program headers and every
  PT_LOAD payload match an independent insertion oracle. A further scenario
  executes the actual kernel_launch_ios and preceding MCP/kernel/FS patchers:
  87 section writes, 221 modeled copies, and preserved bootrom arguments.
  A bad NET signature produces 86 writes and omits the new hook. Interrupt
  helpers, CP15/MMU effects and bootrom execution are stubbed; hardware/cache
  behavior remains unproven.

Upstream section_write extends p_filesz/p_memsz and shifts later file payloads
but does not zero the gap from 0x12431844 to 0x12431900. The experiment does not
read or execute those gap bytes. Never infer a free zero-filled allocation from
ELF padding or from this offline test.

Next unresolved integration fact: when Mocha is first entered, instant_patches
copies its NET payload into an already-running IOS. This experiment installs
the hook only from kernel_launch_ios/net_run_patches on a later IOS reload.
The installed Mocha release is now identified. The owner environment's reload
sequence and test recovery procedure must still be established before proposing
an actual boot test. Even a
successful allocation test would leave heap execution and cache coherence
unproven; the page is never entered by this candidate.

## Inherited MCP timeout pointer defect in the reload path

Full ARM execution reached an upstream table entry in
source/ios_kernel/source/ios_mcp_patches_asm.s:

    .word 0x05022474, 0xFFFFFFFF, 4

patch_table_entries passes the second field as a source pointer to section_write,
which reads a word at that address. The emulated reload attempts to read
0xffffffff. The installed RPX contains the byte-identical baseline kernel
payload, so this is not introduced by the NET candidate. The hardware behavior
of that invalid read has not been tested; do not infer that an ordinary Menu
transition invokes this path or explains prior hangs.

The isolated experiment changes the table data field to rtl_timeout_word,
which points to an actual .word 0xffffffff placed after the table end label.
The table length is unchanged. That explicit prerequisite fix allows the full
reload patcher emulation to finish. Original upstream sources and the console
payload remain unchanged. This deviation is material and must accompany any
future review or console-test proposal.

## Read-only runtime identity probe

tools/mocha_boot_state is a headless PPC application. It reads the reload
instruction at 0x0812a120 and NET initialization call at 0x12300ae4 through
Mocha_IOSUKernelRead32. Only a recognized reload branch permits further reads
of the fixed Mocha kernel/NET code windows. It compares all immutable .text
bytes with the official baseline and the allocation-only candidate; the final
partial NET word excludes padding. It never follows runtime pointers.

scripts/make_mocha_boot_profile.py generates the comparison tables only after
verifying the saved original RPX SHA256, its embedded kernel/NET payloads, and
the candidate review hash. Build with make mocha-boot-state. The tool does not
write IOS memory, allocate IOS memory, call ARM code, claim USB, change logging
configuration or issue a reload. Its only persistent output is the SD log at
wiiu/rtl8153/mocha-boot-state.log. No graphics initialization is used.

The host harness tests actual probe C under ASan/UBSan: official and candidate
code, candidate copied without the NET hook, unknown reload hook, initialization
failure, hook/payload read failure, corrupted NET/kernel code and fixed address
bounds. These tests do not replace execution on the console. A code match
proves identity only, never allocation, free, reload execution or cache behavior.

After the owner-confirmed idle Menu, the read-only WUHB was uploaded by active
FTP and downloaded again for byte comparison (70456 bytes, SHA256
fae78cebaf9899629f8f937b57d8e240821bfa7f9407e26e81e44948d8bec13b).
Immediately before upload, the SD Mocha still matched the verified official
v0.2.5 SHA256. No boot/module file was written. Console execution and its
report were subsequently confirmed: the owner ran the application and returned
to Menu. The retrieved runtime.log records reload hook eb003005, NET init
ebffffe2, and official=MATCH for complete kernel/NET .text. The allocation
candidate does not match and its NET hook is absent. Inspection completed.
This is positive evidence of the currently installed official code, not a
native allocation/entry test. No identical probe rerun is needed.

## Reload path evidence and remaining gate

In the recorded source, instant_patches_setup(0) installs the BL at 0x0812a120
to kernel_launch_ios. That function patches a new IOS ELF only when its launch
header has the expected 0x00dfd000 word. The candidate NET hook is inserted
there; copying candidate payloads into a running IOS leaves the NET init call
unchanged. Re-entering an already-patched Mocha also skips payload installation
when ExecuteIOSExploit finds /dev/iosuhax. Therefore neither an ordinary title
transition nor merely re-running Mocha establishes this execution path.

syslogserver.c exposes `iosu reload`, implemented by shellCommand_title_launch
at 0x0510c9a1 with gCurrentColdbootTitle. Its source comment says the argument
handling is not fully understood. Offline execution of the owner's actual
Thumb handler now confirms that it loads title halves from argv[2]/argv[3]
as integers, accepts an optional flag at argv[4] only for argc > 2, and calls
0x0502acb8, 0x0501e7a0, then 0x0501f188 when preceding results are zero.
It returns zero even when any of these external calls reports an error.
tests/test_ios_reload_command_arm.py runs eight scenarios against the pinned
owner firmware, preserving the handler's real register/stack behavior. External
prepare/configure/relaunch calls are stubbed; no actual launch occurs.
This is a possible full-relaunch trigger,
not a verified procedure for the owner's current environment. No such command
has been sent for this investigation. A manual power cycle clears the live
patches and returns to initial payload installation; it does not by itself
exercise this candidate's reload-only NET hook.

Before any allocation experiment, review the exact reload trigger and recovery
procedure. TCP logging is not guaranteed to survive reload: source shows it is
started by an explicit IPC command, while catch-up availability depends on the
new logging buffers. Require positive RTL_NET_ALLOC evidence; absence of a line
must never be interpreted as allocation failure or success. Native heap entry,
USB ownership and RTL ops registration remain separately gated.

## Allocation status mailbox revision (local only)

The current combined NET code is 1280 bytes at 0x12431900..0x12431e00, still
below Hokaku at 0x12432000. Its only allocated writable section is 64 bytes of
NOLOAD .rtl_alloc_status at 0x1288d040. The experiment explicitly extends the
owner's NET BSS end from 0x1288d028 to 0x1288d080 (88 bytes including padding).
Both ends lie in the same 4 KiB page. This does not introduce a new mapping or
change a domain. No writes to this location are permitted in stock live IOS.

Before installing the hook, the boot guard now requires the exact unique NET
BSS segment: identity physical/virtual base 0x124c6000, zero file size, memory
size 0x3c7028 and flags 0x700006. It also rejects overlap of the added BSS bytes
with any loaded segment, and address overflow. A mismatching profile omits both
the NET hook and the status BSS extension. The original/DLP patch path remains.

native/mocha_alloc_status.h defines the 64-byte ABI: magic/version, terminal
stage, original NET init result, allocation address, data comparison, alloc/free
call counts, zero reserved words and a final commit word. The allocation probe
sets the commit only after init failure, allocation failure, or return from the
single free call. No freed page is dereferenced afterwards. A free call returning
is the observable claim; the probe does not prove allocator-internal cleanup.
No code is copied into or executed from the page and USB is never claimed.

The updated Boot State reads this BSS only when both entire candidate code
identities match, the candidate reload branch matches, and the NET init hook is
present. It reads twice and rejects differing snapshots, bad magic/version,
missing commit, nonzero reserved words or incomplete success fields. It never
follows the reported allocation address. This updated WUHB is built locally;
the WUHB previously verified on the console remains the older identity-only
version. No experimental RPX or updated WUHB has been uploaded in this revision.

Host tests cover all terminal stages and negative status states. Thirty actual
ARM allocation scenarios check original result, r4-r11/SP, owned-page bounds,
status writes and commit ordering around external IOS calls. Full ARM reload
patch emulation validates the BSS extension, all PT_LOAD/PHDR content against an
independent oracle, and omission on signature mismatch (87/86 section writes;
221 modeled copies). The guard's host test rejects changed BSS size, physical
base and nonzero file size. IOS functions/cache/bootrom remain stubbed.

The mailbox removes the requirement that a TCP connection survive the reload.
It does not prove real bootloader zeroing of the extended BSS or coherent status
readback. An absent/incomplete mailbox remains inconclusive and must not cause
automatic retries, further IOS writes or driver activation. A future hardware
test still requires a reviewed reload trigger, explicit owner approval for the
experimental boot file, and recovery of the verified original from the PC/SD.
