"""Compare offline relocations with GNU ld, including ARM branch targets."""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/"scripts"))
from relocate_ios_image import ArmRelocatable,u32

def branch_target(word,pc):
    n=(word&0xffffff)<<2
    if n&0x2000000:n-=0x4000000
    return (pc+8+n)&0xffffffff

def run(elf,ld):
    source=Path(elf).read_bytes();obj=ArmRelocatable(source)
    # The native core is freestanding. Keep this map empty so an accidental
    # libc/compiler dependency fails closed instead of being guessed at runtime.
    imports={}
    for base in (0x12500000,0x08000000,0x1f000000):
        image,manifest=obj.build(base,imports)
        assert manifest["relocations"]>=207 and manifest["veneers"]==0
        assert manifest["imports"]=={}
        # GNU ld is the independent ARM EABI relocation oracle. A nearby
        # import avoids linker-generated veneers in the oracle image.
        near={}
        actual,check=obj.build(base,near)
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp);script=tmp/"image.ld";output=tmp/"oracle.elf"
            lines=["SECTIONS {"]
            for s in check["sections"]:
                lines.append(f'. = 0x{base+s["offset"]:x}; {s["name"]} : {{ *({s["name"]}) }}')
            lines += ["/DISCARD/ : { *(.ARM.attributes) *(.comment) *(.note*) }","}"]
            lines += [f'{name} = 0x{address:x};' for name,address in near.items()]
            script.write_text("\n".join(lines)+"\n")
            subprocess.run([ld,"-EB","-T",str(script),"-o",str(output),str(elf)],check=True,capture_output=True)
            oracle=output.read_bytes();h=struct.unpack_from(">HHIIIIIHHHHHH",oracle,16)
            expected=bytearray(len(actual))
            for i in range(h[11]):
                s=struct.unpack_from(">10I",oracle,h[5]+i*40)
                if s[2]&2 and s[1]!=8:
                    pos=s[3]-base;assert pos>=0
                    expected[pos:pos+s[5]]=oracle[s[4]:s[4]+s[5]]
            allowed=set()
            for s in obj.sections:
                if s["type"]!=9:continue
                target=obj.sections[s["info"]]
                pos=next(v["offset"] for v in check["sections"] if v["name"]==target["name"])
                for off in range(0,s["size"],8):
                    location,info=struct.unpack_from(">II",obj.data,s["offset"]+off)
                    p=pos+location;kind=info&255
                    if kind in (28,29):
                        dest=branch_target(u32(actual,p),base+p)
                        v=dest-base
                        if 0<=v<=len(actual)-8 and u32(actual,v)==0xe51ff004:
                            dest=u32(actual,v+4)
                        assert dest==branch_target(u32(expected,p),base+p),(hex(p),hex(dest))
                        allowed.update(range(p,p+4))
                    else:assert actual[p:p+4]==expected[p:p+4]
            core_end=max(s["offset"]+s["size"] for s in check["sections"])
            for i in range(core_end):
                if i not in allowed:assert actual[i]==expected[i],hex(i)
        assert image==obj.build(base,imports)[0]
    for base in (0,1,0xffffffc0):
        try:obj.build(base,imports)
        except ValueError:pass
        else:raise AssertionError("accepted invalid base")
    # Truncation, machine/endian damage, unknown relocation and duplicate sites.
    mutations=[source[:n] for n in (0,4,51,len(source)-1)]
    for at in (4,5,18):
        b=bytearray(source);b[at]^=255;mutations.append(b)
    rel=next(s for s in obj.sections if s["type"]==9 and s["size"]>=16)
    b=bytearray(source);b[rel["offset"]+7]=255;mutations.append(b)
    b=bytearray(source);b[rel["offset"]+8:rel["offset"]+12]=b[rel["offset"]:rel["offset"]+4];mutations.append(b)
    for b in mutations:
        try:ArmRelocatable(b).build(0x12500000,imports)
        except (ValueError,UnicodeError):pass
        else:raise AssertionError("accepted malformed ELF")
    print(f"relocation: {manifest['relocations']} relocations at three bases match GNU ld, BSS, bounds and malformed input passed")

if __name__=="__main__":run(Path(sys.argv[1]),sys.argv[2])
