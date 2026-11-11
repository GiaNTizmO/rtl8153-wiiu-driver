"""Execute pinned owner RX/TX submit and ASIX close instructions offline.

UHS, timer, chip-specific disable and final drain are intercepted. This proves
the caller-side ABI/order, never actual cancellation, USB DMA or quiescence.
"""
import struct
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
    UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10,
    UC_ARM_REG_R11, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)
from test_ios_completion_arm import IMAGE, MAPPING

CTX, CTRL, IFACE, BATCH, BUFFER = (0x20000000, 0x20020000, 0x20021000,
                                   0x20022000, 0x20024000)
STACK, STOP = 0x21000000, 0x1000
REGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
SAVED = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
         UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)


class Machine:
    def __init__(self):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_BIG_ENDIAN)
        s = next(s for s in MAPPING['segments'] if s['type'] == 1 and s['vaddr'] == 0x12300000)
        self.uc.mem_map(s['vaddr'], (s['filesz'] + 4095) & ~4095)
        self.uc.mem_write(s['vaddr'], IMAGE[s['offset']:s['offset'] + s['filesz']])
        self.uc.mem_map(CTX, 0x30000)
        self.uc.mem_map(STACK, 0x10000)
        self.uc.mem_map(STOP, 0x1000)
        self.sp = STACK + 0xffe0
        self.events = []
        self.allowed = []
        self.returned = False
        self.put(CTRL + 4, IFACE)
        self.put(IFACE, 0x1234)
        self.put(CTRL + 0x18, 0x5678)
        self.put(CTX + 0x192d4, 71)
        self.put(CTRL + 0x1a4, 7)
        self.put(CTRL + 0x1a8, 7)
        self.put(CTRL + 0x1ac, 7)
        self.put(BATCH + 0x444, BUFFER)
        self.put(BATCH + 0x44, BUFFER)
        self.put(BATCH + 0x40, 512)
        self.uc.mem_write(CTRL + 0x18a, bytes([0x81]))
        self.uc.mem_write(CTRL + 0x193, bytes([2]))
        self.uc.mem_write(CTRL + 0x19c, bytes([0x83]))

    def put(self, address, value):
        self.uc.mem_write(address, struct.pack('>I', value & 0xffffffff))

    def get(self, address):
        return struct.unpack('>I', self.uc.mem_read(address, 4))[0]

    def run(self, entry, args, stub):
        uc = self.uc
        self.returned = False
        uc.reg_write(UC_ARM_REG_SP, self.sp)
        uc.reg_write(UC_ARM_REG_LR, STOP)
        for reg, value in zip(REGS, args):
            uc.reg_write(reg, value)
        saved = {r: 0xbb001000 + i for i, r in enumerate(SAVED)}
        for reg, value in saved.items():
            uc.reg_write(reg, value)

        def write(uc, access, address, size, value, data):
            assert (STACK <= address and address + size <= STACK + 0x10000) or any(
                lo <= address and address + size <= hi for lo, hi in self.allowed), hex(address)

        def code(uc, address, size, data):
            if address == STOP:
                self.returned = True
                uc.emu_stop()
                return
            values = [uc.reg_read(r) for r in REGS]
            ret = stub(address, values, uc.reg_read(UC_ARM_REG_SP))
            if ret is None:
                return
            uc.reg_write(UC_ARM_REG_R0, ret & 0xffffffff)
            for r in REGS[1:]:
                uc.reg_write(r, 0xdeadbeef)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

        hooks = [uc.hook_add(UC_HOOK_MEM_WRITE, write), uc.hook_add(UC_HOOK_CODE, code)]
        try:
            uc.emu_start(entry, 0, count=5000)
        finally:
            for hook in hooks:
                uc.hook_del(hook)
        assert self.returned and uc.reg_read(UC_ARM_REG_SP) == self.sp
        for reg, value in saved.items():
            assert uc.reg_read(reg) == value
        return uc.reg_read(UC_ARM_REG_R0)


def submit(kind, result):
    m = Machine()
    offset = 0x1a8 if kind == 1 else 0x1ac
    m.allowed = [(CTRL + offset, CTRL + offset + 4)]

    def stub(address, args, sp):
        if address == 0x123bc0d4:
            assert args == [CTX + 0x74, 0x5678, 1 if kind == 1 else 2, 2 if kind == 1 else 1]
            assert [m.get(sp), m.get(sp + 4), m.get(sp + 12)] == [BUFFER, 0x4000 if kind == 1 else 512, 71]
            metadata = m.get(sp + 8)
            assert [m.get(metadata + i * 4) for i in range(5)] == [kind, 0x1234, BATCH, BUFFER, 0]
            assert m.get(CTRL + offset) == 7
            m.events.append('submit')
            return result
        if address == 0x123b6f28:
            assert args == [CTX, CTRL, kind, 1]
            m.events.append('increment')
        if address == 0x123ccb50:
            m.events.append('log')
            return 0
        return None

    rc = m.run(0x123ba6b0 if kind == 1 else 0x123b9730, [CTX, CTRL, BATCH], stub)
    assert rc == result & 0xffffffff
    assert m.events == (['submit', 'increment'] if result >= 0 else
                        ['submit'] if kind == 1 else ['submit', 'log'])
    for off in (0x1a4, 0x1a8, 0x1ac):
        assert m.get(CTRL + off) == (8 if off == offset and result >= 0 else 7)


def close(flags, timer, admin_result):
    m = Machine()
    m.put(CTRL + 0x52c, timer)
    m.put(CTRL + 0x1b0, 1)
    m.allowed = [(CTRL + 0x52c, CTRL + 0x530), (CTRL + 0x1b0, CTRL + 0x1b4)]

    def stub(address, args, sp):
        if address == 0x123b6fdc:
            assert args[:2] == [CTX, timer]
            m.events.append('stop_timer')
            return 0
        if address == 0x123baee4:
            # Native admin opcode 2, not the PPC client's enum value 3.
            assert args == [CTX + 0x74, 0x5678, 2, (1 << 17) | (1 << 2) | (1 << 19)]
            assert [m.get(sp), m.get(sp + 4)] == [0, 0]
            m.events.append('admin_cancel')
            return admin_result
        if address == 0x123b99a4:
            assert args[:2] == [CTX, CTRL]
            m.events.append('asix_disable')
            return 0
        if address == 0x123b936c:
            assert args[:2] == [CTX, CTRL]
            assert m.get(CTRL + 0x1b0) == 0
            # Close requests drain but does not itself retire outstanding USB.
            assert [m.get(CTRL + o) for o in (0x1a4, 0x1a8, 0x1ac)] == [7, 7, 7]
            m.events.append('drain')
            return 0
        return None

    rc = m.run(0x123b9e54, [CTX, CTRL, flags], stub)
    assert rc == 0 # admin error is not propagated by this ASIX close.
    assert m.events == (['stop_timer'] if timer else []) + (
        [] if flags & 1 else ['admin_cancel', 'asix_disable']) + ['drain']
    assert m.get(CTRL + 0x52c) == 0


if __name__ == '__main__':
    for kind in (1, 2):
        for result in (-64, -1, 0, 512):
            submit(kind, result)
    for flags in (0, 1):
        for timer in (0, 0x9876):
            for result in (0, -64):
                close(flags, timer, result)
    print('Owner ARM: 8 RX/TX submit and 8 close cases; real pending increment, copied metadata, cancel mask/opcode, flag branch, unchanged pending and error masking verified. UHS/timer/chip-disable/drain stubbed.')
