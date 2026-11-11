"""Execute the linked ARM big-endian heap-entry candidate in Unicorn.

The actual NET init shim, C diagnostic and the 28-byte blob copied into the
modelled heap page all execute as real ARM instructions. Stubbed: the original
NET init, IOS_AllocAligned, IOS_Free, IOS_FlushDCache and printf. Unicorn has
no MMU permissions, caches or prefetch aborts: a pass proves control flow,
ABI, ordering and result checking, never hardware execute permission or cache
coherency. No console/SDK changes.
"""
import argparse
import struct
import sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
    UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)
root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "scripts"))
from check_mocha_alloc_candidate import elf_sections

STACK, STACK_BYTES = 0x200000, 0x10000
HEAP_MAP, HEAP_MAP_BYTES = 0x124c0000, 0x50000   # model window around local heap
STATUS = 0x1288d040
STOP = 0x1000
MAGIC, BLOB_MARKER, SALT = 0x52544c45, 0x52544c58, 0x5a17c0de
SAVED = [UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
         UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11]
BLOB_WORDS = 7
# scenario -> (allocator result, expected terminal stage for nonnegative init)
SCENARIOS = {
    "null": (0, 3), "low_page": (0x124c7000, 8), "last_page": (0x12505000, 8),
    "unaligned": (0x124c7004, 4), "below_heap": (0x124c5000, 4),
    "past_heap": (0x12506000, 4), "copy_corrupt": (0x124c7000, 5),
    "flush_fail": (0x124c7000, 6), "stale_fetch": (0x124c7000, 9),
}
RUNS = ("low_page", "last_page", "stale_fetch")


def u32(x): return x & 0xffffffff
def signed(x): return x - 0x100000000 if x & 0x80000000 else x


