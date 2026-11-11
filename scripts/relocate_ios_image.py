"""Offline ARM big-endian ELF32 ET_REL image builder. Never contacts a console.

Load address must come from a future verified allocator/mapping, not this tool.
Only ABS32, CALL and JUMP24 relocations are accepted; unknown inputs fail closed.
External ARM branches use literal veneers, avoiding a +/-32 MiB import limit.
Output includes BSS zeros and a manifest, but is NOT an installable driver.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]

def put32(data, offset, value):
    struct.pack_into(">I", data, offset, value & 0xffffffff)

def align(value, alignment):
    if not alignment or alignment & (alignment - 1):
        raise ValueError("invalid alignment")
    return (value + alignment - 1) & -alignment

class ArmRelocatable:
    def __init__(self, data):
        self.data = data
        if len(data) < 52 or data[:7] != b"\x7fELF\x01\x02\x01":
            raise ValueError("expected ELF32 big-endian version 1")
        h = struct.unpack_from(">HHIIIIIHHHHHH", data, 16)
        if h[0:3] != (1, 40, 1) or h[10] != 40 or not h[11]:
            raise ValueError("expected ARM ET_REL with section table")
        if h[4] or h[9]:
            raise ValueError("program headers in relocatable input")
        if h[5] + h[11] * 40 > len(data) or h[12] >= h[11]:
            raise ValueError("truncated section table")
        self.sections = []
        for i in range(h[11]):
            s = dict(zip(("nameoff","type","flags","address","offset","size",
                          "link","info","align","entsize"),
                         struct.unpack_from(">10I", data, h[5] + i*40)))
            if s["type"] != 8 and s["offset"] + s["size"] > len(data):
                raise ValueError("section outside file")
            self.sections.append(s)
        strings = self.section_data(h[12])
        for s in self.sections:
            s["name"] = self.string(strings, s["nameoff"])

    @staticmethod
    def string(data, offset):
        end = data.find(b"\0", offset)
        if offset >= len(data) or end < 0:
            raise ValueError("invalid string table reference")
        return data[offset:end].decode("ascii")

    def section_data(self, index):
        if index >= len(self.sections):
            raise ValueError("section index outside table")
        s = self.sections[index]
        return self.data[s["offset"]:s["offset"]+s["size"]]

    def build(self, base, imports):
        if base & 63 or not 0 < base <= 0xffffffff:
            raise ValueError("load base must be a nonzero 64-byte aligned ARM address")
        positions = {}; size = 0
        for i,s in enumerate(self.sections):
            if not s["flags"] & 2:
                continue
            if s["type"] not in (1,8) or s["flags"] & 0x400:
                raise ValueError("unsupported allocated section")
            a = s["align"] or 1
            if a > 4096 or base % a:
                raise ValueError("unsupported load alignment")
            size = align(size,a);positions[i] = size;size += s["size"]
        if not positions or size > 1024*1024 or base + size > 0x100000000:
            raise ValueError("invalid image size/address range")
        image = bytearray(size); layout = []
        for i,offset in positions.items():
            s = self.sections[i]
            if s["type"] != 8:
                image[offset:offset+s["size"]] = self.section_data(i)
            layout.append(dict(name=s["name"],offset=offset,size=s["size"],flags=s["flags"]))
        symbols = {}
        for i,s in enumerate(self.sections):
            if s["type"] != 2:
                continue
            if s["entsize"] != 16 or s["size"] % 16:
                raise ValueError("invalid symbol table")
            names = self.section_data(s["link"]); table = []
            for off in range(0,s["size"],16):
                name,value,n,info,other,section = struct.unpack_from(">IIIBBH",self.section_data(i),off)
                table.append(dict(name=self.string(names,name),value=value,size=n,info=info,section=section))
            symbols[i] = table
        def resolve(symbol):
            sec = symbol["section"];value=symbol["value"]
            if sec == 0:
                if symbol["name"] not in imports:
                    raise ValueError("unresolved import: " + symbol["name"])
                return imports[symbol["name"]],True
            if sec == 0xfff1:
                return value,True
            if sec not in positions or value > self.sections[sec]["size"]:
                raise ValueError("symbol outside allocated section")
            return base+positions[sec]+value,False
        veneers = {};used_imports = {};relocations=0;sites=set()
        for i,s in enumerate(self.sections):
            if s["type"] == 4:
                raise ValueError("RELA unsupported")
            if s["type"] != 9:
                continue
            if s["info"] not in positions or s["link"] not in symbols:
                raise ValueError("relocation outside allocated image")
            if s["entsize"] != 8 or s["size"] % 8:
                raise ValueError("invalid REL table")
            table=symbols[s["link"]];target=self.sections[s["info"]]
            for off in range(0,s["size"],8):
                location,info=struct.unpack_from(">II",self.section_data(i),off)
                kind=info&255;symbol_index=info>>8
                if location & 3 or location+4 > target["size"] or symbol_index >= len(table):
                    raise ValueError("invalid relocation location/symbol")
                position=positions[s["info"]]+location
                if position in sites:
                    raise ValueError("duplicate relocation")
                sites.add(position)
                symbol=table[symbol_index];address,external=resolve(symbol)
                if not 0 <= address <= 0xffffffff:
                    raise ValueError("symbol address overflow")
                if symbol["section"] == 0:
                    used_imports[symbol["name"]]=address
                word=u32(image,position)
                if kind == 2:
                    put32(image,position,address+word)
                elif kind in (28,29):
                    if word & 0x0e000000 != 0x0a000000 or word >> 28 == 15 or address & 3:
                        raise ValueError("unsupported ARM branch/Thumb target")
                    addend=(word&0xffffff)<<2
                    if addend&0x2000000:addend-=0x4000000
                    if external:
                        # The ordinary assembler addend is -8; fold any custom
                        # addend into the absolute veneer destination.
                        destination=address+addend+8
                        if destination&3 or not 0 <= destination <= 0xffffffff:
                            raise ValueError("veneer target overflow/alignment")
                        if destination not in veneers:
                            at=align(len(image),4)
                            image.extend(b"\0"*(at-len(image)))
                            image.extend(struct.pack(">II",0xe51ff004,destination))
                            veneers[destination]=base+at
                        delta=veneers[destination]-(base+position)-8
                    else:
                        delta=address+addend-(base+position)
                    if delta&3 or not -0x2000000 <= delta < 0x2000000:
                        raise ValueError("branch relocation out of range")
                    put32(image,position,(word&0xff000000)|((delta>>2)&0xffffff))
                else:
                    raise ValueError("unsupported ARM relocation: "+str(kind))
                relocations+=1
        if base+len(image)>0x100000000:
            raise ValueError("image end overflow")
        exports={}
        for table in symbols.values():
            for symbol in table:
                if symbol["info"]>>4 == 1 and symbol["section"] in positions and symbol["name"]:
                    exports[symbol["name"]]=resolve(symbol)[0]
        manifest=dict(base=base,bytes=len(image),sections=layout,imports=used_imports,
                      exports=exports,relocations=relocations,veneers=len(veneers),
                      input_sha256=hashlib.sha256(self.data).hexdigest(),
                      image_sha256=hashlib.sha256(image).hexdigest(),
                      status="offline relocated core only; allocation, mapping, entrypoint and driver ops not installed")
        return bytes(image),manifest

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("elf");p.add_argument("output")
    p.add_argument("--base",required=True,type=lambda x:int(x,0))
    p.add_argument("--imports",required=True,help="JSON mapping names to integer or hex-string addresses")
    a=p.parse_args();imports=json.loads(Path(a.imports).read_text())
    imports={k:int(v,0) if isinstance(v,str) else v for k,v in imports.items()}
    image,manifest=ArmRelocatable(Path(a.elf).read_bytes()).build(a.base,imports)
    dest=Path(a.output);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(image)
    dest.with_suffix(dest.suffix+".json").write_text(json.dumps(manifest,indent=2)+"\n")
    print(f"Offline image: {len(image)} bytes, {manifest['relocations']} relocations, {manifest['veneers']} veneers")
if __name__=="__main__":main()
