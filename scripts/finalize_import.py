"""Record dependency provenance without modifying toolchains."""
import json
import subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
repos={
    "networking_base":"upstream/ax88179_aroma_driver",
    "WiiUModuleSystem":"vendor/wums",
    "libmocha":"vendor/libmocha",
    "libfunctionpatcher":"vendor/functionpatcher",
}
result={}
for name,folder in repos.items():
    def git(*args):
        return subprocess.check_output(["git","-C",str(root/folder),*args],text=True).strip()
    result[name]={"url":git("remote","get-url","origin"),"commit":git("rev-parse","HEAD")}
result["lwIP"]={"url":"https://github.com/lwip-tcpip/lwip",
    "commit":"77dcd25a72509eb83f72b033d219b1d40cd8eb95",
    "notes":"2.2.1 subset with upstream Aroma lifecycle/socket patches; vendor/lwip/UPSTREAM.md"}
result["OpenBSD"]={"url":"https://github.com/openbsd/src/tree/master/sys/dev/usb",
    "if_ure.c":"1.39 2026/11/06", "if_urereg.h":"1.16 2026/11/06",
    "snapshot":"references/openbsd-if_ure.c and references/openbsd-if_urereg.h"}
(root/"docs/dependencies.json").write_text(json.dumps(result,indent=2)+"\n")