def run_case(data, original_result, incoming_mod, scenario):
    sections, symbols = elf_sections(data)
    page_result, stage_ok = SCENARIOS[scenario]
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_BIG_ENDIAN)
    for name, s in sections.items():
        if s[2] & 2 and s[5]:
            assert name in (".text", ".rtl_alloc_status"), name
            start = s[3] & ~4095
            uc.mem_map(start, (s[3] + s[5] - start + 4095) & ~4095)
            if s[1] != 8: uc.mem_write(s[3], data[s[4]:s[4] + s[5]])
    names = ("rtl_net_original_init", "rtl_net_alloc", "rtl_net_free",
             "rtl_net_flush_dcache", "rtl_net_printf")
    externals = {symbols[n]: n for n in names}
    for address in sorted({a & ~4095 for a in externals}):
        uc.mem_map(address, 4096)
    uc.mem_map(STACK, STACK_BYTES)
    uc.mem_map(HEAP_MAP, HEAP_MAP_BYTES)
    uc.mem_write(HEAP_MAP, b"\x5a" * HEAP_MAP_BYTES)
    uc.mem_map(STOP, 4096)
    blob = bytes(uc.mem_read(symbols["rtl_heap_entry_blob"], 4 * BLOB_WORDS))
    incoming_sp = STACK + STACK_BYTES - 16 + incoming_mod
    uc.reg_write(UC_ARM_REG_SP, incoming_sp)
    uc.reg_write(UC_ARM_REG_LR, STOP)
    expected_regs = {}
    for i, reg in enumerate(SAVED):
        expected_regs[reg] = 0xa0000100 + i
        uc.reg_write(reg, expected_regs[reg])
    page = page_result
    st = dict(calls=[], live=False, returned=False, page_writes=[], executed=[],
              flushed=False, corrupt_pending=False)

    def status_words(): return struct.unpack(">16I", uc.mem_read(STATUS, 64))

    def write(uc, access, address, size, value, user):
        if STACK <= address and address + size <= STACK + STACK_BYTES: return
        if STATUS <= address and address + size <= STATUS + 64:
            # Heap code may write only observed_pc/observed_marker (+0x20/+0x24).
            if st["executed"] and not st["returned_from_blob"]:
                assert address in (STATUS + 0x20, STATUS + 0x24) and size == 4
            return
        assert st["live"] and page <= address and address + size <= page + 4 * BLOB_WORDS, (hex(address), size)
        assert size == 4 and not st["flushed"], "page written after flush"
        st["page_writes"].append(address)
        if scenario == "copy_corrupt" and address == page + 8:
            st["corrupt_pending"] = True

    def instruction(uc, address, size, user):
        if st["corrupt_pending"]:
            st["corrupt_pending"] = False
            uc.mem_write(page + 8, b"\xde\xad\xbe\xef")
        if address == STOP:
            st["returned"] = True; uc.emu_stop(); return
        if page and page <= address < page + 4096 and HEAP_MAP <= page:
            assert st["flushed"] and st["live"], "heap entered before FlushDCache"
            assert address < page + 4 * (BLOB_WORDS - 1), "fell through blob"
            st["executed"].append(address - page)
            return
        if st["executed"] and not st["returned_from_blob"]:
            st["returned_from_blob"] = True
        if address not in externals: return
        name = externals[address]
        st["calls"].append(name)
        sp = uc.reg_read(UC_ARM_REG_SP)
        r = [uc.reg_read(reg) for reg in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2)]
        result = 0
        if name == "rtl_net_original_init":
            assert len(st["calls"]) == 1 and sp % 8 == incoming_sp % 8
            result = original_result
        else:
            assert sp % 8 == 0, "unaligned C-to-IOS call"
            snap = status_words()
            if name != "rtl_net_printf":
                assert snap[15] == 0, "commit published before IOS call returned"
            if name == "rtl_net_alloc":
                assert original_result >= 0 and r == [0xcafe, 4096, 4096]
                result = page_result
                st["live"] = bool(result)
            elif name == "rtl_net_flush_dcache":
                assert st["live"] and r[:2] == [page, 4096]
                assert sorted(st["page_writes"]) == [page + 4 * i for i in range(BLOB_WORDS)]
                assert bytes(uc.mem_read(page, 4 * BLOB_WORDS)) == blob
                st["flushed"] = True
                if scenario == "flush_fail": result = -1
                if scenario == "stale_fetch":
                    # Model a stale instruction fetch: eor r0,r1,r2 -> mov r0,#0.
                    uc.mem_write(page + 16, struct.pack(">I", 0xe3a00000))
            elif name == "rtl_net_free":
                assert st["live"] and r[:2] == [0xcafe, page]
                st["live"] = False
            elif name == "rtl_net_printf":
                assert snap[15] == u32(~MAGIC) and not st["live"]
                text = bytes(uc.mem_read(r[0], 100)).split(b"\0")[0].decode("ascii")
                assert text.startswith("RTL_NET_ENTRY:")
                if scenario == "null":
                    assert "allocation failed" in text and signed(r[1]) == original_result
                else:
                    assert r[1] == page and r[2] == stage_ok
        uc.reg_write(UC_ARM_REG_R0, u32(result))
        for reg in (UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R12):
            uc.reg_write(reg, 0xdeadbeef)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    st["returned_from_blob"] = False
    uc.hook_add(UC_HOOK_CODE, instruction)
    uc.hook_add(UC_HOOK_MEM_WRITE, write)
    uc.emu_start(symbols["rtl_mocha_net_init_entry_probe"], 0, count=5000)
    assert st["returned"] and not st["live"]
    s = status_words()
    ran = original_result >= 0 and scenario in RUNS
    stage = 2 if original_result < 0 else stage_ok
    copied = original_result >= 0 and scenario in RUNS + ("flush_fail",)
    nonce = u32(page ^ SALT) if ran else 0
    expected = [MAGIC, 2, stage, u32(original_result),
                page if original_result >= 0 else 0,
                int(copied), u32(-1) if (original_result >= 0 and scenario == "flush_fail") else 0,
                (0 if scenario == "stale_fetch" else nonce ^ BLOB_MARKER) if ran else 0,
                page + 8 if ran else 0, BLOB_MARKER if ran else 0, nonce,
                int(original_result >= 0), int(copied), int(ran),
                int(original_result >= 0 and scenario != "null"), u32(~MAGIC)]
    assert list(s) == expected, (scenario, original_result, [hex(x) for x in s], [hex(x) for x in expected])
    # Only the six blob instructions run, in order, exactly once.
    assert st["executed"] == ([0, 4, 8, 12, 16, 20] if ran else []), st["executed"]
    assert signed(uc.reg_read(UC_ARM_REG_R0)) == original_result
    assert uc.reg_read(UC_ARM_REG_SP) == incoming_sp
    for reg, value in expected_regs.items(): assert uc.reg_read(reg) == value
    calls = st["calls"]
    if original_result < 0:
        assert calls == ["rtl_net_original_init"]
    elif scenario == "null":
        assert calls == ["rtl_net_original_init", "rtl_net_alloc", "rtl_net_printf"]
    else:
        middle = ["rtl_net_flush_dcache"] if copied else []
        assert calls == ["rtl_net_original_init", "rtl_net_alloc"] + middle + ["rtl_net_free", "rtl_net_printf"]
    if original_result < 0 or scenario in ("null", "unaligned", "below_heap", "past_heap"):
        assert not st["page_writes"]
    return stage


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("elf", type=Path, nargs="?",
                   default=root / "build/mocha-entry-experiment/source/ios_net/ios_net.elf")
    a = p.parse_args()
    data = a.elf.read_bytes()
    cases, stages = 0, {}
    for result in (-77, -1, 0, 9, 2147483647):
        for incoming in (0, 4):
            for scenario in SCENARIOS:
                stage = run_case(data, result, incoming, scenario)
                stages[stage] = stages.get(stage, 0) + 1
                cases += 1
    print(f"ARM entry emulation: {cases} cases, terminal stages {dict(sorted(stages.items()))}; "
          "real shim/C/heap blob executed; original result, r4-r11, SP, call order, "
          "copy-before-flush, flush-before-entry, exact blob writes, entry-only-in-range, "
          "stale-fetch detection and commit ordering passed. "
          "Stubbed: original init, alloc, free, FlushDCache, printf; no MMU/cache/abort model.")


if __name__ == "__main__": main()
