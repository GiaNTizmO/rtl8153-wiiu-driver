"""Owner-firmware USB Ethernet completion ABI, actual ARM execution offline.

Execute ASIX completion and the real pending-counter helper; stub network
delivery, shutdown drain and logging. No claim of RTL attachment/USB hardware.
"""
import hashlib
import json
import struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
    UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10,
    UC_ARM_REG_R11, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

ROOT=Path(__file__).resolve().parents[1]
FOLDER=ROOT/'evidence/firmware-5.5.6/analysis'
IMAGE=(FOLDER/'ios.elf').read_bytes()
assert hashlib.sha256(IMAGE).hexdigest()=='8ac1ae366b2925f66f19b7f50033103f0ecee45847c9198aa78343a8cd9c4e23'
MAPPING=json.loads((FOLDER/'map.json').read_text())
ENTRY,COUNTER=0x123b94d8,0x123b6f28
CONTEXT,CONTROL,STATS,BATCH,BUFFER,STACK,STOP=(0x20000000,0x20001000,0x20002000,
    0x20003000,0x20004000,0x21000000,0x1000)


def run(kind,result,pending):
    uc=Uc(UC_ARCH_ARM,UC_MODE_ARM|UC_MODE_BIG_ENDIAN)
    s=next(s for s in MAPPING['segments'] if s['type']==1 and s['vaddr']==0x12300000)
    uc.mem_map(s['vaddr'],(s['filesz']+4095)&~4095)
    uc.mem_write(s['vaddr'],IMAGE[s['offset']:s['offset']+s['filesz']])
    uc.mem_map(CONTEXT,0x5000);uc.mem_map(STACK,0x10000);uc.mem_map(STOP,4096)
    def put(a,v):uc.mem_write(a,struct.pack('>I',v&0xffffffff))
    def get(a):return struct.unpack('>I',uc.mem_read(a,4))[0]
    put(CONTROL+4,STATS)
    offsets={1:0x1a8,2:0x1ac}
    for offset in (0x1a4,0x1a8,0x1ac):put(CONTROL+offset,7)
    put(CONTROL+offsets[kind],pending)
    put(BATCH+0x38,9);put(BATCH+0x3c,0xcccccccc);put(BATCH+0x444,BUFFER)
    sp=STACK+0x10000-32
    put(sp,BUFFER);put(sp+4,result)
    uc.reg_write(UC_ARM_REG_SP,sp);uc.reg_write(UC_ARM_REG_LR,STOP)
    for reg,value in ((UC_ARM_REG_R0,CONTEXT),(UC_ARM_REG_R1,CONTROL),
                      (UC_ARM_REG_R2,kind),(UC_ARM_REG_R3,BATCH)):
        uc.reg_write(reg,value)
    saved={r:0xa0000100+i for i,r in enumerate((UC_ARM_REG_R4,UC_ARM_REG_R5,
        UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11))}
    for reg,value in saved.items():uc.reg_write(reg,value)
    events=[];returned=False
    downstream={0x123b8034:'tx_delivery',0x123b8174:'rx_delivery',0x123b936c:'drain'}
    def write(uc,access,a,n,value,_):
        assert ((STACK<=a and a+n<=STACK+0x10000) or
                (CONTROL+offsets[kind]<=a and a+n<=CONTROL+offsets[kind]+4) or
                (BATCH+0x38<=a and a+n<=BATCH+0x40) or
                (a==CONTROL+0x530 and n==4 and kind==1 and result==0) or
                (STATS<=a and a+n<=STATS+0x100)),hex(a)
    def code(uc,a,n,_):
        nonlocal returned
        if a==STOP:returned=True;uc.emu_stop();return
        args=[uc.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
        if a==COUNTER:
            assert args==[CONTEXT,CONTROL,kind,0xffffffff]
            events.append('counter')
        if a not in downstream:return
        name=downstream[a];events.append(name)
        assert args[:2]==[CONTEXT,CONTROL]
        assert get(CONTROL+offsets[kind])==pending-1
        if name!='drain':
            assert args[2]==BATCH and get(BATCH+0x3c)==result&0xffffffff
            if kind==1:assert get(BATCH+0x38)==0
        uc.reg_write(UC_ARM_REG_R0,0)
        for r in (UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3):uc.reg_write(r,0xdeadbeef)
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    uc.hook_add(UC_HOOK_CODE,code);uc.hook_add(UC_HOOK_MEM_WRITE,write)
    uc.emu_start(ENTRY,0,count=3000)
    assert returned and uc.reg_read(UC_ARM_REG_R0)==0 and uc.reg_read(UC_ARM_REG_SP)==sp
    for r,value in saved.items():assert uc.reg_read(r)==value
    assert events==['counter','rx_delivery' if kind==1 else 'tx_delivery','drain']
    for k,offset in offsets.items():assert get(CONTROL+offset)==(pending-1 if k==kind else 7)
    assert get(CONTROL+0x1a4)==7
    if kind==1:
        assert result<=0 # Positive RX invokes ASIX frame parsing, excluded here.
        assert get(STATS+0x80)==(result<0)
    else:
        assert get(STATS+0x64)==(result<0)
        assert get(BATCH+0x38)==9 # TX batch count is preserved.


if __name__=='__main__':
    cases=0
    for pending in (1,2,8):
        for kind,results in ((1,(-64,-1,0)),(2,(-64,-1,0,48,16384))):
            for result in results:run(kind,result,pending);cases+=1
    print(f'IOS completion ARM: {cases} cases; real pending decrement, signed result, delivery/drain order and saved registers/SP passed. Network callbacks stubbed; positive ASIX RX parsing not exercised.')
