# Reload diagnostic candidate and evidence limits

The approved follow-up diagnostic test completed on 2026-11-09. Fresh report
evidence/mocha-diagnostic/20261109T114201520106Z/boot-state.log records candidate
kernel/NET identities, reload eb0030b0, NET init eb04c385, RLDE/HDRP/NETP,
and stage=4 init=0 page=124c7000 data=1 alloc=1 free=1. Its invocation stamp
00bb28c39698cccf differs from the pre-reload report's 00bb28bd6dec092e; the
owner confirmed both the reload and the new application invocation. The
pre-reload report was not separately archived on SD before reload: both
downloaded reports and their differing hashes/stamps are retained locally.

FTP rename-overwrite returned 550 File exists, so no FTP boot replacement was
attempted after that failure. The owner powered off and connected SD as G:.
Diagnostic installation/restoration used the SD directly. Both retained
experiments now have non-RPX suffixes. Official Mocha was restored and the
owner cold-booted and launched Boot State again. Fresh official report:
evidence/mocha-diagnostic/20261109T115136125509Z/boot-state.log, invocation
00bb28cc399c4f5d, reload eb003005, NET init ebffffe2, kernel/NET official=MATCH.
Official RPX hash is 16921b4059bdc434b481c16ef8380e16c89ff8b2ad78da62d19a2d372d0bc529.

The allocation test proves only that NET init returned and the limited
allocation/data-check/free call path completed. Executable heap, cache/MMU,
full native image entry and USB integration remain unverified on hardware.
USB pid-21 acquisition/release traces belong to the existing PPC driver;
they do not establish native IOS-NET RTL ownership.

The first run positively proves candidate kernel/NET code after initial cold
boot. In that earlier run the report retrieved after reload was identical and no fresh invocation
was confirmed. Consequently hook rejection, invocation and mailbox outcome
after reload are unknown. The final official SHA256 proves the SD file, not
runtime identity. The later follow-up above supersedes those unknown outcomes;
the old run itself remains inconclusive.

Current candidate changes:

- Four owned kernel .data words record entry (RLDE), accepted header (HDRP),
  completed NET patcher (NETP), with a fourth zero reserved word.
- Kernel patching copies the live kernel before NETP is written. An explicit
  final section_write copies the diagnostic array into the new ELF so NETP
  does not disappear solely due to that ordering.
- Boot State reads diagnostics after complete kernel/NET identity and reload
  branch match, even if the NET hook is absent. The NET BSS mailbox remains
  gated on the NET hook. It never follows the recorded allocation pointer.
- Zero diagnostics mean no retained observation; they do not prove that the
  hook never ran. Fresh boot payload installation may reset kernel data.
- Existing NET startup hook is still reload-only. Installing it live after
  Aroma starts would miss the already-finished NET initialization, so initial
  payload copying is not a demonstrated alternative trigger.

Offline proof:

- Eight actual ARM boot scenarios run with cache/MMU/bootrom stubs. Valid launch
  records RLDE/HDRP/NETP and performs 88 ELF writes / 222 modeled copies. A bad
  NET profile records the same diagnostic completion, performs 87 writes and
  omits NET hook/status BSS. A bad launch header records only RLDE with zero
  ELF writes, zero copies and byte-identical input image.
- Writes are restricted to stack, bounded ELF buffer or the exact 16-byte
  diagnostic array. CP15 MRC c1 results go to their decoded destination
  register. The prior assumption that it was always r6 was incorrect (the new
  compiler output uses r7). CP15 effects remain modeled, not verified hardware.
- Host probe tests check reading diagnostics without a NET hook, marker
  interpretations, read failures, code mismatches and status exclusion.

For hardware, require a new Boot State invocation after each reload/restore.
Archive or rename the previous SD report before the next invocation, so an old
file cannot satisfy the evidence gate. Require owner confirmation of Menu
before FTP. Use only one reload command, once the receiver's actual command
directory and live connection have been inspected. Preserve official recovery
RPX on PC; any retained backup in setup must have a non-RPX extension.

## Console file operations

scripts/mocha_diagnostic_deploy.py defaults to verify-local, with no network
contact. Console operations require --menu-confirmed only after a new human
Menu confirmation. This switch records that prerequisite, it does not grant
permission or detect Menu from FTP responsiveness. The owner has approved this
diagnostic variant; the pending confirmation concerns current console state.

install verifies local manifest hashes, backs up active boot/app, validates and
disables the known old experiment RPX, rejects unknown extra Mocha RPXs, stages
both files under non-loadable suffixes, verifies readbacks, and renames the
staged boot directly over the destination. It never removes/renames away the
active boot first. A server refusing rename-overwrite requires SD recovery/copy;
the script stops with the original boot destination present. Restore likewise
requires the exact candidate SHA256 before installing the verified official
image. It leaves the diagnostic app available for official-runtime inspection.
No reload/launch/IOS memory commands are present in this script.

archive-report first downloads the prior log then renames it away on SD. The
next report must be created by a new invocation. Boot State logs OSGetTime
ticks at entry as supplementary provenance, not a cryptographic nonce or proof
of a full IOS reload (ticks may repeat across boots). Eight mock FTP tests cover
correct replacement, refused overwrite, corrupted upload, unexpected boot,
staging collision, unreviewed module, unknown backup and report archival.

fetch-report requires --after-archive pointing to a prior archive operation's
evidence, rejects byte-identical previous logs, preserves the downloaded report
and writes observation.json only after validation. mocha_boot_report.py requires
one invocation stamp, completion marker and code identity fields; rejects
duplicates, truncated text, unknown diagnostic identity and allocator results
without the NET hook. It classifies kernel markers independently from allocator
results and always reports native_activation_allowed=false. An invocation stamp
alone does not prove freshness, so confirmed new execution and prior archival
remain essential. Six parser tests and ten mock FTP tests pass locally.
