"""Compare project-local ARM payloads embedded in a saved RPX. Read only."""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path


def sections(data):
    if len(data)<52 or data[:7]!=b"\x7fELF\x01\x02\x01": raise ValueError("invalid RPX")
    h=struct.unpack_from(">HHIIIIIHHHHHH",data,16)
    if h[0]!=0xfe01 or h[1]!=20 or h[10]!=40 or h[11]>256:raise ValueError("expected PPC RPX")
    if h[5]+h[11]*40>len(data):raise ValueError("truncated section table")
    result=[]
    for i in range(h[11]):
        s=struct.unpack_from(">10I",data,h[5]+i*40)
        if s[1]==8 or not s[5]:continue
        if s[4]+s[5]>len(data):raise ValueError("section outside RPX")
        b=data[s[4]:s[4]+s[5]]
        if s[2]&0x08000000:
            if len(b)<4:raise ValueError("short compressed section")
            n=struct.unpack_from(">I",b)[0]
            if n>32*1024*1024:raise ValueError("excessive section size")
            d=zlib.decompressobj(); out=d.decompress(b[4:],n+1)
            if len(out)!=n or not d.eof or d.unconsumed_tail or d.unused_data:
                raise ValueError("invalid compressed section")
            b=out
        result.append((i,s[3],b))
    return result


def compare(rpx, project):
    data=rpx.read_bytes();blocks=sections(data);matches={}
    for name in ("ios_net","ios_usb","ios_fs","ios_mcp","ios_kernel"):
        payload=(project/"source"/name/(name+".bin")).read_bytes()
        found=[]
        for i,base,b in blocks:
            at=b.find(payload)
            if at>=0:found.append(dict(section=i,section_offset=at,ppc_address=hex(base+at)))
        matches[name]=dict(bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest(),exact_matches=found)
    return dict(rpx=str(rpx),bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),embedded_payloads=matches)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("rpx",type=Path)
    p.add_argument("--baseline",type=Path,default=Path("upstream/MochaPayload"))
    p.add_argument("--output",type=Path,required=True);a=p.parse_args()
    report=compare(a.rpx,a.baseline)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({k:bool(v["exact_matches"]) for k,v in report["embedded_payloads"].items()}))


if __name__=="__main__":main()

