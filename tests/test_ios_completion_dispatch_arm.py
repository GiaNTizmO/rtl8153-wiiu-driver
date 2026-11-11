"""Execute owner IOS-NET dispatch with the real interface resolver.
Verify the metadata boundary, optionally chaining real completion/counter code.
Queue locks, request free and downstream network delivery remain stubs.
"""
import struct
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,
    UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,
    UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC)
from test_ios_completion_arm import IMAGE,MAPPING,ENTRY,COUNTER

CTX,IFACE,CTRL,OPS,MSG,BATCH,BUFFER,URB=range(0x20000000,0x20008000,0x1000)
STACK,STOP,CALLBACK=0x21000000,0x1000,0x1100
STATS=CTX+0x8000


def run(kind,result,actual_driver=False,opaque_buffer=False):
    uc=Uc(UC_ARCH_ARM,UC_MODE_ARM|UC_MODE_BIG_ENDIAN)
    s=next(s for s in MAPPING['segments'] if s['type']==1 and s['vaddr']==0x12300000)
    uc.mem_map(s['vaddr'],(s['filesz']+4095)&~4095)
    uc.mem_write(s['vaddr'],IMAGE[s['offset']:s['offset']+s['filesz']])
    uc.mem_map(CTX,0x9000);uc.mem_map(STACK,0x10000);uc.mem_map(STOP,4096)
    def put(a,v):uc.mem_write(a,struct.pack('>I',v&0xffffffff))
    def get(a):return struct.unpack('>I',uc.mem_read(a,4))[0]
    ident,handle,cookie=0x1234,0x5678,0xfedcba98
    put(IFACE,ident);put(IFACE+0x30,CTRL);put(CTRL,OPS);put(CTRL+0x18,handle)
    put(OPS+0x34,ENTRY if actual_driver else CALLBACK)
    put(CTX+0x82ac,IFACE) # Real resolver's first interface slot.
    put(CTRL+4,STATS)
    pending_offset={1:0x1a8,2:0x1ac}[kind]
    for offset in (0x1a4,0x1a8,0x1ac):put(CTRL+offset,7)
    put(CTRL+pending_offset,1)
    put(BATCH+0x38,9);put(BATCH+0x3c,0xcccccccc);put(BATCH+0x444,BUFFER)
    assert not actual_driver or kind==2 or result<=0 # No ASIX RX parsing.
    # Resolver uses the separate USB handle at +0x3c. The five copied
    # metadata words at +0x24 are kind, interface_id, batch, buffer, cookie.
    put(MSG+4,result);put(MSG+0x24,kind);put(MSG+0x28,ident)
    callback_buffer=0xabc123 if opaque_buffer else BUFFER
    assert not (opaque_buffer and actual_driver) # ASIX expects an actual buffer.
    put(MSG+0x2c,BATCH);put(MSG+0x30,callback_buffer);put(MSG+0x34,cookie)
    put(MSG+0x38,URB);put(MSG+0x3c,handle)
    before=bytes(uc.mem_read(MSG,0x40))
    sp=STACK+0x10000-32
    uc.reg_write(UC_ARM_REG_SP,sp);uc.reg_write(UC_ARM_REG_LR,STOP)
    uc.reg_write(UC_ARM_REG_R0,CTX);uc.reg_write(UC_ARM_REG_R1,MSG)
    saved={r:0xbb000100+i for i,r in enumerate((UC_ARM_REG_R4,UC_ARM_REG_R5,
        UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11))}
    for r,v in saved.items():uc.reg_write(r,v)
    events=[];returned=False
    downstream={0x123b8034:'tx_delivery',0x123b8174:'rx_delivery',0x123b936c:'drain'}
    def write(uc,access,a,n,v,_):
        assert ((STACK<=a and a+n<=STACK+0x10000) or (actual_driver and (
            (a==CTRL+pending_offset and n==4) or
            (BATCH+0x38<=a and a+n<=BATCH+0x40) or
            (a==CTRL+0x530 and n==4 and kind==1 and result==0) or
            (STATS<=a and a+n<=STATS+0x100)))),hex(a)
    def code(uc,a,n,_):
        nonlocal returned
        if a==STOP:returned=True;uc.emu_stop();return
        if a not in (0x123b3ecc,0x123b4384,0x123b42b4,0x123ba77c,CALLBACK,ENTRY,COUNTER) and a not in downstream:return
        args=[uc.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
        value=0
        if a==0x123b3ecc:
            assert args[:2]==[CTX,handle];events.append('resolve');return
        elif a==COUNTER:
            assert actual_driver and args==[CTX,CTRL,kind,0xffffffff]
            events.append('counter');return
        elif a in downstream:
            assert actual_driver and args[:2]==[CTX,CTRL]
            assert get(CTRL+pending_offset)==0
            if downstream[a]!='drain':assert args[2]==BATCH
            events.append(downstream[a])
        elif a==0x123b4384:
            assert kind==2 and args==[CTX,ident,2,0];events.append('acquire')
        elif a in (CALLBACK,ENTRY):
            assert args==[CTX,CTRL,kind,BATCH]
            call_sp=uc.reg_read(UC_ARM_REG_SP)
            assert [get(call_sp),get(call_sp+4)]==[callback_buffer,result&0xffffffff]
            # Cookie survives in the message but the ops ABI forwards
            # neither its value nor the metadata/message pointer.
            assert get(MSG+0x34)==cookie
            events.append('driver')
            if actual_driver:return
        elif a==0x123b42b4:
            assert kind==2 and args[:3]==[CTX,IFACE,2];events.append('release')
        else:
            assert args[:2]==[CTX+0x74,URB];events.append('free_request')
        uc.reg_write(UC_ARM_REG_R0,value)
        for r in (UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3):uc.reg_write(r,0xdeadbeef)
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    uc.hook_add(UC_HOOK_MEM_WRITE,write);uc.hook_add(UC_HOOK_CODE,code)
    uc.emu_start(0x123b83b0,0,count=3000)
    assert returned and uc.reg_read(UC_ARM_REG_R0)==0 and uc.reg_read(UC_ARM_REG_SP)==sp
    for r,v in saved.items():assert uc.reg_read(r)==v
    assert bytes(uc.mem_read(MSG,0x40))==before
    expected=['resolve']+(['acquire'] if kind==2 else [])+['driver']
    if actual_driver:
        expected+=['counter','rx_delivery' if kind==1 else 'tx_delivery','drain']
        assert get(CTRL+pending_offset)==0
        assert get(BATCH+0x3c)==result&0xffffffff
    else:assert get(CTRL+pending_offset)==1
    for offset in (0x1a4,0x1a8,0x1ac):
        if offset!=pending_offset:assert get(CTRL+offset)==7
    assert events==expected+(['release'] if kind==2 else [])+['free_request']


if __name__=='__main__':
    cases=0
    for kind in (1,2):
        for result in (-64,-1,0,48,16384,32768):run(kind,result);cases+=1
    integrated=0
    for kind,results in ((1,(-64,-1,0)),(2,(-64,-1,0,48,16384,32768))):
        for result in results:run(kind,result,True);integrated+=1
    opaque=0
    for kind in (1,2):
        for result in (-64,0,48):run(kind,result,opaque_buffer=True);opaque+=1
    print(f'IOS worker dispatch ARM: {cases} metadata, {integrated} integrated and {opaque} opaque-token cases; real resolver, callback arguments, cookie exclusion, exactly one pending retirement and cleanup order passed. Lock/free/network helpers stubbed; positive ASIX RX excluded.')
