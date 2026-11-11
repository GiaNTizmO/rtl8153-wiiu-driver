"""ARM literal and raw pointer cross references in an extracted IOSU ELF."""
import argparse,json,struct
from pathlib import Path
def analyze(folder):
    root=Path(folder);m=json.loads((root/"map.json").read_text());b=(root/"ios.elf").read_bytes()
    segs=[s for s in m["segments"] if s["type"]==1 and s["filesz"]]
    def u32(va):
        for s in segs:
            if s["vaddr"]<=va<=s["vaddr"]+s["filesz"]-4:
                return struct.unpack_from(">I",b,s["offset"]+va-s["vaddr"])[0]
        return None
    result=[]
    for string in m["strings"]:
        target=string["address"]
        if not target or target<0x12300000:continue
        refs=[]
        for s in segs:
            data=b[s["offset"]:s["offset"]+s["filesz"]]
            pattern=struct.pack(">I",target)
            off=0
            while True:
                off=data.find(pattern,off)
                if off<0:break
                refs.append(s["vaddr"]+off);off+=4
        loads=[]
        for s in segs:
            if not s["flags"]&1 or s["uid"]!=7:continue
            for off in range(0,s["filesz"]-3,4):
                w=struct.unpack_from(">I",b,s["offset"]+off)[0]
                # ARM LDR immediate, pre-indexed, word, Rn=PC, no writeback.
                if w & 0x0f7f0000 != 0x051f0000:continue
                pc=s["vaddr"]+off
                literal=pc+8+(w&0xfff)*(1 if w&0x00800000 else -1)
                if u32(literal)==target:loads.append(pc)
        result.append(dict(**string,pointers=refs,arm_literal_loads=loads))
    (root/"network_xrefs.json").write_text(json.dumps(result,indent=2)+"\n")
    for r in result:
        print(f'{r["address"]:08x} {r["text"]}')
        print('  loads: '+', '.join(f'{v:08x}' for v in r["arm_literal_loads"]))
        print('  pointers: '+', '.join(f'{v:08x}' for v in r["pointers"]))
if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("folder");a=p.parse_args();analyze(a.folder)
