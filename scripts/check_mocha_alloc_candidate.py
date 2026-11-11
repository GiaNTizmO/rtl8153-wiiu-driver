"""Inspect an OFFLINE NET allocation candidate. Never connects to a console.

Writes a disabled review plan, not a patcher or boot payload. Private firmware
is read locally; no firmware segment is copied to the output.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def elf_sections(data):
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x02\x01":
        raise ValueError("expected ELF32 big endian")
    h = struct.unpack_from(">HHIIIIIHHHHHH", data, 16)
    if h[:3] != (2, 40, 1) or h[10] != 40 or not h[11]:
        raise ValueError("expected linked ARM executable")
    if h[5] + h[11]*40 > len(data) or h[12] >= h[11]:
        raise ValueError("invalid section table")
    sections = [struct.unpack_from(">10I", data, h[5]+i*40) for i in range(h[11])]
    for s in sections:
        if s[1] != 8 and s[4]+s[5] > len(data):
            raise ValueError("section exceeds input")
    def content(s):
        return data[s[4]:s[4]+s[5]]
    def string(table, offset):
        end = table.find(b"\0", offset)
        if offset >= len(table) or end < 0: raise ValueError("invalid string")
        return table[offset:end].decode("ascii")
    names = content(sections[h[12]])
    named = {string(names, s[0]): s for s in sections}
    symbols = {}
    for s in sections:
        if s[1] != 2: continue
        if s[9] != 16 or s[5] % 16 or s[6] >= len(sections):
            raise ValueError("invalid symbol table")
        strings = content(sections[s[6]])
        for at in range(s[4], s[4]+s[5], 16):
            name, value, size, info, other, section = struct.unpack_from(">IIIBBH", data, at)
            name = string(strings, name)
            if name and section == 0: raise ValueError("unresolved symbol " + name)
            if name: symbols[name] = value
    return named, symbols


def target(word, pc):
    if word & 0xff000000 != 0xeb000000: raise ValueError("expected ARM BL")
    delta = (word & 0xffffff) << 2
    if delta & 0x2000000: delta -= 0x4000000
    return pc + 8 + delta


def branch(pc, destination):
    delta = destination-pc-8
    if delta & 3 or not -0x2000000 <= delta < 0x2000000:
        raise ValueError("branch range/alignment")
    return 0xeb000000 | ((delta >> 2) & 0xffffff)


def verify_shim(words, entry, original, after_init):
    # Fixed seven-instruction shim, so no hidden instruction/emulator coverage.
    if len(words) != 7 or words[0] != 0xe92d41f0 or words[2:4] != [0xe1a0400d, 0xe3cdd007] or words[5:] != [0xe1a0d004, 0xe8bd81f0]:
        raise ValueError("unrecognized allocation shim")
    if target(words[1], entry+4) != original or target(words[4], entry+16) != after_init:
        raise ValueError("incorrect shim call target")
    # Model only SP/save/restore with firmware and C calls as hostile ABI stubs.
    # Both legal word-aligned SP congruences must round-trip exactly.
    for incoming in (0x200000, 0x200004):
        sp = incoming-24
        original_call_sp = sp
        saved_sp = sp
        sp &= ~7
        if sp & 7 or original_call_sp % 8 != incoming % 8:
            raise ValueError("call stack alignment")
        sp = saved_sp+24
        if sp != incoming: raise ValueError("stack restore")


def make_plan(candidate, firmware, mapping):
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
        symbols.get("rtl_mocha_alloc_status") != status[3]):
        raise ValueError("missing/invalid owned status BSS")
    bss = [s for s in mapping["segments"] if s["type"] == 1 and
           s["vaddr"] == 0x124c6000 and s["filesz"] == 0 and
           s["memsz"] == 0x3c7028 and s["flags"] == 0x700006]
    if len(bss) != 1: raise ValueError("NET BSS ownership profile mismatch")
    required = ("DLP_FSAInit_patch", "DLP_FSADeinit_patch", "DLP_FSA_OpenFile",
                "DLP_GetChildTitleId", "rtl_mocha_net_init_alloc_probe",
                "rtl_mocha_net_alloc_probe_after_init")
    for name in required:
        a = symbols.get(name, 0)
        if a & 3 or not text[3] <= a < text[3]+text[5]:
            raise ValueError("missing/out-of-range candidate symbol " + name)
    def read_fw(address, length):
        for s in mapping["segments"]:
            if s["type"] == 1 and s["vaddr"] <= address and address+length <= s["vaddr"]+s["filesz"]:
                at = s["offset"]+address-s["vaddr"]
                if at+length > len(firmware): raise ValueError("truncated firmware")
                return firmware[at:at+length]
        raise ValueError("unmapped firmware address")
    if hashlib.sha256(firmware).hexdigest() != "8ac1ae366b2925f66f19b7f50033103f0ecee45847c9198aa78343a8cd9c4e23":
        raise ValueError("owner firmware hash mismatch")
    imports = {"rtl_net_original_init": 0x12300a74, "rtl_net_alloc": 0x123e4394,
               "rtl_net_free": 0x123e439c, "rtl_net_printf": 0x123ccfbc}
    for name, address in imports.items():
        if symbols.get(name) != address: raise ValueError("incorrect binding " + name)
    for address, opcode in ((0x123e4394, 0xe7f028f0), (0x123e439c, 0xe7f029f0)):
        if struct.unpack(">II", read_fw(address, 8)) != (opcode, 0xe12fff1e):
            raise ValueError("allocator SVC mismatch")
    hook = 0x12300ae4
    old = struct.unpack(">I", read_fw(hook, 4))[0]
    if old != 0xebffffe2 or target(old, hook) != imports["rtl_net_original_init"]:
        raise ValueError("NET init call mismatch")
    entry = symbols["rtl_mocha_net_init_alloc_probe"]
    pos = text[4]+entry-text[3]
    words = list(struct.unpack_from(">7I", candidate, pos))
    verify_shim(words, entry, imports["rtl_net_original_init"],
                symbols["rtl_mocha_net_alloc_probe_after_init"])
    return dict(status="OFFLINE_ONLY_NOT_INSTALLABLE", activation_allowed=False,
                code_bytes=text[5], text_start=hex(text[3]), text_end=hex(text[3]+text[5]),
                status_bss=dict(address=hex(status[3]), bytes=status[5],
                                original_bss_end="0x1288d028", proposed_bss_end="0x1288d080"),
                candidate_sha256=hashlib.sha256(candidate).hexdigest(),
                candidate_hook=dict(address=hex(hook), original=hex(old),
                                    proposed=hex(branch(hook, entry))),
                symbols={name: hex(symbols[name]) for name in required},
                checks=["owner firmware SHA256", "original NET BL", "allocation SVCs",
                        "DLP symbols retained", "owned status BSS only", "Hokaku boundary", "seven-instruction shim"],
                unverified=["candidate not installed; official runtime identified", "NET boot/reload hook timing",
                            "bootloader zeroing of extended BSS and status cache visibility",
                            "real allocator/context and free behavior", "logging at this init phase",
                            "heap execute permission", "cache coherence", "native USB ownership"])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("candidate", type=Path)
    p.add_argument("--firmware-folder", type=Path,
                   default=Path("evidence/firmware-5.5.6/analysis"))
    a = p.parse_args()
    plan = make_plan(a.candidate.read_bytes(), (a.firmware_folder/"ios.elf").read_bytes(),
                     json.loads((a.firmware_folder/"map.json").read_text()))
    output = a.candidate.with_suffix(".review.json")
    output.write_text(json.dumps(plan, indent=2)+"\n")
    print(f"candidate review: {plan['code_bytes']} bytes; static hook/ABI checks passed; activation disabled")


if __name__ == "__main__": main()
