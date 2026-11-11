"""FTP deployment failure-path tests; no console or network operations."""
import ftplib
import importlib.util
import tempfile
import unittest
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
spec=importlib.util.spec_from_file_location('deploy',ROOT/'scripts/mocha_diagnostic_deploy.py')
d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)


class FTP:
    def __init__(self, files, overwrite=True, corrupt_stage=False):
        self.files=dict(files);self.overwrite=overwrite;self.corrupt_stage=corrupt_stage
        self.mutations=[]
    def nlst(self, directory):
        return [n for n in self.files if n.startswith(directory) and '/' not in n[len(directory):]]
    def retrbinary(self, command, callback):
        callback(self.files[command.removeprefix('RETR ')])
    def storbinary(self, command, source):
        name=command.removeprefix('STOR ');self.mutations.append(('store',name))
        self.files[name]=source.read()+(b'bad' if self.corrupt_stage else b'')
    def rename(self, source, dest):
        if dest in self.files and not self.overwrite:raise ftplib.error_perm('550 exists')
        self.mutations.append(('rename',source,dest))
        self.files[dest]=self.files.pop(source)


class Deploy(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.out=Path(self.tmp.name)
    def test_replacement_verified(self):
        f=FTP({d.BOOT:b'old'})
        d.replace_verified(f,d.BOOT,b'new',d.sha(b'old'),self.out,'boot')
        self.assertEqual(f.files[d.BOOT],b'new')
        self.assertEqual((self.out/'boot-before.bin').read_bytes(),b'old')
        self.assertEqual(f.mutations[0][1],d.BOOT+'.rtl-stage-disabled')
        self.assertEqual(f.mutations[1],('rename',d.BOOT+'.rtl-stage-disabled',d.BOOT))
    def test_no_overwrite_leaves_boot_present(self):
        f=FTP({d.BOOT:b'old'},overwrite=False)
        with self.assertRaises(ftplib.error_perm):
            d.replace_verified(f,d.BOOT,b'new',d.sha(b'old'),self.out,'boot')
        self.assertEqual(f.files[d.BOOT],b'old')
        self.assertTrue(all(n==d.BOOT or not n.lower().endswith('.rpx') for n in f.files))
    def test_corrupt_upload_never_replaces(self):
        f=FTP({d.BOOT:b'old'},corrupt_stage=True)
        with self.assertRaises(ValueError):
            d.replace_verified(f,d.BOOT,b'new',d.sha(b'old'),self.out,'boot')
        self.assertEqual(f.files[d.BOOT],b'old')
        self.assertEqual(len(f.mutations),1)
    def test_unexpected_boot_no_writes(self):
        f=FTP({d.BOOT:b'unknown'})
        with self.assertRaises(ValueError):
            d.replace_verified(f,d.BOOT,b'new',d.sha(b'old'),self.out,'boot')
        self.assertEqual(f.mutations,[])
    def test_staging_collision_preserved(self):
        f=FTP({d.BOOT:b'old',d.BOOT+'.rtl-stage-disabled':b'prior'})
        with self.assertRaises(ValueError):
            d.replace_verified(f,d.BOOT,b'new',d.sha(b'old'),self.out,'boot')
        self.assertEqual(f.mutations,[])
    def test_unknown_extra_mocha_preserved(self):
        f=FTP({d.BOOT:b'old',d.SETUP+'other_mocha.rpx':b'unknown'})
        with self.assertRaises(ValueError):d.disable_old_experiment(f,self.out)
        self.assertEqual(f.mutations,[])
    def test_old_experiment_hash_required(self):
        f=FTP({d.BOOT:b'old',d.SETUP+'00_mocha.experiment-run.rpx':b'unknown'})
        with self.assertRaises(ValueError):d.disable_old_experiment(f,self.out)
        self.assertEqual(f.mutations,[])
    def test_report_is_archived_not_reused(self):
        f=FTP({d.REPORT:b'previous'})
        d.archive_report(f,self.out)
        self.assertNotIn(d.REPORT,f.files)
        self.assertEqual((self.out/'previous-report.log').read_bytes(),b'previous')
    def test_fetch_requires_archival_evidence(self):
        archive=self.out/'archive';archive.mkdir()
        (archive/'operations.jsonl').write_text('{"event":"finished"}\n')
        f=FTP({d.REPORT:b'old'})
        with self.assertRaises(ValueError):d.fetch_report(f,self.out,archive,{})
        self.assertEqual(f.mutations,[])
    def test_identical_report_cannot_be_reused(self):
        archive=self.out/'archive';archive.mkdir()
        (archive/'operations.jsonl').write_text('{"event":"report-archived"}\n')
        (archive/'previous-report.log').write_bytes(b'old report')
        f=FTP({d.REPORT:b'old report'})
        with self.assertRaises(ValueError):d.fetch_report(f,self.out,archive,{})
        self.assertFalse((self.out/'observation.json').exists())
        self.assertEqual(f.mutations,[])


if __name__=='__main__':unittest.main()
