"""Negative checks for the offline allocation candidate reviewer."""
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "scripts"))
from check_mocha_alloc_candidate import elf_sections, make_plan, verify_shim


def main():
    folder = root / "evidence/firmware-5.5.6/analysis"
    source = (root / "build/mocha-alloc-candidate/candidate.elf").read_bytes()
    firmware = (folder / "ios.elf").read_bytes()
    mapping = json.loads((folder / "map.json").read_text())
    plan = make_plan(source, firmware, mapping)
    assert plan["activation_allowed"] is False
    sections, symbols = elf_sections(source)
    entry = symbols["rtl_mocha_net_init_alloc_probe"]
    text = sections[".text"]
    offset = text[4] + entry - text[3]
    # Any one-instruction damage to the exact shim must be rejected.
    for i in range(7):
        damaged = bytearray(source)
        damaged[offset + 4*i + 3] ^= 1
        try: make_plan(damaged, firmware, mapping)
        except ValueError: pass
        else: raise AssertionError(f"accepted damaged shim word {i}")
    for short in (source[:4], source[:51], source[:-1]):
        try: make_plan(short, firmware, mapping)
        except ValueError: pass
        else: raise AssertionError("accepted truncated ELF")
    damaged = bytearray(firmware); damaged[0] ^= 1
    try: make_plan(source, damaged, mapping)
    except ValueError: pass
    else: raise AssertionError("accepted wrong firmware")
    # The SP model covers either word-aligned entry convention; it does not
    # emulate arbitrary ARM instructions or external function implementations.
    import struct
    words = list(struct.unpack_from(">7I", source, offset))
    verify_shim(words, entry, 0x12300a74,
                symbols["rtl_mocha_net_alloc_probe_after_init"])
    print("Mocha candidate review: seven damaged instructions, truncated ELF and firmware mismatch rejected; activation disabled")


if __name__ == "__main__": main()
