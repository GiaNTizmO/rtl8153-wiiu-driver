"""Emulate the linked Mocha NET boot patcher against a private owner ELF copy.
No console access. Kernel memcpy/reverse_memcpy are modeled as bounded memmove.
NET-only and full kernel_launch_ios paths execute actual compiled patchers;
full reload stubs CP15, interrupt calls and the final bootrom handoff.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/"scripts"))
from check_mocha_alloc_candidate import elf_sections, branch
IMAGE, CAPACITY = 0x30000000, 0x2000000
STACK, STACK_SIZE = 0x20000000, 0x10000
STOP=0x1000


def phdrs(data):
    h=struct.unpack_from(">HHIIIIIHHHHHH",data,16)
    assert h[8]==32
    return [list(struct.unpack_from(">8I",data,h[4]+i*32)) for i in range(h[9])]


def at_va(data, address, length):
    found=[]
    for s in phdrs(data):
        if s[0]==1 and s[2]<=address and address+length<=s[2]+s[4]:
            found.append(s[1]+address-s[2])
    assert len(found)==1,(hex(address),len(found))
    return found[0]


def emulate(project, original, negative=False, full_reload=False, reject_header=False):
    kernel=(project/"source/ios_kernel/ios_kernel.elf").read_bytes()
    net=(project/"source/ios_net/ios_net.elf").read_bytes()
    sections,symbols=elf_sections(kernel)
    ns,net_symbols=elf_sections(net)
    ntext=ns[".text"]
    net_bytes=net[ntext[4]:ntext[4]+ntext[5]]
    uc=Uc(UC_ARCH_ARM,UC_MODE_ARM|UC_MODE_BIG_ENDIAN)
    pages=set()
    for s in sections.values():
        if s[2]&2 and s[5]:
            pages.update(range(s[3]&~4095,(s[3]+s[5]+4095)&~4095,4096))
    for p in sorted(pages):uc.mem_map(p,4096)
    for s in sections.values():
        if s[2]&2 and s[5] and s[1]!=8:uc.mem_write(s[3],kernel[s[4]:s[4]+s[5]])
    uc.mem_map(ntext[3]&~4095,4096)
    uc.mem_write(ntext[3],net_bytes)
    uc.mem_map(0x08131000,4096)
    uc.mem_map(IMAGE,CAPACITY)
    uc.mem_write(IMAGE,bytes(original))
    if full_reload:
        # Model the caller-supplied launch/header placement only. CP15 and
        # interrupt state are stubbed below; there is no bootrom execution.
        uc.mem_map(IMAGE-4096,4096)
        uc.mem_write(IMAGE-0x658,struct.pack(">I",0 if reject_header else 0x00dfd000))
        for module,base,capacity in (("ios_mcp",0x13d96000,0xa000),
                                     ("ios_fs",0x107f8200,0x2000)):
            payload=(project/"source"/module/(module+".bin")).read_bytes()
            first=base&~4095
            uc.mem_map(first,(base+capacity-first+4095)&~4095)
            uc.mem_write(base,payload)
        uc.mem_map(0x081e9000,4096) # MCP existing USB logging marker address
        for base in (0x0812e000,0x0812a000):uc.mem_map(base,4096)
    uc.mem_map(STACK,STACK_SIZE)
    uc.mem_map(STOP,4096)
    uc.reg_write(UC_ARM_REG_R0,IMAGE-0x504 if full_reload else IMAGE)
    for reg,value in ((UC_ARM_REG_R1,0x1111),(UC_ARM_REG_R2,0x2222),(UC_ARM_REG_R3,0x3333)):
        uc.reg_write(reg,value)
    uc.reg_write(UC_ARM_REG_SP,STACK+STACK_SIZE-16)
    uc.reg_write(UC_ARM_REG_LR,STOP)
    calls=[]; moves=[]; returned=False; order=[]; bss_events=[]
    diag=symbols.get("rtl_mocha_reload_diag")
    diag_writes=[]
    def write(uc,access,address,size,value,user):
        assert ((IMAGE<=address and address+size<=IMAGE+CAPACITY) or
                (STACK<=address and address+size<=STACK+STACK_SIZE) or
                (diag is not None and diag<=address and address+size<=diag+16)),hex(address)
        if diag is not None and diag<=address<diag+16:
            diag_writes.append((address-diag,value))
    def instruction(uc,address,size,user):
        nonlocal returned
        if address==STOP:
            returned=True;uc.emu_stop();return
        if full_reload:
            for name in ("mcp_run_patches","kernel_run_patches","fs_run_patches","net_run_patches"):
                if address==symbols[name]:order.append(name)
            word=struct.unpack(">I",bytes(uc.mem_read(address,4)))[0]
            if word & 0x0f000f10 == 0x0e000f10:
                # MRC/MCR p15 are execution-context effects outside this test.
                if word & 0x00100000:
                    assert word & 0x0fff0fff == 0x0e110f10,hex(word)
                    reg=(word>>12)&15
                    assert reg<13
                    uc.reg_write(UC_ARM_REG_R0+reg,0x1005)
                uc.reg_write(UC_ARM_REG_PC,address+4);return
            if address in (0x0812e778,0x0812e78c):
                uc.reg_write(UC_ARM_REG_R0,0)
                uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR));return
            if address==0x0812a050:
                assert [uc.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]==[IMAGE-0x504,0x1111,0x2222,0x3333]
                returned=True;uc.emu_stop();return
        if address==symbols["section_write_bss"]:
            bss_events.append((len(calls),uc.reg_read(UC_ARM_REG_R1),uc.reg_read(UC_ARM_REG_R2)))
        if address==symbols["section_write"]:
            dest=uc.reg_read(UC_ARM_REG_R1); ptr=uc.reg_read(UC_ARM_REG_R2); count=uc.reg_read(UC_ARM_REG_R3)
            try: content=bytes(uc.mem_read(ptr,count))
            except Exception as exc: raise AssertionError((hex(dest),hex(ptr),count)) from exc
            calls.append((dest,content))
        if address in (0x08131d04,symbols["reverse_memcpy"]):
            dst=uc.reg_read(UC_ARM_REG_R0);src=uc.reg_read(UC_ARM_REG_R1);count=uc.reg_read(UC_ARM_REG_R2)
            assert count<CAPACITY and IMAGE<=dst and dst+count<=IMAGE+CAPACITY
            assert ((IMAGE<=src and src+count<=IMAGE+CAPACITY) or
                    (ntext[3]<=src and src+count<=ntext[3]+len(net_bytes)) or
                    (STACK<=src and src+count<=STACK+STACK_SIZE) or
                    (full_reload and (0x08135000<=src and src+count<=0x08139200 or
                     0x13d96000<=src and src+count<=0x13da0000 or
                     0x107f8200<=src and src+count<=0x107fa200))),hex(src)
            uc.mem_write(dst,bytes(uc.mem_read(src,count)))
            moves.append((dst,src,count))
            uc.reg_write(UC_ARM_REG_R0,dst)
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    uc.hook_add(UC_HOOK_MEM_WRITE,write)
    uc.hook_add(UC_HOOK_CODE,instruction)
    uc.emu_start(symbols["kernel_launch_ios" if full_reload else "net_run_patches"],0,count=300000)
    assert returned,"instruction limit/patcher did not return"
    assert uc.reg_read(UC_ARM_REG_SP)==STACK+STACK_SIZE-16
    hook_symbol=next((n for n in ("rtl_mocha_net_init_alloc_probe","rtl_mocha_net_init_entry_probe")
                      if n in net_symbols),None)
    experimental=hook_symbol is not None
    if full_reload and experimental:
        assert diag is not None
        words = [struct.unpack(">I", bytes(uc.mem_read(diag+i*4,4)))[0] for i in range(4)]
        if reject_header:
            assert words==[0x524c4445,0,0,0]
            assert diag_writes==[(0,0x524c4445)]
            assert not calls and not bss_events and not moves and not order
            assert bytes(uc.mem_read(IMAGE,len(original)))==original
            return dict(writes=0,moves=0,rejected_header=True)
        else:
            assert words==[0x524c4445,0x48445250,0x4e455450,0]
            assert diag_writes==[(0,0x524c4445),(4,0x48445250),(8,0x4e455450)]
    if not full_reload: assert calls[0]==(0x12431900,net_bytes)
    expected_addresses=[0x12431900,0x1239da7c,0x1239e108,0x1239e10c,0x1239e110]
    expected_addresses+=list(range(0x12455368,0x12455368+24,4))
    expected_addresses+=list(range(0x12455490,0x12455490+24,4))
    expected_addresses += [0x1237f33c,0x123a4448,0x1239de98,0x1237f310,0x1239dfa0,0x1239dfc0,0x1239dfd8,0x1239dfec,0x1239e020,0x1239e094,0x123a457c]
    expected_mailbox=[] if not experimental or negative else [(0x1288d040,64)]
    assert [(a,n) for _,a,n in bss_events if a==0x1288d040]==expected_mailbox
    if experimental and not negative: expected_addresses += [0x12300ae4]
    if not full_reload: assert [a for a,b in calls]==expected_addresses
    else:
        assert order==["mcp_run_patches","kernel_run_patches","fs_run_patches","net_run_patches"]
        extra = 1 if diag is not None else 0
        net_calls=calls[:-extra] if extra else calls
        assert [a for a,b in net_calls[-len(expected_addresses):]]==expected_addresses
        if diag is not None:
            assert calls[-1]==(diag,struct.pack(">4I",0x524c4445,0x48445250,0x4e455450,0))
    # Independent oracle: insert each extension once, shifting file data/PHDRs,
    # then apply the observed, whitelisted candidate/DLP writes at virtual VAs.
    expected=bytearray(original)
    hdr=struct.unpack_from(">HHIIIIIHHHHHH",expected,16)
    for call_index,(address,content) in enumerate(calls):
        for before,bss_address,bss_bytes in bss_events:
            if before!=call_index:continue
            bs=phdrs(expected)
            candidates=[j for j,s in enumerate(bs[:-1]) if s[2]<=bss_address<bs[j+1][2]]
            assert len(candidates)==1
            j=candidates[0];bs[j][5]=max(bs[j][5],bss_address-bs[j][2]+bss_bytes)
            struct.pack_into(">8I",expected,hdr[4]+j*32,*bs[j])
        segments=phdrs(expected)
        matches=[i for i,s in enumerate(segments) if s[0]==1 and s[2]<=address<s[2]+s[5]]
        # Initial Mocha extension begins just past the original NET p_memsz;
        # choose its text segment by the next virtual segment boundary.
        if not matches and address==0x12431900:
            matches=[i for i,s in enumerate(segments) if s[0]==1 and s[2]==0x12300000]
        if not matches and full_reload:
            matches=[j for j,s in enumerate(segments[:-1]) if s[2]<=address<segments[j+1][2]]
        assert len(matches)==1
        i=matches[0];s=segments[i]; end=address-s[2]+len(content)
        if end>s[4]:
            extra=end-s[4];insert=s[1]+s[4]
            # Upstream section_write moves later file payloads but does not
            # zero the exposed gap before writing the candidate. Model the
            # retained old bytes explicitly; no zero-fill guarantee is made.
            exposed=bytes(expected[insert:insert+extra]).ljust(extra,b"\0")
            expected[insert:insert]=exposed
            for j,t in enumerate(segments):
                if t[1]>s[1]:t[1]+=extra
            s[4]=end;s[5]=max(s[5],end)
            for j,t in enumerate(segments):struct.pack_into(">8I",expected,hdr[4]+j*32,*t)
        pos=s[1]+address-s[2]
        expected[pos:pos+len(content)]=content
    actual=bytes(uc.mem_read(IMAGE,len(expected)))
    if full_reload and diag is not None:
        assert actual[at_va(actual,diag,16):at_va(actual,diag,16)+16]==struct.pack(">4I",0x524c4445,0x48445250,0x4e455450,0)
    # Upstream only preserves PT_LOAD data and PHDRs when expanding the image;
    # section headers/trailing non-loaded bytes aren't a boot input guarantee.
    assert actual[:hdr[4]+hdr[9]*32]==expected[:hdr[4]+hdr[9]*32]
    for s in phdrs(expected):
        if s[0]==1 and s[4]:
            left=actual[s[1]:s[1]+s[4]]; right=expected[s[1]:s[1]+s[4]]
            if left!=right:
                mismatch=next(i for i,(a,b) in enumerate(zip(left,right)) if a!=b)
                raise AssertionError((hex(s[2]),hex(s[2]+mismatch),left[mismatch:mismatch+12].hex(),right[mismatch:mismatch+12].hex()))
    if experimental:
        pos=at_va(actual,0x12300ae4,4)
        word=struct.unpack_from(">I",actual,pos)[0]
        wanted=struct.unpack_from(">I",original,at_va(original,0x12300ae4,4))[0] if negative else branch(0x12300ae4,net_symbols[hook_symbol])
        assert word==wanted
        bss=next(s for s in phdrs(actual) if s[0]==1 and s[2]==0x124c6000)
        original_bss=next(s for s in phdrs(original) if s[0]==1 and s[2]==0x124c6000)
        assert bss[4]==0 and bss[5]==(original_bss[5] if negative else 0x3c7080)
    return dict(writes=len(calls),moves=len(moves),text_bytes=len(net_bytes),negative=negative,full_reload=full_reload)


def main():
    original=(root/"evidence/firmware-5.5.6/analysis/ios.elf").read_bytes()
    assert hashlib.sha256(original).hexdigest()=="8ac1ae366b2925f66f19b7f50033103f0ecee45847c9198aa78343a8cd9c4e23"
    # Optional argument: experiment directory (default: allocation experiment).
    project=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/"build/mocha-alloc-experiment"
    entry="rtl_mocha_net_init_entry_probe" in elf_sections((project/"source/ios_net/ios_net.elf").read_bytes())[1]
    results=[emulate(root/"upstream/MochaPayload",original),emulate(project,original)]
    damage=[0x12300ae4,0x123e4394]
    if entry: damage+=[0x123e44e4,0x12300ab4,0x12300c18]  # FlushDCache svc, heap creation, heap literal
    for address in damage:
        damaged=bytearray(original);pos=at_va(damaged,address,4);damaged[pos+3]^=1
        results.append(emulate(project,damaged,negative=True))
    damaged=bytearray(original)
    h=struct.unpack_from(">HHIIIIIHHHHHH",damaged,16)
    for i,s in enumerate(phdrs(damaged)):
        if s[0]==1 and s[2]==0x124c6000:
            struct.pack_into(">I",damaged,h[4]+i*32+20,s[5]+4)
    results.append(emulate(project,damaged,negative=True))
    results.append(emulate(project,original,full_reload=True))
    damaged=bytearray(original);pos=at_va(damaged,0x12300ae4,4);damaged[pos+3]^=1
    results.append(emulate(project,damaged,negative=True,full_reload=True))
    results.append(emulate(project,original,full_reload=True,reject_header=True))
    print("ARM boot patcher:",json.dumps(results))
    print("Actual NET guard/section_write execute; bounded memory-copy stubs; PT_LOAD/PHDRs match independent insertion oracle. No console activation.")


if __name__=="__main__":main()

