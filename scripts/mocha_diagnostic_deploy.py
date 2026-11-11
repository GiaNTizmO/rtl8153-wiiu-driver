"""Verified FTP operations for the explicitly approved reload diagnostic test.

No reboot, IOSU command or memory write. Default action verifies local files
only. Console actions require --menu-confirmed, which records the human's
current Menu confirmation; it must never be inferred from network reachability.
"""
import argparse
import ftplib
import hashlib
import io
import json
from pathlib import Path
from datetime import datetime, timezone
from mocha_boot_report import classify

ROOT = Path(__file__).resolve().parents[1]
SETUP = '/fs/vol/external01/wiiu/environments/aroma/modules/setup/'
BOOT = SETUP + '00_mocha.rpx'
APP = '/fs/vol/external01/wiiu/apps/rtl8153_mocha_boot_state.wuhb'
REPORT = '/fs/vol/external01/wiiu/rtl8153/mocha-boot-state.log'
OFFICIAL = '16921b4059bdc434b481c16ef8380e16c89ff8b2ad78da62d19a2d372d0bc529'
OLD_EXPERIMENT = '95ff0d1d26c8928f77df878725569d2601a1c9631cd10ff4c839b10f71f8e919'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read(ftp, remote):
    b = io.BytesIO()
    ftp.retrbinary('RETR ' + remote, b.write)
    return b.getvalue()


def names(ftp, directory):
    # Plain basenames, no reliance on FTP current working directory.
    result = [p.rstrip('/').rsplit('/', 1)[-1] for p in ftp.nlst(directory)]
    if any(n in ('', '.', '..') or '\\' in n for n in result):
        raise ValueError('unexpected FTP directory listing')
    return result


def bundle(folder):
    m = json.loads((folder / 'manifest.json').read_text())
    if m['activation_allowed'] is not False:
        raise ValueError('unexpected review gate')
    for n, info in m['files'].items():
        p = (folder / n).resolve()
        if not p.is_relative_to(folder.resolve()):
            raise ValueError('manifest path outside bundle')
        data = p.read_bytes()
        if len(data) != info['bytes'] or sha(data) != info['sha256']:
            raise ValueError('bundle hash mismatch: ' + n)
    official = (folder / 'restore/00_mocha.rpx').read_bytes()
    if sha(official) != OFFICIAL:
        raise ValueError('unverified original')
    return official, (folder / 'experiment/00_mocha.rpx').read_bytes(), (folder / 'rtl8153_mocha_boot_state.wuhb').read_bytes()


def journal(folder, event, **fields):
    with (folder / 'operations.jsonl').open('a', encoding='utf-8') as f:
        f.write(json.dumps(dict(utc=datetime.now(timezone.utc).isoformat(), event=event, **fields)) + '\n')


def disable_old_experiment(ftp, evidence):
    listed = names(ftp, SETUP)
    known = '00_mocha.experiment-run.rpx'
    # Do not modify an unrelated setup module merely because it ends in RPX.
    unexpected = [n for n in listed if n.lower().endswith('.rpx') and
                  'mocha' in n.lower() and n not in ('00_mocha.rpx', known)]
    if unexpected:
        raise ValueError('unreviewed Mocha RPX files: ' + repr(unexpected))
    if known in listed:
        data = read(ftp, SETUP + known)
        if sha(data) != OLD_EXPERIMENT:
            raise ValueError('unknown retained experiment; no rename')
        (evidence / 'retained-experiment.rpx').write_bytes(data)
        target = SETUP + known + '.disabled'
        if known + '.disabled' in listed:
            raise ValueError('disabled destination already exists; preserve both')
        journal(evidence, 'disable-intent', source=SETUP+known, destination=target)
        ftp.rename(SETUP+known, target)
        if read(ftp, target) != data or known in names(ftp, SETUP):
            raise ValueError('disable verification failed')
        journal(evidence, 'disabled', sha256=sha(data))


def replace_verified(ftp, destination, data, expected_current, evidence, label):
    current = read(ftp, destination)
    if sha(current) != expected_current:
        raise ValueError('current destination changed: ' + destination)
    (evidence / (label + '-before.bin')).write_bytes(current)
    staged = destination + '.rtl-stage-disabled'
    if staged.rsplit('/', 1)[-1] in names(ftp, destination.rsplit('/', 1)[0]+'/'):
        raise ValueError('staging file already exists; inspect prior operation')
    ftp.storbinary('STOR ' + staged, io.BytesIO(data))
    if read(ftp, staged) != data:
        raise ValueError('staged readback mismatch')
    if read(ftp, destination) != current:
        raise ValueError('destination changed during staging')
    journal(evidence, 'replace-intent', destination=destination, sha256=sha(data))
    # Rename over the current file, never rename away/delete the boot file.
    # Unsupported overwrite stops here with current boot file still present.
    ftp.rename(staged, destination)
    if read(ftp, destination) != data:
        raise ValueError('active readback mismatch: ' + destination)
    journal(evidence, 'replaced', destination=destination, sha256=sha(data))


