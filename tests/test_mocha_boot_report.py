"""Offline validation of captured/proposed report observations."""
import importlib.util
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('report', ROOT/'scripts/mocha_boot_report.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
PROFILE={'official':{'reload_word':'0xeb003005'},'candidate':{'reload_word':'0xeb0030b0'}}


def report(diag='00000000 00000000 00000000 00000000', allocation=''):
    return ('Boot State invocation ticks=0000000123456789\n'
            'Read-only boot state; no reload, writes, native entry or USB access.\n'
            'reload hook=eb0030b0 NET init call=ebffffe2\n'
            'Mocha kernel text: official=no allocation_candidate=MATCH\n'
            'Mocha NET text: official=no allocation_candidate=MATCH\n'
            'reload diag='+diag+'\n'+allocation+
            'Inspection complete. HOME -> Close to return.\n').encode()


class Report(unittest.TestCase):
    def test_marker_outcomes_are_distinct(self):
        for words,expected in (
            ('00000000 00000000 00000000 00000000','no_retained_entry_evidence'),
            ('524c4445 00000000 00000000 00000000','entry_observed_header_not_accepted'),
            ('524c4445 48445250 00000000 00000000','header_accepted_no_patcher_completion'),
            ('524c4445 48445250 4e455450 00000000','net_patcher_returned'),
            ('00000000 48445250 4e455450 00000000','unknown_or_inconsistent_markers')):
            with self.subTest(words=words):
                result=r.classify(report(words),PROFILE)
                self.assertEqual(result['reload_observation'],expected)
                self.assertFalse(result['native_activation_allowed'])
    def test_old_hardware_report_is_rejected(self):
        old=ROOT/'evidence/2026-11-09/mocha-allocation-test/boot-state-after-reboot.log'
        with self.assertRaises(ValueError):r.classify(old.read_bytes(),PROFILE)
    def test_truncated_and_duplicate_reports(self):
        data=report()
        for bad in (data[:-50],data+data,data.replace(b'ticks=0000000123456789',b'ticks=bad'),data+b'\0'):
            with self.subTest(bad=bad[:60]),self.assertRaises(ValueError):r.classify(bad,PROFILE)
    def test_unverified_identity_rejects_diagnostics(self):
        with self.assertRaises(ValueError):r.classify(report().replace(b'allocation_candidate=MATCH',b'allocation_candidate=no'),PROFILE)
    def test_allocator_requires_hook(self):
        a='NET allocation status: stage=4 init=0 page=18001000 data=1 alloc=1 free=1\n'
        with self.assertRaises(ValueError):r.classify(report(allocation=a),PROFILE)
        data=report(allocation=a).replace(b'NET init call=ebffffe2',b'NET init call=eb04c385')
        result=r.classify(data,PROFILE)
        self.assertEqual(result['allocation_observation'],'allocation_data_free_call_returned')
        self.assertFalse(result['native_activation_allowed'])
        for old,new in ((b'free=1',b'free=0'),(b'page=18001000',b'page=18001004'),(b'init=0 page',b'init=-1 page')):
            self.assertEqual(r.classify(data.replace(old,new),PROFILE)['allocation_observation'],'reported_failure_or_incomplete')
    def test_official_identity(self):
        data=report().replace(b'reload hook=eb0030b0',b'reload hook=eb003005')
        data=data.replace(b'official=no allocation_candidate=MATCH',b'official=MATCH allocation_candidate=no')
        data=data.replace(b'reload diag=00000000 00000000 00000000 00000000\n',b'')
        self.assertEqual(r.classify(data,PROFILE)['identity'],'official')


if __name__=='__main__':unittest.main()
