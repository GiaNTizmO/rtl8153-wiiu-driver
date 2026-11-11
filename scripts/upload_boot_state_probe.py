"""Upload only the read-only WUHB after owner-confirmed idle Menu.
FTP control .144, active data accepted from .13; boot/modules are never written.
"""
import ftplib
import hashlib
import io
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = "16921b4059bdc434b481c16ef8380e16c89ff8b2ad78da62d19a2d372d0bc529"


def main():
    out = ROOT / "evidence/2026-11-09/mocha-boot-state"
    out.mkdir(parents=True, exist_ok=True)
    app = ROOT / "dist/mocha-boot-state/wiiu/apps/rtl8153_mocha_boot_state.wuhb"
    remote = "/fs/vol/external01/wiiu/apps/rtl8153_mocha_boot_state.wuhb"
    with ftplib.FTP(timeout=12, source_address=("192.168.31.14", 0)) as ftp:
        ftp.connect("192.168.31.144", 21)
        ftp.login()
        ftp.set_pasv(False)
        boot = io.BytesIO()
        ftp.retrbinary("RETR /fs/vol/external01/wiiu/environments/aroma/modules/setup/00_mocha.rpx", boot.write)
        if hashlib.sha256(boot.getvalue()).hexdigest() != EXPECTED:
            raise ValueError("console Mocha changed; review before uploading probe")
        (out / "00_mocha-before.rpx").write_bytes(boot.getvalue())
        data = app.read_bytes()
        ftp.storbinary("STOR " + remote, io.BytesIO(data))
        check = io.BytesIO()
        ftp.retrbinary("RETR " + remote, check.write)
        if check.getvalue() != data:
            raise ValueError("uploaded WUHB readback mismatch")
        (out / "probe-readback.wuhb").write_bytes(check.getvalue())
        report = dict(remote=remote, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                      exact_readback=True, boot_sha256=EXPECTED, boot_written=False)
        (out / "upload.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
