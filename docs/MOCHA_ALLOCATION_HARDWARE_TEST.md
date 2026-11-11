# Allocation-only console test and approved reload diagnostics

This plan concerns the 1280-byte NET candidate with a 64-byte status BSS.
It does not install the RTL native ops, claim USB, disable Wi-Fi or execute heap
code. The existing PPC driver remains the working network path. The PC source
and SDK/toolchains remain unchanged. All IOS modifications are volatile.

The owner explicitly approved the follow-up RLDE/HDRP/NETP diagnostic run.
Confirm current idle Menu before FTP; the confirmation request is pending.
Current kernel diagnostics are four owned .data words; their address is derived
from the candidate ELF and recorded in boot-state-profile.json. Boot State
reads them even with no NET hook, after full candidate identity checks. Final
NETP state is explicitly copied into the next boot ELF after NET patching.
Never assume a marker stored in the old running kernel survives a reload.

Audit correction to the first run: the report fetched after reload was byte
identical to the earlier report; a new Boot State invocation was not confirmed.
It therefore proves no post-reload state. The claimed absent mailbox was not
read because the tool skipped it without the NET hook. Official restoration
was confirmed on SD only; fresh official runtime identity was not established.
Also, the retained 00_mocha.experiment-run.rpx is still a loadable suffix in
Aroma setup. Rename it to a non-RPX suffix before the next boot; a different
base name alone does not disable a setup module.

## Evidence already collected

- Official Mocha v0.2.5 file backed up twice; exact release/runtime kernel and
  NET code confirmed. Original RPX SHA256:
  16921b4059bdc434b481c16ef8380e16c89ff8b2ad78da62d19a2d372d0bc529.
- Complete experiment links. Host sanitizer suite passes; 30 actual ARM probe
  scenarios and eight boot-patcher scenarios pass with external IOS/cache/MMU/
  bootrom functions stubbed. Full reload performs 88 whitelisted section writes
  and 222 modeled copies, including final diagnostic-state persistence. A bad
  NET signature gives 87 writes and preserves the completed NETP diagnostic.
  A rejected launch header performs no ELF writes and records only RLDE.
- The owner firmware title shell handler argument layout is verified offline.
  Its downstream launch operations and the hardware reload are unverified.
- Inherited MCP timeout pointer defect fixed in the isolated experiment.
  The compiled MCP/PPC differs from the official release; this is a material
  limitation, despite matching official baseline kernel/NET/FS/USB payloads.

## Owner approval and recovery prerequisite

Before any upload of experiment/00_mocha.rpx, obtain explicit approval to replace
that boot file temporarily, cold boot, and issue exactly one iosu reload from
idle Menu. Owner must be present and able to remove SD while powered off and
restore the verified original using this PC. Do not rely on FTP for recovery
after a failed boot. Keep original recovery RPX outside Aroma's setup directory;
never place a second backup RPX there because it may also be loaded.

Approval to run the read-only Boot State application is not approval for this
boot replacement/reload. Do not infer approval from elapsed time or a command
echo. Never automatically repeat an unanswered reload.

## Exact proposed sequence after approval

1. Confirm idle Menu with no game/browser/loading. Copy current SD Mocha to a
   new evidence directory and verify its SHA256 against the original above.
   Download current RTL config/module metadata for recovery context.
2. Stage the new read-only Boot State WUHB and download it again to compare
   bytes. Upload experimental Mocha to a non-RPX temporary name in the same SD
   directory, read back/compare, then rename to the setup path. If FTP rename
   cannot atomically replace, stop; use the owner/PC SD copy instead. Never
   delete the installed RPX first to make an upload succeed.
3. Owner powers fully off and boots Aroma. A plain return to Menu/relaunch of
   already-patched Mocha does not copy a new payload into IOS.
4. Run new Boot State, close and fetch its report from Menu. Require candidate
   kernel and NET code matches and candidate reload hook. The NET init call
   should still be original and the status is not read at this phase. If either
   identity differs, stop and restore; no reload or memory patch.
5. Ensure TCP receiver is listening on PC 192.168.31.14:7965 and logging is
   enabled on console using the existing owner UI. This is supplemental; the
   status mailbox is the primary allocation result. Confirm idle Menu again.
6. Queue exactly one iosu reload. This requests the coldboot title through
   the existing shell handler. A reply of zero or echo is not completion.
   Observe owner/video and TCP reconnect. If loading hangs, do not queue
   shell commands or use FTP; stop and restore via powered-off SD.
7. Once Menu is responsive, run new Boot State and close to Menu. Retrieve
   SD:/wiiu/rtl8153/mocha-boot-state.log using FTP control 192.168.31.144 and
   active data connection on PC .14 (data may originate from .13).
8. Run Boot State again after reload; do not reuse the pre-reload report.
   Require exact candidate identities, inspect RLDE/HDRP/NETP independently of
   NET hook presence, then NET hook word eb04c385 and a stable status
   snapshot with magic 52544c41, ABI 1, commit adabb3be, stage 4, nonnegative init,
   nonzero 4 KiB aligned page, data=1, alloc=1, free=1 and zero reserved words.
   Accept only the claim that the probe reached allocation/data checks and the
   free call returned. Do not infer code execution permissions or cache proof.
9. Restore verified official Mocha on SD and cold boot again. Retain experimental
   backups only under non-RPX names outside the setup scan. A file replacement
   alone leaves the running experimental patches resident. Confirm responsive
   Menu; use the original identity-only Boot State if needed to verify original
   runtime. Preserve all logs/hashes locally.

## Abnormal or inconclusive outcome

Any hang, unknown code, negative init, failed/misaligned allocation, changed or
absent mailbox, read error, or incomplete counters ends the test. Do not bypass
the guard, change network configuration, register ops or try heap entry. An
absent mailbox may mean hook not reached, initialization failed before it,
unexpected BSS/cache behavior or wrong reload path; it does not diagnose one
cause by itself. Restore official Mocha and analyze evidence first.

Success still leaves physical translation, cache coherence, executable mapping,
NET entry/lifetime, USB cancellation/ownership and actual native callbacks to
verify separately before DHCP or system traffic tests.
