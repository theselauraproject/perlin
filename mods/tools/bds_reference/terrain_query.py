"""Execute BDS terrain-query traversal with empty synthetic block geometry.

Native height clipping, traversal, below-world collision and record writes run
unchanged. Chunk/palette/geometry services are supplied; the separate chunk-wall
prepass is disabled and the world-border tail receives its native skip predicate.
"""

import argparse
import json
import random
import struct
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, RETURN, Machine, bits


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    columns, cells = [], []
    minimum = maximum = mode = 0

    def service(cpu, pc, size, user):
        if pc == RETURN + 0x100:
            result = minimum & 0xFFFF
        elif pc == RETURN + 0x200:
            result = maximum & 0xFFFF
        elif pc == RETURN + 0x300:
            p = cpu.reg_read(x86.UC_X86_REG_RDX)
            x, _, z = struct.unpack('<3i', cpu.mem_read(p, 12))
            columns.append((x, z))
            ready = mode == 0 or (mode == 2 and not (((x >> 4) + (z >> 4)) & 1))
            result = HEAP + 0x1000 if ready else 0
        elif pc == RETURN + 0x400:
            result = HEAP + 0x1800
        elif pc == RETURN + 0x500:
            p = cpu.reg_read(x86.UC_X86_REG_R9)
            cells.append(struct.unpack('<3i', cpu.mem_read(p, 12)))
            result = 0
        elif pc == RETURN + 0x600:
            result = 1
        else:
            raise RuntimeError('Unexpected terrain-query service')
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = int.from_bytes(cpu.mem_read(sp, 8), 'little')
        cpu.reg_write(x86.UC_X86_REG_RAX, result)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    for offset in range(0x100, 0x700, 0x100):
        pc = RETURN + offset
        machine.cpu.mem_write(pc, b'\xc3')
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, service, begin=pc, end=pc)
    inputs = []
    for minimum_id, maximum_id in ((-64, 320), (0, 128), (-16, 16), (0, 0),
                                  (-32768, -32752), (32751, 32767)):
        for y in (minimum_id - 42, minimum_id - 1, minimum_id, minimum_id + 0.125,
                  maximum_id - 1, maximum_id, maximum_id + 2):
            for mode_id in range(3):
                inputs.append((minimum_id, maximum_id, mode_id, (-0.3, y, -0.3, 0.3, y + 1.8, 0.3)))
    rng = random.Random(0xCE11126)
    for _ in range(256):
        minimum_id, maximum_id = rng.choice(((-64, 320), (0, 128), (-16, 16)))
        x, z = (rng.choice((0, 16, -16, 1000000)) + rng.uniform(-1, 1) for _ in range(2))
        y = rng.uniform(minimum_id - 45, maximum_id + 5)
        inputs.append((minimum_id, maximum_id, rng.randrange(3),
                       (x, y, z, x + rng.uniform(-0.25, 2), y + rng.uniform(-0.25, 3), z + rng.uniform(-0.25, 2))))
    function = data['functions']['0x143e9fff0']
    lines = ['# BDS terrain traversal v1', f'# input_sha256 {INPUT_SHA256}',
             f"# function 0x143e9fff0 sha256 {function['code_sha256']}",
             '# T minimum maximum mode bounds[6] columnCount (x z)*n cellCount (x y z)*n boundaryCount box[6]*n']
    for minimum, maximum, mode, bounds in inputs:
        entry = machine.call(0x143E9FFF0, [HEAP + 0x100, HEAP + 0x700, HEAP + 0x500, 0,
                                         HEAP + 0x900, HEAP + 0x800])
        machine.qword(HEAP + 0x100, HEAP + 0x300)
        machine.cpu.mem_write(HEAP + 0x100 + 58, struct.pack('<h', minimum))
        for slot, offset in ((272, 0x100), (280, 0x200), (312, 0x300)):
            machine.qword(HEAP + 0x300 + slot, RETURN + offset)
        machine.qword(HEAP + 0x700, HEAP + 0x600)
        for vector, start, capacity in ((0x600, 0x4000, 16 * 48), (0x800, 0x5000, 4 * 24)):
            for offset, value in ((0, HEAP + start), (8, HEAP + start), (16, HEAP + start + capacity)):
                machine.qword(HEAP + vector + offset, value)
        machine.qword(HEAP + 0x900, HEAP + 0xA00)
        machine.qword(HEAP + 0xA00 + 64, RETURN + 0x600)
        sections = (maximum - minimum) // 16 + 2
        machine.qword(HEAP + 0x1000 + 328, HEAP + 0x2000)
        machine.qword(HEAP + 0x1000 + 336, HEAP + 0x2000 + sections * 88)
        for i in range(sections):
            machine.qword(HEAP + 0x2000 + i * 88 + 48, HEAP + 0x1400)
        machine.qword(HEAP + 0x1400, HEAP + 0x1500)
        machine.qword(HEAP + 0x1500 + 24, RETURN + 0x400)
        machine.qword(HEAP + 0x1800 + 88, HEAP + 0x1900)
        machine.qword(HEAP + 0x1900, HEAP + 0x1A00)
        machine.qword(HEAP + 0x1A00 + 56, RETURN + 0x500)
        machine.put(HEAP + 0x500, [bits(v) for v in bounds])
        columns.clear()
        cells.clear()
        machine.run(entry, budget=1000000)
        output = [minimum & 0xFFFFFFFF, maximum & 0xFFFFFFFF, mode, *(bits(v) for v in bounds), len(columns)]
        output.extend(v & 0xFFFFFFFF for pair in columns for v in pair)
        output.append(len(cells))
        output.extend(v & 0xFFFFFFFF for pos in cells for v in pos)
        end = int.from_bytes(machine.cpu.mem_read(HEAP + 0x608, 8), 'little')
        count = (end - HEAP - 0x4000) // 48
        output.append(count)
        for i in range(count):
            record = HEAP + 0x4000 + i * 48
            if any(machine.cpu.mem_read(record, 8)) or any(machine.cpu.mem_read(record + 32, 13)):
                raise RuntimeError('Unexpected below-world metadata')
            output.extend(machine.get(record + 8, 6))
        lines.append('T ' + ' '.join(f'{v:08x}' for v in output))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
