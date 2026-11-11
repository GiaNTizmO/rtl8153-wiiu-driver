"""Real owner RX allocator/worker + compiled RTL firmware-RX/token adapters.

Runs real pending helper, RX delivery and native input dispatch. Actual heaps,
UHS, cache/USB DMA, pacing/timers and final network input/free remain stubs.
"""
import sys
import struct
from pathlib import Path
from test_ios_submit_close_arm import Machine, CTX, CTRL, IFACE
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from relocate_ios_image import ArmRelocatable

BASE=0x22000000
TOKENS,BINDING,WRAPPED,OPS,MSG,RX,HOST=(CTX+0x28000,CTX+0x29000,
    CTX+0x2a000,CTX+0x2b000,CTX+0x2c000,CTX+0x2d000,CTX+0x2e000)
BATCH,DMA=0x24000000,0x25000000
ALLOC,FREE,DISCARD,DRAIN,SUBMIT,CONTROL=0x1200,0x1300,0x1400,0x1500,0x1600,0x1700
PENDING,DONE,INPUT=0x1800,0x1900,0x1a00
READ=0x1b00
RX_BIND=HOST+0x100


def run(result,closing=False,fail_submit=False,refill=False,malformed=False):
    m=Machine()
    image,manifest=ArmRelocatable((Path(__file__).resolve().parents[1]/
        'build/native/rtl8153_ios.rel.elf').read_bytes()).build(BASE,{})
    m.uc.mem_map(BASE,(len(image)+4095)&~4095)
    m.uc.mem_write(BASE,image)
    m.uc.mem_map(BATCH,0x10000);m.uc.mem_map(DMA,0x8000)
    symbols=manifest['exports']
    m.allowed=[(CTX,CTX+0x30000),(BATCH,BATCH+0x10000),(DMA,DMA+0x8000)]+[
        (BASE+s['offset'],BASE+s['offset']+s['size'])
        for s in manifest['sections'] if s['flags']&1]
    for offset,value in ((0,1),(4,0x5678),(8,0x1234),(16,71),(24,CONTROL),(28,SUBMIT)):
        m.put(BINDING+offset,value)
    m.uc.mem_write(BINDING+20,bytes([0x81,2]))
    m.put(CTX,17);m.put(CTX+0x82ac,IFACE);m.put(IFACE+0x30,CTRL)
    m.put(IFACE+0xd0,INPUT);m.put(IFACE+0x14,0)
    m.put(CTRL,OPS);m.put(CTRL+0x1a8,0);m.put(CTRL+0x1b0,1)
    m.put(OPS+0x14,0x4000);m.put(OPS+0x18,0)
    m.put(OPS+0x30,symbols['rtl_ios_firmware_receive_async'])
    m.put(OPS+0x34,symbols['rtl_ios_tokens_callback'])
    m.uc.mem_write(DMA,bytes([18])+bytes(23)+bytes([0x42])*14+bytes(10))
    if malformed:m.uc.mem_write(DMA,bytes(48))
    wire=[];events=[];allocations=[0,0];read_mode=[None]

    def stub(address,args,sp):
        if address==READ:
            assert args[0]==0x9876 and args[3]==64
            if read_mode[0]=='error':return -1
            raw=bytearray(m.uc.mem_read(args[1],64))
            if read_mode[0]=='mismatch':raw[0]^=1
            m.uc.mem_write(args[2],bytes(raw));return 0
        if address==0x123e438c:
            assert args[:2]==[0xcaff,0x4467];events.append('batch_alloc')
            value=BATCH+allocations[0]*0x5000;allocations[0]+=1;return value
        if address==0x123e2ac0:
            m.uc.mem_write(args[0],bytes([args[1]&255])*args[2]);return args[0]
        if address==0x123e4394:
            assert args[:3]==[0xcaff,0x4000,64]
            value=DMA+allocations[1]*0x4000;allocations[1]+=1
            events.append('bounce_alloc');return value
        if address==SUBMIT:
            assert args==[1,0x5678,1,2]
            assert [m.get(sp),m.get(sp+4),m.get(sp+12)]==[DMA+(allocations[1]-1)*0x4000,0x4000,71]
            meta=m.get(sp+8);wire[:]=[m.get(meta+i*4) for i in range(5)]
            assert wire[:3]==[1,0x1234,BATCH+(allocations[0]-1)*0x5000] and wire[3]!=DMA
            events.append('submit');return -64 if fail_submit else 0
        if address==0x123b80ac:
            assert args[:2]==[CTX,CTRL];events.append('pacing')
            return None if refill else 0
        if address==INPUT:
            assert args[:3]==[17,0x1234,BATCH]
            assert m.get(BATCH+0x38)==(1 if result else 0) and m.get(BATCH+0x3c)==0
            if result:
                assert m.get(BATCH+0x40)==BATCH+0x460+24
                assert m.get(BATCH+0x44)==14
                assert bytes(m.uc.mem_read(BATCH+0x460+24,14))==bytes([0x42])*14
            events.append('native_input');return 0
        if address==0x123e439c:
            assert args[0]==0xcaff
            if DMA<=args[1]<DMA+0x8000:events.append('bounce_free')
            else:assert args[1]==BATCH;events.append('batch_free')
            return 0
        if address==0x123ba77c:
            assert args[:2]==[CTX+0x74,CTX+0x27000];events.append('request_free');return 0
        if address==0x123ccb50:return 0
        if address==0x123b936c:
            assert args[:2]==[CTX,CTRL] and m.get(CTRL+0x1a8)==(1 if refill else 0)
            events.append('drain');return 0
        return None

    def call(name,args):
        for i,value in enumerate(args[4:]):m.put(m.sp+i*4,value)
        return m.run(symbols[name],args[:4],stub)

    assert call('rtl_ios_tokens_init',[TOKENS,BINDING,CTX,CTRL,
        symbols['rtl_ios_firmware_rx_complete'],RX,WRAPPED])==0
    initial=bytes(m.uc.mem_read(RX_BIND,32))+bytes(m.uc.mem_read(HOST,28))
    for mode in ('error','mismatch'):
        read_mode[0]=mode
        assert call('rtl_ios_firmware_rx_bind',[RX_BIND,CTX,CTRL,READ,0x9876,HOST])==0xffffffff
        assert bytes(m.uc.mem_read(RX_BIND,32))+bytes(m.uc.mem_read(HOST,28))==initial
    read_mode[0]=None
    assert call('rtl_ios_firmware_rx_bind',[RX_BIND,CTX,CTRL,READ,0x9876,HOST])==0
    assert call('rtl_ios_firmware_rx_init',[RX,WRAPPED,HOST])==0
    assert call('rtl_ios_tokens_set_after',[TOKENS,symbols['rtl_ios_firmware_rx_finish'],RX])==0
    assert call('rtl_ios_tokens_attach',[TOKENS])==0
    assert call('rtl_ios_firmware_rx_attach',[RX,CTX,CTRL])==0
    # A callback cannot borrow geometry from a different configured ops table.
    m.put(OPS+0x14,0x8000)
    assert call('rtl_ios_firmware_receive_async',[CTX,CTRL,BATCH])==0xffffffff
    m.put(OPS+0x14,0x4000);m.put(OPS+0x18,4)
    assert call('rtl_ios_firmware_receive_async',[CTX,CTRL,BATCH])==0xffffffff
    assert not events
    m.put(OPS+0x18,0)
    assert m.run(0x123b75ac,[CTX,CTRL],stub)==(-64&0xffffffff if fail_submit else 0)
    inline=m.get(BATCH+0x444)
    assert inline==BATCH+0x460 and inline%64==32
    if fail_submit:
        assert events==['batch_alloc','bounce_alloc','submit','bounce_free','batch_free']
        assert m.get(CTRL+0x1a8)==0
        return
    assert events==['batch_alloc','bounce_alloc','submit'] and m.get(CTRL+0x1a8)==1
    if closing:
        assert call('rtl_ios_firmware_rx_close',[RX])==0xfffffffe
    m.put(MSG+4,result)
    for i,value in enumerate(wire):m.put(MSG+0x24+i*4,value)
    m.put(MSG+0x38,CTX+0x27000);m.put(MSG+0x3c,0x5678)
    events.clear()
    assert m.run(0x123b83b0,[CTX,MSG],stub)==0
    assert m.get(CTRL+0x1a8)==(1 if refill else 0)
    expected=['bounce_free']+(['batch_free'] if closing else
        ['batch_free'] if result<0 or result>0x4000 or malformed else ['pacing','native_input'])+['drain','request_free']
    if refill:expected=['bounce_free','pacing','batch_alloc','bounce_alloc','submit','native_input','drain','request_free']
    assert events==expected,events
    if not closing and result>=0 and result<=0x4000 and not malformed:
        assert m.get(IFACE+0x14)==1 # input retains batch beyond USB retirement.
    events.clear()
    assert m.run(0x123b83b0,[CTX,MSG],stub)==0
    assert events==['request_free'] and m.get(CTRL+0x1a8)==(1 if refill else 0)
    assert call('rtl_ios_tokens_drained',[TOKENS])==(0 if refill else 1)
    assert call('rtl_ios_firmware_rx_drained',[RX])==(0 if refill else 1)


if __name__=='__main__':
    run(48);run(-64);run(16385);run(-64,closing=True);run(48,fail_submit=True);run(48,refill=True)
    run(0);run(48,malformed=True)
    print('Owner allocator -> RTL receive_async -> UHS token -> owner worker -> RTL RX binding -> native input: 8 ARM cases; code fingerprint/read failures, real pending +/-1, inline 32-byte buffer, bounce publication/free, actual pacing/refill, empty/malformed/error release and duplicate no-op passed. Heap/UHS/cache/USB/input/final-drain helpers stubbed.')
