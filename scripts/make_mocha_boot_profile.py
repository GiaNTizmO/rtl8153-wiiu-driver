"""Generate immutable-code identities for a read-only console inspection.
The baseline must match payloads embedded in the owner's verified official RPX.
No firmware bytes, IOS writes, launch commands, or console connections.
"""
import argparse
import hashlib
import json
from pathlib import Path
from check_mocha_alloc_candidate import elf_sections, branch
from compare_mocha_rpx import compare

ROOT = Path(__file__).resolve().parents[1]
OFFICIAL = "16921b4059bdc434b481c16ef8380e16c89ff8b2ad78da62d19a2d372d0bc529"


def generate(candidate_dir="build/mocha-alloc-experiment", out_dir="build/mocha-boot-state"):
    candidate_project = ROOT / candidate_dir
    original = ROOT / "evidence/2026-11-09/mocha-installed/00_mocha.rpx"
    if hashlib.sha256(original.read_bytes()).hexdigest() != OFFICIAL:
        raise ValueError("unverified official baseline")
    baseline = ROOT / "upstream/MochaPayload"
    embedded = compare(original, baseline)["embedded_payloads"]
    if not all(embedded[n]["exact_matches"]
               for n in ("ios_kernel", "ios_net")):
        raise ValueError("baseline kernel/NET differs from installed release")
    lines = ["/* Generated from verified Mocha and local allocation candidate. */",
             "#include <stdint.h>"]
    manifest = {}
    for label, project in (("official", baseline),
                           ("candidate", candidate_project)):
        manifest[label] = {}
        for module, start, bound in (("ios_kernel", 0x08135000, 0x08139200),
                                      ("ios_net", 0x12431900, 0x12432000)):
            data = (project / "source" / module / (module + ".elf")).read_bytes()
            sections, symbols = elf_sections(data)
            s = sections[".text"]
            if s[3] != start or not s[5] or start + ((s[5]+3)&~3) > bound:
                raise ValueError("code bounds/alignment mismatch")
            code = data[s[4]:s[4]+s[5]]
            key = label + "_" + module
            words = [int.from_bytes(code[i:i+4].ljust(4, b"\0"), "big") for i in range(0, len(code), 4)]
            lines.append("#define %s_BYTES %du" % (key.upper(), len(code)))
            lines.append("static const uint32_t %s[] = {" % key)
            for i in range(0, len(words), 8):
                lines.append("    " + ", ".join("0x%08xu" % w for w in words[i:i+8]) + ",")
            lines.append("};")
            manifest[label][module] = dict(address=hex(start), bytes=len(code),
                                          sha256=hashlib.sha256(code).hexdigest())
            if module == "ios_kernel":
                word = branch(0x0812a120, symbols["kernel_launch_ios"])
                lines.append("#define %s_RELOAD_WORD 0x%08xu" % (label.upper(), word))
                manifest[label]["reload_word"] = hex(word)
                if label == "candidate":
                    diag = symbols.get("rtl_mocha_reload_diag")
                    if diag is None:
                        raise ValueError("candidate reload diagnostic symbol missing")
                    lines.append("#define CANDIDATE_RELOAD_DIAG_ADDRESS 0x%08xu" % diag)
                    manifest[label]["reload_diag_address"] = hex(diag)
    review = json.loads((candidate_project / "source/ios_net/ios_net.review.json").read_text())
    candidate = candidate_project / "source/ios_net/ios_net.elf"
    if review["activation_allowed"] is not False or hashlib.sha256(candidate.read_bytes()).hexdigest() != review["candidate_sha256"]:
        raise ValueError("candidate review is stale or activation gate changed")
    lines.append("#define CANDIDATE_NET_WORD %su" % review["candidate_hook"]["proposed"])
    lines.append("#define CANDIDATE_STATUS_ADDRESS %su" % review["status_bss"]["address"])
    out = ROOT / out_dir
    out.mkdir(parents=True, exist_ok=True)
    (out / "profile.h").write_text("\n".join(lines) + "\n")
    manifest["activation_allowed"] = False
    (out / "profile.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Read-only Mocha code profile generated; official embedded payloads verified")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--candidate-dir", default="build/mocha-alloc-experiment")
    p.add_argument("--out", default="build/mocha-boot-state")
    a = p.parse_args()
    generate(a.candidate_dir, a.out)
