"""Owner worker dispatch -> relocated RTL token callback, actual ARM code.

Real resolver and compiled bridge execute together. UHS submission, queue
locks, request free and final network delivery are stubs. No console access.
"""
import sys
from pathlib import Path
from test_ios_submit_close_arm import Machine, CTX, CTRL, IFACE, BATCH, BUFFER
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from relocate_ios_image import ArmRelocatable

BASE = 0x22000000
TOKENS, BINDING, WRAPPED, OPS, MSG = (CTX + 0x29000, CTX + 0x2a000,
    CTX + 0x2b000, CTX + 0x2c000, CTX + 0x2d000)
SUBMIT, DELIVERY, CONTROL = 0x1200, 0x1300, 0x1400


def run(kind, result):
    m = Machine()
    elf = Path(__file__).resolve().parents[1] / 'build/native/rtl8153_ios.rel.elf'
    image, manifest = ArmRelocatable(elf.read_bytes()).build(BASE, {})
    m.uc.mem_map(BASE, (len(image) + 4095) & ~4095)
    m.uc.mem_write(BASE, image)
    symbols = manifest['exports']
    # All writable data is synthetic. Guard against writes into firmware or
    # relocated text; only the compiled module's own BSS may be written.
    m.allowed = [(CTX, CTX + 0x30000)] + [
        (BASE + s['offset'], BASE + s['offset'] + s['size'])
        for s in manifest['sections'] if s['flags'] & 1]
    for offset, value in ((0, 1), (4, 0x5678), (8, 0x1234), (12, 42),
                           (16, 71), (24, CONTROL), (28, SUBMIT)):
        m.put(BINDING + offset, value)
    m.uc.mem_write(BINDING + 20, bytes([0x81, 2]))
    m.put(CTX + 0x82ac, IFACE)
    m.put(IFACE + 0x30, CTRL)
    m.put(CTRL, OPS)
    m.put(OPS + 0x34, symbols['rtl_ios_tokens_callback'])
    deliveries = []
    wires = []
    events = []

    def stub(address, args, sp):
        if address == SUBMIT:
            assert args == [1, 0x5678, 1 if kind == 1 else 2, 2 if kind == 1 else 1]
            assert [m.get(sp), m.get(sp + 4), m.get(sp + 12)] == [BUFFER, 512, 71]
            meta = m.get(sp + 8)
            wire = [m.get(meta + i * 4) for i in range(5)]
            assert wire[:3] == [kind, 0x1234, BATCH] and wire[4] == 42
            assert wire[3] != BUFFER and wire[3]
            wires.append(wire)
            return 0
        if address == DELIVERY:
            assert args[0] == 0x9876
            record = [m.get(args[1] + i * 4) for i in range(5)]
            assert record == [kind, 0x1234, BATCH, BUFFER, 42]
            assert args[2:] == [result & 0xffffffff if result < 0 else 0,
                                 0 if result < 0 else result]
            deliveries.append(record)
            events.append('delivery')
            return -7 if result < 0 else 0
        if address == 0x123b4384:
            assert kind == 2 and args == [CTX, 0x1234, 2, 0]
            events.append('acquire');return 0
        if address == 0x123b42b4:
            assert kind == 2 and args[:3] == [CTX, IFACE, 2]
            events.append('release');return 0
        if address == 0x123ba77c:
            assert args[:2] == [CTX + 0x74, CTX + 0x28000]
            events.append('free_request');return 0
        return None

    def call(name, args):
        for i, value in enumerate(args[4:]):
            m.put(m.sp + i * 4, value)
        return m.run(symbols[name], args[:4], stub)

    assert call('rtl_ios_tokens_init', [TOKENS, BINDING, CTX, CTRL,
                                       DELIVERY, 0x9876, WRAPPED]) == 0
    assert call('rtl_ios_tokens_attach', [TOKENS]) == 0
    for round in range(2):
        assert call('rtl_ios_uhs_submit', [WRAPPED, kind == 1, BATCH, BUFFER, 512]) == 0
        token = wires[-1][3]
        m.put(MSG + 4, result)
        for i, word in enumerate(wires[-1]):
            m.put(MSG + 0x24 + i * 4, word)
        m.put(MSG + 0x38, CTX + 0x28000)
        m.put(MSG + 0x3c, 0x5678)
        events.clear()
        # Real owner worker loads the relocated callback via ops+0x34.
        assert m.run(0x123b83b0, [CTX, MSG], stub) == 0
        assert events == (['acquire'] if kind == 2 else []) + ['delivery'] + (
            ['release'] if kind == 2 else []) + ['free_request']
        assert len(deliveries) == round + 1
        # Duplicate is consumed without delivery; negative callback return
        # would make the owner worker free the still-reusable real batch.
        assert call('rtl_ios_tokens_callback', [CTX, CTRL, kind, BATCH, token, result]) == 0
        assert len(deliveries) == round + 1
        events.clear()
        assert m.run(0x123b83b0, [CTX, MSG], stub) == 0
        assert events == (['acquire', 'release'] if kind == 2 else []) + ['free_request']
        assert len(deliveries) == round + 1
        if round:
            assert token != wires[0][3]
    assert call('rtl_ios_tokens_drained', [TOKENS]) == 1
    call('rtl_ios_tokens_close', [TOKENS])
    assert call('rtl_ios_tokens_detach', [TOKENS]) == 0


if __name__ == '__main__':
    for kind in (1, 2):
        for result in (-64, 0, 48, 512):
            run(kind, result)
    print('ARM owner worker + relocated RTL callback: 8 cases/16 transfers; exact six-argument ABI, opaque tokens, restored DMA/cookie, signed errors consumed once, lock/free order and repeated buffer reuse passed. USB/network helpers stubbed.')
