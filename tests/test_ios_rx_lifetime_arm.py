"""Execute owner RX allocation and dispatch-to-input, with external helpers stubbed.
This checks allocation geometry and ownership handoff, not USB/cache hardware.
"""
from test_ios_submit_close_arm import Machine, CTX, CTRL, IFACE, BATCH

OPS, RECEIVER, INPUT = CTX + 0x26000, 0x1200, 0x1300


def allocate(result, base):
    m = Machine()
    m.put(CTRL, OPS)
    m.put(OPS + 0x14, 0x4000)
    m.put(OPS + 0x18, 0)
    m.put(OPS + 0x30, RECEIVER)
    m.put(CTRL + 0x1a8, 0)
    m.put(CTRL + 0x1b0, 1)
    m.put(IFACE + 0x14, 0)
    m.allowed = [(base, base + 0x4467)]
    image_before = bytes(m.uc.mem_read(base, 0x4467))
    events = []

    def stub(address, args, sp):
        if address == 0x123e438c:
            assert args[:2] == [0xcaff, 0x4467]
            events.append('allocate')
            return base
        if address == 0x123e2ac0:
            assert args[:3] == [base, 0, 0x448]
            m.uc.mem_write(base, bytes(0x448))
            return base
        if address == RECEIVER:
            assert args[:3] == [CTX, CTRL, base]
            dma = m.get(base + 0x444)
            assert dma == (base + 0x467) & ~31
            assert dma % 32 == 0 and dma + 0x4000 <= base + 0x4467
            assert m.get(base + 0x440) == 0x1234
            assert bytes(m.uc.mem_read(base + 0x448, 0x4467 - 0x448)) == image_before[0x448:]
            events.append('receive')
            return result
        if address == 0x123e439c:
            assert args[:2] == [0xcaff, base]
            events.append('free_failed_submit')
            return 0
        if address == 0x123ccb50:
            return 0
        return None

    rc = m.run(0x123b75ac, [CTX, CTRL], stub)
    assert (rc == result & 0xffffffff)
    assert events == ['allocate', 'receive'] + (['free_failed_submit'] if result < 0 else [])
    # Actual DMA may be 32 mod 64; requiring 64 from this allocator is invalid.
    return m.get(base + 0x444) % 64


def input_delivery(count):
    m = Machine()
    m.put(CTX, 17)
    m.put(IFACE, 23)
    m.put(IFACE + 0xd0, INPUT)
    m.put(BATCH + 0x38, count)
    m.put(BATCH + 0x3c, 0)
    m.put(BATCH + 0x40, BATCH + 0x460)
    m.put(BATCH + 0x44, 14)
    m.allowed = [(IFACE + 0x14, IFACE + 0x18), (IFACE + 0x68, IFACE + 0x80)]
    events = []

    def stub(address, args, sp):
        if address == INPUT:
            assert args[:3] == [17, 23, BATCH]
            # Native input count is incremented only after this callback.
            assert m.get(IFACE + 0x14) == 0
            events.append('input')
            return 0
        return None

    m.run(0x123b3f30, [CTX, IFACE, BATCH], stub)
    assert events == ['input'] and m.get(IFACE + 0x14) == 1
    assert m.get(IFACE + 0x70) == 1
    assert m.get(IFACE + 0x68) == count


def release_batch():
    m = Machine()
    ident = 0x100 # Low-byte slot 0; deliberately differs from USB handle.
    m.put(CTX + 0x82ac, IFACE)
    m.put(IFACE, ident)
    m.put(IFACE + 0x14, 1)
    m.put(BATCH + 8, ident)
    m.allowed = [(IFACE + 0x14, IFACE + 0x18)]
    events = []

    def stub(address, args, sp):
        if address == 0x123b8288:
            assert args[:3] == [CTX, IFACE, 0]
            assert m.get(IFACE + 0x14) == 0
            events.append('reconsider_rx')
            return 0
        if address == 0x123e439c:
            assert args[:2] == [0xcaff, BATCH]
            events.append('free_original_batch')
            return 0
        return None

    m.run(0x123b4430, [CTX, BATCH], stub)
    assert events == ['reconsider_rx', 'free_original_batch']
    assert m.get(IFACE + 0x14) == 0


if __name__ == '__main__':
    alignments = set()
    for base in (BATCH, BATCH + 0x20):
        for result in (0, -64):
            alignments.add(allocate(result, base))
    assert alignments == {0, 32}
    for count in (0, 1):
        input_delivery(count)
    release_batch()
    print('Owner RX lifetime ARM: 4 allocation/submit, 2 input-handoff and 1 native-release cases; caller-owned inline buffer, 32-byte alignment, failed-submit free, native input retention/decrement and original batch free verified. Heap/receiver/free/input/refill stubbed.')
