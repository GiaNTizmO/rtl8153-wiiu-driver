"""Local read-only IOSU ELF map and network strings; never reads OTP files."""
import argparse, hashlib, json, re, struct
from pathlib import Path

def inspect(path, output):
    b=Path(path).read_bytes()
    elf=b.find(b"\x7fELF")
    if elf<0: raise ValueError("ELF not found")
    if b[elf+4:elf+6]!=b"\x01\x02": raise ValueError("Expected ELF32 big endian")
    h=struct.unpack_from(">HHIIIIIHHHHHH",b,elf+16)
    phoff, entsize, count=h[4],h[8],h[9]
    segments=[]
    for i in range(count):
        fields=struct.unpack_from(">8I",b,elf+phoff+i*entsize)
        kind,off,va,pa,fs,ms,flags,align=fields
        if off+fs>len(b)-elf: raise ValueError("segment outside file")
        segments.append(dict(index=i,type=kind,offset=off,vaddr=va,paddr=pa,
                             filesz=fs,memsz=ms,flags=flags,uid=(flags>>20)&0xff))
    def mapping(off):
        for s in segments:
            if s["type"]==1 and s["offset"]<=off<s["offset"]+s["filesz"]:
                return s,s["vaddr"]+off-s["offset"]
        return None,None
    strings=[]
    for m in re.finditer(rb"[\x20-\x7e]{5,}",b):
        s=m.group().decode("ascii")
        if re.search(r"usb_eth|/dev/ifnet|/dev/net/ifmgr|IFNET_ETH|ifnet_.*write|ifnet_.*ioctl",s,re.I):
            seg,va=mapping(m.start()-elf)
            strings.append(dict(offset=m.start(),address=va,segment=seg["index"] if seg else None,text=s))
    result=dict(source_sha256=hashlib.sha256(b).hexdigest(),elf_offset=elf,segments=segments,strings=strings)
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    (output/"map.json").write_text(json.dumps(result,indent=2)+"\n")
    (output/"ios.elf").write_bytes(b[elf:])
    for s in segments:
        if s["type"]==1 and s["filesz"] and s["uid"] in (4,7):
            data=b[elf+s["offset"]:elf+s["offset"]+s["filesz"]]
            (output/f'uid{s["uid"]}_{s["vaddr"]:08x}.bin').write_bytes(data)
    for s in segments:
        if s["uid"] in (4,7):
            print(f'[{s["index"]:02}] uid={s["uid"]} va={s["vaddr"]:08x} pa={s["paddr"]:08x} file={s["filesz"]:x} mem={s["memsz"]:x} flags={s["flags"]:x}')
    for s in strings: print(f'{s["address"] or 0:08x} {s["text"]}')
if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("image");p.add_argument("output")
    a=p.parse_args();inspect(a.image,a.output)
