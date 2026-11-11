"""Inspect an OFFLINE NET heap-entry candidate. Never connects to a console.

Writes a disabled review plan (activation_allowed=false), not a patcher or a
boot payload. Private firmware is read locally; no firmware bytes are output.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from check_mocha_alloc_candidate import elf_sections, target, branch, verify_shim

OWNER_SHA256 = "8ac1ae366b2925f66f19b7f50033103f0ecee45847c9198aa78343a8cd9c4e23"
IMPORTS = {"rtl_net_original_init": 0x12300a74, "rtl_net_alloc": 0x123e4394,
           "rtl_net_free": 0x123e439c, "rtl_net_printf": 0x123ccfbc,
           "rtl_net_flush_dcache": 0x123e44e4}
# svc stubs: AllocAligned 0x28, Free 0x29, FlushDCache 0x52.
SVC_STUBS = ((0x123e4394, 0xe7f028f0), (0x123e439c, 0xe7f029f0), (0x123e44e4, 0xe7f052f0))
# NET main local heap: ldr r0,=0x124c6060; mov r1,#0x40000; bl svc 0x24.
HEAP_WORDS = ((0x12300aac, 0xe59f0164), (0x12300ab0, 0xe3a01701),
              (0x12300ab4, 0xeb038e2e), (0x12300c18, 0x124c6060))
LOCAL_HEAP = (0x124c6060, 0x40000)
# mov r3,pc; ldr r2,[pc,#12]; str r3,[r0]; str r2,[r0,#4]; eor r0,r1,r2; bx lr; RTLX
BLOB = (0xe1a0300f, 0xe59f200c, 0xe5803000, 0xe5802004, 0xe0210002, 0xe12fff1e, 0x52544c58)
REQUIRED = ("DLP_FSAInit_patch", "DLP_FSADeinit_patch", "DLP_FSA_OpenFile",
            "DLP_GetChildTitleId", "rtl_mocha_net_init_entry_probe",
            "rtl_mocha_net_entry_probe_after_init", "rtl_heap_entry_blob",
            "rtl_heap_entry_blob_end")


def make_plan(candidate, firmware, mapping):
    if hashlib.sha256(firmware).hexdigest() != OWNER_SHA256:
        raise ValueError("owner firmware hash mismatch")
    sections, symbols = elf_sections(candidate)
    text = sections.get(".text")
    if text is None or text[3] != 0x12431900 or not 0 < text[5] < 0x700:
        raise ValueError("candidate outside reserved Mocha text range")
    for name, s in sections.items():
        if s[2] & 2 and s[5] and name not in (".text", ".rtl_alloc_status"):
            raise ValueError("unreserved allocated section " + name)
    status = sections.get(".rtl_alloc_status")
    if (status is None or status[1] != 8 or status[2] != 3 or
            status[3] != 0x1288d040 or status[5] != 64 or
            symbols.get("rtl_mocha_entry_status") != status[3]):
        raise ValueError("missing/invalid owned status BSS")
    segments = [s for s in mapping["segments"] if s["type"] == 1]
    bss = [s for s in segments if s["vaddr"] == 0x124c6000 and s["paddr"] == 0x124c6000 and
           s["filesz"] == 0 and s["memsz"] == 0x3c7028 and s["flags"] == 0x700006]
    if len(bss) != 1: raise ValueError("NET BSS ownership profile mismatch")
    heap_end = LOCAL_HEAP[0] + LOCAL_HEAP[1]
    if not (bss[0]["vaddr"] <= LOCAL_HEAP[0] and heap_end <= bss[0]["vaddr"] + bss[0]["memsz"]):
        raise ValueError("local heap is not inside NET BSS")
    for s in segments:
        if s is not bss[0] and s["vaddr"] < heap_end and LOCAL_HEAP[0] < s["vaddr"] + s["memsz"]:
            raise ValueError("local heap overlaps another loaded segment")
    for name in REQUIRED:
        a = symbols.get(name, 0)
        if a & 3 or not text[3] <= a <= text[3] + text[5]:
            raise ValueError("missing/out-of-range candidate symbol " + name)

    def read_fw(address, length):
        for s in segments:
            if s["vaddr"] <= address and address + length <= s["vaddr"] + s["filesz"]:
                at = s["offset"] + address - s["vaddr"]
                if at + length > len(firmware): raise ValueError("truncated firmware")
                return firmware[at:at + length]
        raise ValueError("unmapped firmware address")

    for name, address in IMPORTS.items():
        if symbols.get(name) != address: raise ValueError("incorrect binding " + name)
    for address, opcode in SVC_STUBS:
        if struct.unpack(">II", read_fw(address, 8)) != (opcode, 0xe12fff1e):
            raise ValueError("SVC stub mismatch at %08x" % address)
    for address, word in HEAP_WORDS:
        if struct.unpack(">I", read_fw(address, 4))[0] != word:
            raise ValueError("local heap creation mismatch at %08x" % address)
    if target(HEAP_WORDS[2][1], HEAP_WORDS[2][0]) != 0x123e4374:
        raise ValueError("heap creation call target")
    hook = 0x12300ae4
    old = struct.unpack(">I", read_fw(hook, 4))[0]
    if old != 0xebffffe2 or target(old, hook) != IMPORTS["rtl_net_original_init"]:
        raise ValueError("NET init call mismatch")

    def code(address, count):
        pos = text[4] + address - text[3]
        return list(struct.unpack_from(">%dI" % count, candidate, pos))

    entry = symbols["rtl_mocha_net_init_entry_probe"]
    verify_shim(code(entry, 7), entry, IMPORTS["rtl_net_original_init"],
                symbols["rtl_mocha_net_entry_probe_after_init"])
    blob, blob_end = symbols["rtl_heap_entry_blob"], symbols["rtl_heap_entry_blob_end"]
    if blob_end - blob != 4 * len(BLOB) or tuple(code(blob, len(BLOB))) != BLOB:
        raise ValueError("unexpected heap entry blob")
    return dict(status="OFFLINE_ONLY_NOT_INSTALLABLE", activation_allowed=False,
                variant="heap-entry", code_bytes=text[5], text_start=hex(text[3]),
                text_end=hex(text[3] + text[5]),
                status_bss=dict(address=hex(status[3]), bytes=status[5], abi=2,
                                original_bss_end="0x1288d028", proposed_bss_end="0x1288d080"),
                local_heap=dict(start=hex(LOCAL_HEAP[0]), bytes=hex(LOCAL_HEAP[1]),
                                segment="NET BSS 0x124c6000..0x1288d028 uid7 RW identity"),
                heap_blob=dict(address=hex(blob), bytes=4 * len(BLOB),
                               sha256=hashlib.sha256(struct.pack(">7I", *BLOB)).hexdigest()),
                candidate_sha256=hashlib.sha256(candidate).hexdigest(),
                candidate_hook=dict(address=hex(hook), original=hex(old),
                                    proposed=hex(branch(hook, entry))),
                symbols={name: hex(symbols[name]) for name in REQUIRED},
                checks=["owner firmware SHA256", "original NET BL", "alloc/free/FlushDCache SVC stubs",
                        "local heap creation inside NET BSS", "DLP symbols retained",
                        "owned status BSS only", "Hokaku boundary", "seven-instruction shim",
                        "exact 7-word heap blob"],
                unverified=["candidate not installed; console runtime is official",
                            "instruction fetch from NET BSS heap on hardware",
                            "FlushDCache (svc 0x52) clean semantics and return value on hardware",
                            "I-cache holding no stale line for the fresh heap page",
                            "behaviour on prefetch abort (expected IOS crash, recovered by cold boot)",
                            "bootloader zeroing of extended BSS and status visibility",
                            "logging at this init phase", "retained allocation/lifetime for a driver image",
                            "native USB ownership, ops installation"])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("candidate", type=Path)
    p.add_argument("--firmware-folder", type=Path,
                   default=Path("evidence/firmware-5.5.6/analysis"))
    a = p.parse_args()
    plan = make_plan(a.candidate.read_bytes(), (a.firmware_folder / "ios.elf").read_bytes(),
                     json.loads((a.firmware_folder / "map.json").read_text()))
    output = a.candidate.with_suffix(".review.json")
    output.write_text(json.dumps(plan, indent=2) + "\n")
    print(f"entry candidate review: {plan['code_bytes']} bytes; static hook/ABI/blob checks passed; activation disabled")


if __name__ == "__main__": main()
