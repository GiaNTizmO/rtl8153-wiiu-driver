"""Execute the compiled ARM big-endian allocation candidate in Unicorn.
Only external IOS functions are stubbed. No console/SDK changes.
"""
import argparse
import json
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
PAGE, PAGE_BYTES = 0x300000, 4096
STOP = 0x1000
SAVED = [UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
         UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11]


def signed(x): return x - 0x100000000 if x & 0x80000000 else x


def run_case(data, original_result, incoming_mod, allocation):
    sections, symbols = elf_sections(data)
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_BIG_ENDIAN)
    for name, s in sections.items():
        if s[2] & 2 and s[5]:
            if name not in (".text", ".rtl_alloc_status"): raise AssertionError("unexpected allocated section")
            start = s[3] & ~4095
            uc.mem_map(start, (s[3]+s[5]-start+4095) & ~4095)
            if s[1] != 8: uc.mem_write(s[3], data[s[4]:s[4]+s[5]])
    externals = {symbols[n]: n for n in ("rtl_net_original_init", "rtl_net_alloc",
                                          "rtl_net_free", "rtl_net_printf")}
    for address in sorted({a & ~4095 for a in externals}):
        uc.mem_map(address, 4096)
    uc.mem_map(STACK, STACK_BYTES)
    uc.mem_map(PAGE, PAGE_BYTES)
    uc.mem_write(PAGE, b"\x5a" * PAGE_BYTES)
    uc.mem_map(STOP, 4096)
    incoming_sp = STACK+STACK_BYTES-16+incoming_mod
    uc.reg_write(UC_ARM_REG_SP, incoming_sp)
    uc.reg_write(UC_ARM_REG_LR, STOP)
    expected = {}
    for i, reg in enumerate(SAVED):
        expected[reg] = 0xa0000100+i
        uc.reg_write(reg, expected[reg])
    state = dict(calls=[], live=False, returned=False, writes=0)

    def write(uc, access, address, size, value, user):
        if STACK <= address and address+size <= STACK+STACK_BYTES: return
        if 0x1288d040 <= address and address+size <= 0x1288d080: return
        assert state["live"] and PAGE <= address and address+size <= PAGE+PAGE_BYTES, (hex(address),size)
        assert allocation == "aligned", "unaligned allocation was written"
        assert size == 4 and address in (PAGE, PAGE+PAGE_BYTES-4)
        state["writes"] += 1

    def instruction(uc, address, size, user):
        if address == STOP:
            state["returned"] = True
            uc.emu_stop()
            return
        if address not in externals: return
        name = externals[address]
        state["calls"].append(name)
        sp = uc.reg_read(UC_ARM_REG_SP)
        r = [uc.reg_read(reg) for reg in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2)]
        result = 0
        if name == "rtl_net_original_init":
            assert len(state["calls"]) == 1 and sp % 8 == incoming_sp % 8
            result = original_result
        else:
            snapshot=struct.unpack(">16I",uc.mem_read(0x1288d040,64))
            if name in ("rtl_net_alloc","rtl_net_free"):
                assert snapshot[-1]==0, "published completion before returning from IOS"
            if name=="rtl_net_printf":
                assert snapshot[-1]==0xadabb3be
            assert sp % 8 == 0, "unaligned C-to-IOS call"
            if name == "rtl_net_alloc":
                assert original_result >= 0 and r == [0xcafe, 4096, 4096]
                result = {"null":0, "aligned":PAGE, "unaligned":PAGE+4}[allocation]
                state["live"] = bool(result)
            elif name == "rtl_net_free":
                assert state["live"] and r[:2] == [0xcafe, PAGE+(4 if allocation=="unaligned" else 0)]
                state["live"] = False
            elif name == "rtl_net_printf":
                text = bytes(uc.mem_read(r[0], 100)).split(b"\0")[0].decode("ascii")
                assert text.startswith("RTL_NET_ALLOC:") and not state["live"]
                if allocation == "null":
                    assert "allocation failed" in text and signed(r[1]) == original_result
                else:
                    assert "exec unverified" in text
                    assert r[1] == PAGE+(4 if allocation=="unaligned" else 0)
                    assert r[2] == (allocation == "aligned")
        # Caller-saved registers may be destroyed by every external call.
        uc.reg_write(UC_ARM_REG_R0, result & 0xffffffff)
        for reg in (UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R12):
            uc.reg_write(reg, 0xdeadbeef)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    uc.hook_add(UC_HOOK_CODE, instruction)
    uc.hook_add(UC_HOOK_MEM_WRITE, write)
    uc.emu_start(symbols["rtl_mocha_net_init_alloc_probe"], 0, count=3000)
    assert state["returned"] and not state["live"]
    status = struct.unpack(">16I", uc.mem_read(0x1288d040,64))
    assert status[:2] == (0x52544c41,1) and status[-1] == 0xadabb3be
    assert status[3] == original_result & 0xffffffff and status[8:15] == (0,)*7
    expected_stage = 2 if original_result < 0 else {"null":3,"aligned":4,"unaligned":5}[allocation]
    assert status[2] == expected_stage
    assert status[6] == (original_result >= 0)
    assert status[7] == (original_result >= 0 and allocation != "null")
    assert status[5] == (original_result >= 0 and allocation == "aligned")
    assert signed(uc.reg_read(UC_ARM_REG_R0)) == original_result
    assert uc.reg_read(UC_ARM_REG_SP) == incoming_sp
    for reg, value in expected.items(): assert uc.reg_read(reg) == value
    calls = state["calls"]
    if original_result < 0:
        assert calls == ["rtl_net_original_init"]
    else:
        expected_calls = ["rtl_net_original_init", "rtl_net_alloc"]
        if allocation != "null": expected_calls += ["rtl_net_free"]
        assert calls == expected_calls + ["rtl_net_printf"]
    page = bytes(uc.mem_read(PAGE, PAGE_BYTES))
    if original_result >= 0 and allocation == "aligned":
        assert page[:4] == struct.pack(">I",0x52544c50)
        assert page[-4:] == struct.pack(">I",~0x52544c50 & 0xffffffff)
        assert page[4:-4] == b"\x5a" * (PAGE_BYTES-8) and state["writes"]==2
    else:
        assert page == b"\x5a" * PAGE_BYTES and not state["writes"]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("elf",type=Path,nargs="?", default=root/"build/mocha-alloc-experiment/source/ios_net/ios_net.elf")
    a=p.parse_args(); data=a.elf.read_bytes(); cases=0
    for result in (-77,-1,0,9,2147483647):
        for incoming in (0,4):
            for allocation in ("null","aligned","unaligned"):
                run_case(data,result,incoming,allocation); cases+=1
    print(f"ARM emulation: {cases} cases; original result, r4-r11, SP, ABI, exact page writes/free and owned status/commit ordering passed; IOS functions stubbed")


if __name__=="__main__": main()

