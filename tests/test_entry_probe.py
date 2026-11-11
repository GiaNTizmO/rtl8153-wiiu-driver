#!/usr/bin/env python3
"""Offline checks for the native entry probe image."""
import json
import struct
import subprocess
import sys
from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    elf = root / "build/native/entry_probe.rel.elf"
    if not elf.exists():
        raise SystemExit("missing entry probe; run scripts/build_native_entry_probe.sh")

    imports = root / "build/native/entry_probe.imports.json"
    imports.write_text("{}\n")
    output = root / "build/native/entry_probe.image.bin"
    cmd = [
        sys.executable,
        str(root / "scripts/relocate_ios_image.py"),
        str(elf),
        str(output),
        "--base",
        "0x12200000",
        "--imports",
        str(imports),
    ]
    subprocess.run(cmd, cwd=root, check=True)
    manifest = json.loads(output.with_suffix(output.suffix + ".json").read_text())
    assert manifest["relocations"] == 0
    assert manifest["imports"] == {}
    assert "rtl_ios_entry_probe" in manifest["exports"]
    entry = manifest["exports"]["rtl_ios_entry_probe"]
    assert manifest["base"] <= entry < manifest["base"] + manifest["bytes"]
    assert entry % 4 == 0

    image = output.read_bytes()
    assert len(image) == manifest["bytes"]
    # The compiler emits a plain ARM function. Keep the check structural: the
    # runtime loader must use the manifest export and never guess an offset.
    assert len(image) >= 8 and struct.unpack_from(">I", image, 0)[0] != 0
    print(f"entry probe: {len(image)} bytes, export 0x{entry:08x}, no imports")


if __name__ == "__main__":
    main()
