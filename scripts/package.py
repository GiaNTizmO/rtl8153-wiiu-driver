"""Build distributable SD archive and a matching source archive; no network."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile
root=Path(__file__).resolve().parents[1]
dist=root/"dist"
sd=dist/"sd"
version="0.1.1-recovery-fix"
license_dir=sd/"wiiu/rtl8153/licenses"
license_dir.mkdir(parents=True,exist_ok=True)
for src,name in (("LICENSE","PROJECT-GPL-3.0.txt"),
                 ("vendor/lwip/COPYING","lwIP-BSD.txt"),
                 ("vendor/wums/LICENSE","WUMS.txt"),
                 ("vendor/libmocha/LICENSE","libmocha.txt"),
                 ("vendor/functionpatcher/LICENSE","libfunctionpatcher.txt")):
    shutil.copy2(root/src,license_dir/name)
bsd=(root/"driver/rtl8153_bsd.inc").read_text()
(license_dir/"OpenBSD-BSD.txt").write_text(bsd[:bsd.index("uint16_t")])
shutil.copy2(root/"README.md",dist/"README.md")
shutil.copy2(root/"docs/VALIDATION.md",dist/"VALIDATION.md")
shutil.copy2(root/"docs/dependencies.json",dist/"dependencies.json")
manifest={"version":version,"hardware_tested":False,
          "validation":"basic networking tested in 0.1.0; timer fix local only; native IOS-NET unfinished",
          "target":"Wii U 5.5.6 / Aroma / USB 0bda:8153",
          "files":{}}
for p in sorted(sd.rglob("*")):
    if p.is_file():
        manifest["files"][p.relative_to(sd).as_posix()]={
            "size":p.stat().st_size,"sha256":hashlib.sha256(p.read_bytes()).hexdigest()}
(dist/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
with zipfile.ZipFile(dist/f"RTL8153-Aroma-{version}.zip","w",zipfile.ZIP_DEFLATED) as z:
    for p in sorted(sd.rglob("*")):
        if p.is_file(): z.write(p,p.relative_to(sd))
    for name in ("README.md","VALIDATION.md","dependencies.json","manifest.json"):
        z.write(dist/name,"RTL8153-docs/"+name)
skip_dirs={".git","build","release","debug","lib","__pycache__"}
with zipfile.ZipFile(dist/f"RTL8153-Aroma-{version}-source.zip","w",zipfile.ZIP_DEFLATED) as z:
    for folder in ("aroma_module","driver","net","native","tools","vendor","tests","scripts","docs"):
        for p in sorted((root/folder).rglob("*")):
            rel=p.relative_to(root)
            if not p.is_file() or any(part in skip_dirs for part in rel.parts): continue
            if p.suffix in (".o",".d",".elf",".map",".wms",".a",".bz2"): continue
            z.write(p,rel)
    for name in ("Makefile","README.md","LICENSE",".gitignore"):
        z.write(root/name,name)
    for name in ("openbsd-if_ure.c","openbsd-if_urereg.h"):
        z.write(root/"references"/name,"references/"+name)
for name in (f"RTL8153-Aroma-{version}.zip",f"RTL8153-Aroma-{version}-source.zip"):
    p=dist/name
    print(name,p.stat().st_size,hashlib.sha256(p.read_bytes()).hexdigest())
