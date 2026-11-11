"""Real owner bulk helper: DMA vectors are separate from copied metadata.
Allocator, memcpy, request-free and IOS ioctlv are stubs. No actual USB/DMA.
"""
from test_ios_submit_close_arm import Machine, CTX, BATCH, BUFFER

UHS, REQUEST, META = CTX + 0x74, CTX + 0x28000, CTX + 0x23000


def run(direction, result):
    m = Machine()
    m.put(UHS, 2)
    m.put(UHS + 8, 0x55)
    token = 0xabc123 # Deliberately unmapped and unaligned: opaque, not DMA.
    original = [1 if direction == 2 else 2, 0x1234, BATCH, token, 0xabcdef]
    for i, word in enumerate(original):
        m.put(META + i * 4, word)
    for i, word in enumerate([BUFFER, 512, META, 71]):
        m.put(m.sp + i * 4, word)
    m.allowed = [(REQUEST, REQUEST + 0x1000)]

    def stub(address, args, sp):
        if address == 0x123ba848:
            assert args[0] == UHS
            m.put(args[1], REQUEST)
            m.events.append('allocate')
            return 0
        if address == 0x123e2a24:
            assert args[:3] == [REQUEST + 0x438, META, 20]
            m.uc.mem_write(args[0], bytes(m.uc.mem_read(args[1], args[2])))
            m.events.append('metadata_copy')
            return args[0]
        if address == 0x123e4454:
            assert args[0] == 0x55 and args[1] == 14
            assert [m.get(REQUEST + 0x438 + i * 4) for i in range(5)] == original
            assert m.get(REQUEST + 0x454) == BUFFER
            assert m.get(REQUEST + 0x458) == 512
            # Vectors use actual DMA, independent of the opaque buffer tag.
            assert m.get(sp + 8) == REQUEST + 0x414
            vector = REQUEST + 0xc
            assert m.get(vector) == BUFFER
            assert m.get(vector + 4) == 512
            assert [m.get(sp), m.get(sp + 4)] == [REQUEST, 71]
            m.events.append('ioctlv')
            return result
        if address == 0x123ba754:
            assert args[:2] == [UHS, REQUEST]
            m.events.append('free_failed_submit')
            return 0
        return None

    assert m.run(0x123bc0d4, [UHS, 0x5678, 1, direction], stub) == result & 0xffffffff
    assert m.events == ['allocate', 'metadata_copy', 'ioctlv'] + (
        ['free_failed_submit'] if result < 0 else [])


if __name__ == '__main__':
    for direction in (1, 2):
        for result in (0, -64):
            run(direction, result)
    print('Owner UHS ARM: 4 cases; actual DMA vectors independent of opaque metadata buffer token, copied metadata and failed-submit free verified. Allocator/memcpy/IOS calls stubbed.')