def archive_report(ftp, evidence):
    directory = REPORT.rsplit('/', 1)[0]+'/'
    if REPORT.rsplit('/', 1)[-1] not in names(ftp, directory):
        journal(evidence, 'report-already-absent')
        return
    data = read(ftp, REPORT)
    (evidence / 'previous-report.log').write_bytes(data)
    remote = REPORT + '.' + evidence.name + '.archived'
    if remote.rsplit('/', 1)[-1] in names(ftp, directory):
        raise ValueError('report archive collision')
    ftp.rename(REPORT, remote)
    if REPORT.rsplit('/', 1)[-1] in names(ftp, directory):
        raise ValueError('previous report still present')
    journal(evidence, 'report-archived', remote=remote, sha256=sha(data))


def fetch_report(ftp, evidence, archive, profile):
    events=[json.loads(s) for s in (archive/'operations.jsonl').read_text().splitlines()]
    if not any(e['event'] in ('report-archived','report-already-absent') for e in events):
        raise ValueError('report archival prerequisite missing')
    data=read(ftp,REPORT)
    (evidence/'boot-state.log').write_bytes(data)
    previous=archive/'previous-report.log'
    if previous.exists() and data==previous.read_bytes():
        raise ValueError('report identical to archived input; fresh run not proven')
    observation=classify(data,profile)
    # This check covers report provenance only; fresh owner-confirmed execution
    # and idle Menu remain required outside this parser.
    observation.update(report_sha256=sha(data),archive_evidence=str(archive.resolve()))
    (evidence/'observation.json').write_text(json.dumps(observation,indent=2)+'\n')
    journal(evidence,'report-validated',**observation)
    print(json.dumps(observation,indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['verify-local', 'inspect', 'install', 'restore', 'archive-report', 'fetch-report'], default='verify-local', nargs='?')
    p.add_argument('--menu-confirmed', action='store_true')
    p.add_argument('--host', choices=['192.168.31.144', '192.168.31.13'],
                   default='192.168.31.144', help='known owner console interface')
    p.add_argument('--bundle', type=Path, default=ROOT/'dist/mocha-allocation-review')
    p.add_argument('--after-archive',type=Path)
    a = p.parse_args()
    official, candidate, app = bundle(a.bundle)
    if a.action == 'verify-local':
        print(json.dumps(dict(official=sha(official), candidate=sha(candidate), app=sha(app), console_contact=False), indent=2))
        return
    if not a.menu_confirmed:
        p.error('current human Menu confirmation is required; no console contact')
    if a.action=='fetch-report' and a.after_archive is None:
        p.error('fetch-report needs --after-archive evidence directory')
    evidence = ROOT/'evidence/mocha-diagnostic'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    evidence.mkdir(parents=True)
    with ftplib.FTP(timeout=15, source_address=('192.168.31.14', 0)) as ftp:
        ftp.connect(a.host, 21)
        ftp.login()
        ftp.set_pasv(False)
        boot = read(ftp, BOOT)
        (evidence/'boot-before.rpx').write_bytes(boot)
        (evidence/'setup-before.json').write_text(json.dumps(names(ftp, SETUP), indent=2))
        if a.action in ('install', 'restore'):
            expected = OFFICIAL if a.action == 'install' else sha(candidate)
            if sha(boot) != expected:
                raise ValueError('unexpected active Mocha; stop before writes')
            disable_old_experiment(ftp, evidence)
            if a.action == 'install':
                current_app = read(ftp, APP)
                replace_verified(ftp, APP, app, sha(current_app), evidence, 'app')
                archive_report(ftp, evidence)
            replace_verified(ftp, BOOT, candidate if a.action == 'install' else official,
                             expected, evidence, 'boot')
        elif a.action == 'archive-report':
            archive_report(ftp, evidence)
        elif a.action == 'fetch-report':
            profile=json.loads((a.bundle/'boot-state-profile.json').read_text())
            fetch_report(ftp,evidence,a.after_archive,profile)
        journal(evidence, 'finished', action=a.action, host=a.host, active_sha256=sha(read(ftp, BOOT)), setup=names(ftp, SETUP))
    print('Evidence: ' + str(evidence))


if __name__ == '__main__':
    main()
