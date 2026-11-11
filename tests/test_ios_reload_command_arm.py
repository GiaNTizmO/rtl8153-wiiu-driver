"""Offline execution of the owner's Thumb title-launch shell command ABI.
External prepare/configure/relaunch functions are stubbed, not executed.
This establishes argument layout only; it cannot establish a safe IOS reload.
"""
import hashlib
import json
import struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R11, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

ROOT = Path(__file__).resolve().parents[1]
FOLDER = ROOT / "evidence/firmware-5.5.6/analysis"
IMAGE = (FOLDER / "ios.elf").read_bytes()
assert hashlib.sha256(IMAGE).hexdigest() == "8ac1ae366b2925f66f19b7f50033103f0ecee45847c9198aa78343a8cd9c4e23"
MAPPING = json.loads((FOLDER / "map.json").read_text())
ENTRY = 0x0510c9a0
STACK, ARGS, STOP = 0x20000000, 0x21000000, 0x1000


def run(failure=0, argc=2):
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_BIG_ENDIAN)
    segment = next(s for s in MAPPING["segments"]
                   if s["type"] == 1 and s["vaddr"] <= ENTRY < s["vaddr"]+s["filesz"])
    start = segment["vaddr"] & ~4095
    size = (segment["vaddr"]+segment["filesz"]-start+4095) & ~4095
    uc.mem_map(start, size)
    uc.mem_write(segment["vaddr"], IMAGE[segment["offset"]:segment["offset"]+segment["filesz"]])
    uc.mem_map(STACK, 0x10000)
    uc.mem_map(ARGS, 4096)
    uc.mem_map(STOP, 4096)
    # The real command receives integer title halves at argv[2] and [3].
    uc.mem_write(ARGS, struct.pack(">5I", 0, 0, 0x00050010, 0x10040100, 0x15))
    incoming_sp = STACK + 0x10000 - 16
    uc.reg_write(UC_ARM_REG_R0, argc)
    uc.reg_write(UC_ARM_REG_R1, ARGS)
    uc.reg_write(UC_ARM_REG_SP, incoming_sp)
    uc.reg_write(UC_ARM_REG_LR, STOP | 1)
    saved = {r:0xaabb0100+i for i,r in enumerate((UC_ARM_REG_R4,UC_ARM_REG_R5,
             UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R11))}
    for r,v in saved.items(): uc.reg_write(r,v)
    state = dict(calls=[], returned=False, buffer=None)
    external = {0x05059140:"printf", 0x0502acb8:"prepare",
                0x0501e7a0:"configure", 0x0501f188:"relaunch"}
    for page in sorted({a & ~4095 for a in external}):
        uc.mem_map(page,4096)

    def instruction(uc, address, size, _):
        if address == STOP:
            state["returned"] = True
            uc.emu_stop()
            return
        if address not in external: return
        name = external[address]
        r = [uc.reg_read(x) for x in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
        result = 0
        if name == "prepare":
            assert r[:2] == [0x00050010,0x10040100] and r[3] == 0
            state["buffer"] = r[2]
            assert STACK <= r[2] < incoming_sp
            # Model the title data consumed by the command at offsets +8..15.
            uc.mem_write(r[2]+8, struct.pack(">4H",0x0123,0x4567,0x89ab,0xcdef))
            if failure == 1: result = -17
        elif name == "configure":
            assert r[:2] == [state["buffer"], 0 if argc==2 else 0x15]
            if failure == 2: result = -18
        elif name == "relaunch":
            assert r[:2] == [0x01234567,0x89abcdef]
            if failure == 3: result = -19
        state["calls"].append(name)
        uc.reg_write(UC_ARM_REG_R0,result & 0xffffffff)
        for reg in (UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3): uc.reg_write(reg,0xdeadbeef)
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))

    uc.hook_add(UC_HOOK_CODE,instruction)
    try:
        uc.emu_start(ENTRY|1,0,count=2000)
    except Exception:
        print("Emulation stopped at", hex(uc.reg_read(UC_ARM_REG_PC)),
              "LR", hex(uc.reg_read(UC_ARM_REG_LR)), state)
        raise
    assert state["returned"] and uc.reg_read(UC_ARM_REG_SP)==incoming_sp
    assert uc.reg_read(UC_ARM_REG_R0)==0 # Shell handler masks downstream errors.
    for r,v in saved.items(): assert uc.reg_read(r)==v
    operations = [n for n in state["calls"] if n!="printf"]
    assert operations == ["prepare","configure","relaunch"][:failure if failure in (1,2) else 3]


if __name__ == "__main__":
    for argc in (2,3):
        for failure in (0,1,2,3): run(failure,argc)
    print("Thumb command ABI: 8 cases; title halves, optional flags, error branches, saved registers/SP passed. External launch/reload functions stubbed; no console action.")
