"""Execute BDS request construction and complete cache refresh with nonempty world responses."""

import argparse
import json
import random
import struct
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from cache_difference import install_storage
from generate import HEAP, INPUT_SHA256, RETURN, Machine, bits


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    install_storage(machine)
    queries = []
    pattern = 0

    def ret(value=None):
        cpu = machine.cpu
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = int.from_bytes(cpu.mem_read(sp, 8), 'little')
        if value is not None:
            cpu.reg_write(x86.UC_X86_REG_RAX, value)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    def service(cpu, pc, size, user):
        if pc == 0x14030D2C0:
            result = cpu.reg_read(x86.UC_X86_REG_RDX)
            machine.qword(result + 8, 1)
            ret(result)
        elif pc == 0x1403554E0:
            ret(HEAP + 0x400)
        elif pc == 0x146251860:
            dest = cpu.reg_read(x86.UC_X86_REG_RCX)
            source = cpu.reg_read(x86.UC_X86_REG_RDX)
            count = cpu.reg_read(x86.UC_X86_REG_R8)
            if count > 4096:
                raise RuntimeError('Unexpected memmove size')
            if count:
                cpu.mem_write(dest, bytes(cpu.mem_read(source, count)))
            ret(dest)
        else:
            vector = cpu.reg_read(x86.UC_X86_REG_RDX)
            region = machine.get(cpu.reg_read(x86.UC_X86_REG_R8), 6)
            ordinal = len(queries)
            queries.append(region)
            count = (pattern // 8 + pattern + ordinal) % 4
            begin = struct.unpack('<Q', cpu.mem_read(vector, 8))[0]
            machine.qword(vector + 8, begin + count * 48)
            for i in range(count):
                address = begin + i * 48
                block = 0x12300000000 + ((ordinal + 1) << 8) + i + 1 if (pattern + i) % 3 else 0
                machine.qword(address, block)
                machine.put(address + 8, region)
                machine.put(address + 32, [(ordinal - 3) & 0xFFFFFFFF, (i * 5 - 7) & 0xFFFFFFFF,
                                          (pattern - 128) & 0xFFFFFFFF])
                cpu.mem_write(address + 44, bytes([int((pattern + ordinal + i) % 3 == 0)]))
            ret()

    for pc in (0x14030D2C0, 0x1403554E0, 0x146251860, RETURN + 0x200):
        if not any(lo <= pc <= hi for lo, hi, _ in machine.cpu.mem_regions()):
            machine.cpu.mem_map(pc & ~4095, 4096)
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, service, begin=pc, end=pc)
    entry = machine.call(0x140355790, [HEAP, HEAP + 0x100, HEAP + 0x200, 0])
    machine.cpu.mem_write(HEAP + 0x400, bytes([0xA5]) * 152)
    machine.run(entry, budget=10000)
    default_region = machine.get(HEAP + 0x418, 6)
    initialized = machine.get(HEAP + 0x400, 38)
    expected = (*default_region, *default_region, *([0] * 26))
    if initialized != expected:
        raise RuntimeError('Unaccounted native request initialization fields')

    def run(box, movement, step, old, old_count, old_flag):
        entry = machine.call(0x140F90EA0,
                             [HEAP, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300,
                              HEAP + 0x400, HEAP + 0x800, HEAP + 0x900, HEAP + 0xC00,
                              0, 0, HEAP + 0xA00, HEAP + 0xB00, 0])
        machine.put(HEAP + 0x100, [bits(v) for v in box])
        machine.put(HEAP + 0x200, [bits(step)])
        machine.qword(HEAP + 0x300, HEAP + 0xD00)
        machine.put(HEAP + 0x418, old)
        machine.put(HEAP + 0x43C, [bits(v) for v in movement])
        machine.cpu.mem_write(HEAP + 0x488, bytes([old_flag]))
        for vector, start, width in ((0xA00, 0x4000, 48), (0xB00, 0x5000, 48),
                                     (0x458, 0x6000, 24), (0x470, 0x7000, 24)):
            count = old_count if vector in (0x458, 0x470) else 0
            machine.qword(HEAP + vector, HEAP + start)
            machine.qword(HEAP + vector + 8, HEAP + start + count * width)
            machine.qword(HEAP + vector + 16, HEAP + start + 128 * width)
        for i in range(old_count):
            machine.put(HEAP + 0x6000 + i * 24, [bits(v) for v in (i, i, i, i + 1, i + 1, i + 1)])
            machine.qword(HEAP + 0x7000 + i * 24, 0x98700000000 + i)
            machine.put(HEAP + 0x7008 + i * 24, [0xFFFFFFFF, i, 100])
        machine.qword(HEAP + 0xC00, HEAP + 0xE00)
        machine.qword(HEAP + 0xEA8, RETURN + 0x200)
        queries.clear()
        machine.run(entry, budget=1000000)
        shape_end = struct.unpack('<Q', machine.cpu.mem_read(HEAP + 0x460, 8))[0]
        count = (shape_end - HEAP - 0x6000) // 24
        block_end = struct.unpack('<Q', machine.cpu.mem_read(HEAP + 0x478, 8))[0]
        if not 0 <= count <= 128 or block_end != HEAP + 0x7000 + count * 24:
            raise RuntimeError('Invalid cache storage result')
        records = []
        for i in range(count):
            block = struct.unpack('<Q', machine.cpu.mem_read(HEAP + 0x7000 + i * 24, 8))[0]
            records.extend([*machine.get(HEAP + 0x6000 + i * 24, 6), block,
                            *machine.get(HEAP + 0x7008 + i * 24, 3)])
        return [*machine.get(HEAP + 0x43C, 3), *machine.get(HEAP + 0x418, 6),
                machine.cpu.mem_read(HEAP + 0x488, 1)[0], len(queries),
                *(v for query in queries for v in query), count, *records]

    lines = ['# BDS nonempty cache refresh v1', f'# input_sha256 {INPUT_SHA256}',
             *(f"# function {hex(a)} sha256 {data['functions'][hex(a)]['code_sha256']}"
               for a in (0x140355790, 0x140F90EA0, 0x140F924E0, 0x140F9DAF0, 0x140F90C90)),
             '# I constructor region[6]',
             '# R pattern step box[6] movement[3] oldRegion[6] oldCount oldFlag capped[3] finalRegion[6] finalFlag queryCount query[6]*n outputCount (box[6] block64 pos[3])*n',
             '# synthetic world response; mechanical sparse storage, vector storage, memset, memmove/free only']
    lines.append('I ' + ' '.join(f'{v:08x}' for v in default_region))
    rng = random.Random(0xF011CA6E)
    for pattern in range(256):
        origin = (0, 0.125, 1000.125, -300000.375)[pattern % 4]
        box = (origin - 0.3, 0, -0.3, origin + 0.3, 1.8, 0.3)
        movement = tuple(rng.uniform(-20, 20) for _ in range(3))
        step = (0, 0.5625, 1)[pattern % 3]
        initial = run(box, movement, step, [0] * 6, 0, 0)
        region = list(struct.unpack('<6f', struct.pack('<6I', *initial[11:17])))
        old = region.copy()
        mode = pattern % 8
        if mode == 0:
            old_bits = default_region
        else:
            if mode == 2:
                old = [v - 1 if i < 3 else v + 1 for i, v in enumerate(region)]
            elif mode in (3, 4, 5):
                axis = mode - 3
                old[axis] += 0.5
                old[axis + 3] += 0.5
            elif mode == 6:
                old = [v + 0.25 if i < 3 else v - 0.25 for i, v in enumerate(region)]
            elif mode == 7:
                old[3] = old[0]
            old_bits = tuple(bits(v) for v in old)
        old_count = 2 if mode else 0
        flag = pattern & 1
        values = [pattern, bits(step), *(bits(v) for v in box), *(bits(v) for v in movement),
                  *old_bits, old_count, flag, *run(box, movement, step, old_bits, old_count, flag)]
        lines.append('R ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
