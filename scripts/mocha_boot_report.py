"""Validate a fresh Boot State report, without any console commands.

This classifies observations only. It never authorizes native activation or
infers a successful IOS reload from a marker, clock tick, or changed text.
"""
import re


def one(lines, prefix):
    hits = [s for s in lines if s.startswith(prefix)]
    if len(hits) != 1:
        raise ValueError('expected one report field: ' + prefix)
    return hits[0]


def classify(data, profile):
    if len(data) > 16384 or b'\0' in data:
        raise ValueError('invalid report size or embedded NUL')
    lines = data.decode('ascii').splitlines()
    invocation = one(lines, 'Boot State invocation ticks=')
    if not re.fullmatch(r'Boot State invocation ticks=[0-9a-f]{16}', invocation):
        raise ValueError('invalid invocation stamp')
    if lines[0] != invocation or lines[-1:] != ['Inspection complete. HOME -> Close to return.']:
        raise ValueError('incomplete or concatenated report')
    hook = one(lines, 'reload hook=')
    match = re.fullmatch(r'reload hook=([0-9a-f]{8}) NET init call=([0-9a-f]{8})', hook)
    if match is None:
        raise ValueError('invalid hook field')
    reload_word, init = (int(s, 16) for s in match.groups())
    # Review manifests and code identity are an observation, not a hardware
    # attestation. Unknown code cannot validate any diagnostic address.
    kernel = one(lines, 'Mocha kernel text: ')
    net = one(lines, 'Mocha NET text: ')
    official = kernel.endswith('official=MATCH allocation_candidate=no') and net.endswith('official=MATCH allocation_candidate=no')
    candidate = kernel.endswith('official=no allocation_candidate=MATCH') and net.endswith('official=no allocation_candidate=MATCH')
    if official and reload_word == int(profile['official']['reload_word'], 16):
        identity = 'official'
    elif candidate and reload_word == int(profile['candidate']['reload_word'], 16):
        identity = 'candidate'
    else:
        identity = 'unverified'
    result = dict(invocation_ticks=invocation.split('=')[1], identity=identity,
                  reload_word=hex(reload_word), net_init_word=hex(init),
                  reload_observation='unverified', allocation_observation='unverified',
                  native_activation_allowed=False)
    diagnostics = [s for s in lines if s.startswith('reload diag=')]
    if len(diagnostics) > 1:
        raise ValueError('duplicate diagnostic state')
    if diagnostics:
        if identity != 'candidate':
            raise ValueError('diagnostic state without verified candidate identity')
        m = re.fullmatch(r'reload diag=([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})', diagnostics[0])
        if not m:
            raise ValueError('malformed diagnostic state')
        words = tuple(int(s, 16) for s in m.groups())
        observations = {
            (0,0,0,0): 'no_retained_entry_evidence',
            (0x524c4445,0,0,0): 'entry_observed_header_not_accepted',
            (0x524c4445,0x48445250,0,0): 'header_accepted_no_patcher_completion',
            (0x524c4445,0x48445250,0x4e455450,0): 'net_patcher_returned',
        }
        result['reload_observation'] = observations.get(words, 'unknown_or_inconsistent_markers')
    allocation = [s for s in lines if s.startswith('NET allocation status: ')]
    if len(allocation) > 1:
        raise ValueError('duplicate allocation state')
    if allocation:
        if identity != 'candidate' or init != 0xeb04c385:
            raise ValueError('allocation status without candidate NET hook')
        m = re.fullmatch(r'NET allocation status: stage=(\d+) init=(-?\d+) page=([0-9a-f]{8}) data=(\d+) alloc=(\d+) free=(\d+)', allocation[0])
        if m is None:
            raise ValueError('invalid allocation status')
        stage, rc, page, data_ok, allocs, frees = m.groups()
        stage, rc, page, data_ok, allocs, frees = int(stage), int(rc), int(page,16), int(data_ok), int(allocs), int(frees)
        success = (stage == 4 and 0 <= rc <= 0x7fffffff and page != 0 and page % 4096 == 0
                   and (data_ok,allocs,frees) == (1,1,1))
        result['allocation_observation'] = 'allocation_data_free_call_returned' if success else 'reported_failure_or_incomplete'
    return result
