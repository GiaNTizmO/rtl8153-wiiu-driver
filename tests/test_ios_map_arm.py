"""Execute the owner's real small-page mapping code offline.
Page-table RAM is synthetic. Cache/TLB maintenance and memcpy/memset are stubs;
this proves descriptor construction only, never real permissions or coherency.
"""
import hashlib
import json
import struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,
    UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
    UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,
    UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC)

ROOT=Path(__file__).resolve().parents[1]
FW=ROOT/"evidence/firmware-5.5.6/analysis"
image=(FW/"ios.elf").read_bytes()
assert hashlib.sha256(image).hexdigest()=="8ac1ae366b2925f66f19b7f50033103f0ecee45847c9198aa78343a8cd9c4e23"
kernel=(FW/"kernel-text.bin").read_bytes()
mapping=json.loads((FW/"map.json").read_text())
segment=next(s for s in mapping["segments"] if s["type"]==1 and s["vaddr"]==0x08120000)
assert kernel==image[segment["offset"]:segment["offset"]+segment["filesz"]]
STACK,DESC,L1,L2,STOP=0x20000000,0x21000000,0x22000000,0x08154000,0x1000
VA,PA=0x12201000,0x18001000 # Synthetic, not reserved console addresses.


def run(pages=1,domain=7,access=3,cached=1,old_domain=7,invalid=None,new_table=False):
    uc=Uc(UC_ARCH_ARM,UC_MODE_ARM|UC_MODE_BIG_ENDIAN)
    uc.mem_map(0x08120000,0x20000)
    uc.mem_write(0x08120000,kernel)
    uc.mem_map(0x08140000,0x30000)
    uc.mem_map(STACK,0x10000)
    uc.mem_map(DESC,4096)
    uc.mem_map(L1,0x4000)
    uc.mem_map(STOP,4096)
    def word(a,v): uc.mem_write(a,struct.pack(">I",v))
    def get(a): return struct.unpack(">I",uc.mem_read(a,4))[0]
    word(0x08143014,L1)
    word(0x08143058,L2)
    l1_slot=L1+(VA>>20)*4
    neighbor=0x18000ff2
    if not new_table:
        word(l1_slot,L2|(old_domain<<5)|0x11)
        word(L2,neighbor)
    size=pages*4096
    pa,va=PA,VA
    if invalid=="physical": pa+=4
    if invalid=="virtual": va+=4
    if invalid=="size": size=4095
    descriptor=struct.pack(">6I",pa,va,size,domain,access,cached)
    uc.mem_write(DESC,descriptor)
    incoming_sp=STACK+0x10000-16
    uc.reg_write(UC_ARM_REG_SP,incoming_sp)
    uc.reg_write(UC_ARM_REG_LR,STOP)
    uc.reg_write(UC_ARM_REG_R0,DESC)
    saved={r:0xaabb0010+i for i,r in enumerate((UC_ARM_REG_R4,UC_ARM_REG_R5,
        UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,
        UC_ARM_REG_R10,UC_ARM_REG_R11))}
    for r,v in saved.items(): uc.reg_write(r,v)
    state=dict(returned=False,flushes=[],tlb=0,errors=0)
    externals={0x08131d04:"copy",0x08131da0:"fill",0x08120160:"flush",
               0x0812ea18:"tlb",0x08129a24:"error"}
    def code(uc,address,size,_):
        if address==STOP:
            state["returned"]=True;uc.emu_stop();return
        if address not in externals:return
        name=externals[address]
        r=[uc.reg_read(x) for x in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2)]
        if name=="copy":
            assert r[0]>=STACK and r[0]+r[2]<=STACK+0x10000 and r[1]==DESC and r[2]==24
            uc.mem_write(r[0],bytes(uc.mem_read(r[1],r[2])))
        elif name=="fill":
            assert r==[L2,0,1024]
            uc.mem_write(r[0],bytes([r[1]])*r[2])
        elif name=="flush":state["flushes"].append(r[:2])
        elif name=="tlb":state["tlb"]+=1
        else:state["errors"]+=1
        uc.reg_write(UC_ARM_REG_R0,0)
        for reg in (UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3):uc.reg_write(reg,0xdeadbeef)
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    uc.hook_add(UC_HOOK_CODE,code)
    uc.emu_start(0x08124f88,0,count=20000)
    assert state["returned"] and uc.reg_read(UC_ARM_REG_SP)==incoming_sp
    for r,v in saved.items():assert uc.reg_read(r)==v
    assert bytes(uc.mem_read(DESC,24))==descriptor # descriptor copied to stack
    assert state["tlb"]==1
    rc=uc.reg_read(UC_ARM_REG_R0)
    if invalid:
        assert rc==0xfffffffc and state["errors"]==1
        assert not state["flushes"]
        return
    assert rc==0 and not state["errors"]
    assert get(l1_slot)==L2|(domain<<5)|0x11
    # Neighbor entry survives but its domain is taken from the enclosing L1.
    if not new_table:assert get(L2)==neighbor
    for i in range(pages):
        pte=L2+(((VA+i*4096)>>12)&255)*4
        expected=PA+i*4096 | (access<<4) | (access<<6) | (access<<8) | (access<<10) | 2
        if cached:expected|=12
        assert get(pte)==expected,(hex(get(pte)),hex(expected))
        assert [pte,4] in state["flushes"]
    assert [l1_slot,4] in state["flushes"]
    if new_table:assert get(0x08143058)==L2+1024 and [L2,1024] in state["flushes"]


if __name__=="__main__":
    cases=0
    for access in (1,2,3):
        for cached in (0,1,0xffffffff):
            for pages in (1,3):run(pages=pages,access=access,cached=cached);cases+=1
    for invalid in ("physical","virtual","size"):run(invalid=invalid);cases+=1
    run(old_domain=1);cases+=1
    run(new_table=True);cases+=1
    print(f"ARM map: {cases} cases; actual PTE/AP/domain/cache encoding, fresh L2 allocation, alignment refusal, stack/registers and descriptor copy passed. Cache/TLB/MMU effects stubbed.")
