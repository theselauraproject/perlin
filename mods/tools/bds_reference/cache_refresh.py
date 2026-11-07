"""Execute complete BDS MoveCollisionSystem cache refresh against an empty world."""

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
    queried = []

    def world_query(cpu, pc, size, user):
        region = cpu.reg_read(x86.UC_X86_REG_R8)
        queried.append(machine.get(region, 6))
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = int.from_bytes(cpu.mem_read(sp, 8), 'little')
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    machine.cpu.mem_write(RETURN + 0x200, b'\xc3')
    machine.cpu.hook_add(unicorn.UC_HOOK_CODE, world_query, begin=RETURN + 0x200, end=RETURN + 0x200)
    old_boxes = [0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3]

    def run(box, movement, step, old_region, old_flag):
        entry = machine.call(0x140F90EA0,
                             [HEAP, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300,
                              HEAP + 0x400, HEAP + 0x800, HEAP + 0x900, HEAP + 0xC00,
                              0, 0, HEAP + 0xA00, HEAP + 0xB00, 0])
        machine.put(HEAP + 0x100, [bits(v) for v in box])
        machine.put(HEAP + 0x200, [bits(step)])
        machine.qword(HEAP + 0x300, HEAP + 0xD00)
        machine.put(HEAP + 0x418, [bits(v) for v in old_region])
        machine.put(HEAP + 0x43C, [bits(v) for v in movement])
        machine.cpu.mem_write(HEAP + 0x488, bytes([old_flag]))
        for vector, start, width in ((0xA00, 0x4000, 48), (0xB00, 0x5000, 48),
                                     (0x458, 0x6000, 24), (0x470, 0x7000, 24)):
            count = 2 if vector in (0x458, 0x470) else 0
            machine.qword(HEAP + vector, HEAP + start)
            machine.qword(HEAP + vector + 8, HEAP + start + count * width)
            machine.qword(HEAP + vector + 16, HEAP + start + 32 * width)
        machine.put(HEAP + 0x6000, [bits(v) for v in old_boxes])
        machine.qword(HEAP + 0x7000, 0x123456789)
        machine.qword(HEAP + 0x7018, 0x987654321)
        machine.qword(HEAP + 0xC00, HEAP + 0xE00)
        machine.qword(HEAP + 0xEA8, RETURN + 0x200)
        queried.clear()
        machine.run(entry, budget=1000000)
        if machine.get(HEAP + 0x6000, 12) != tuple(bits(v) for v in old_boxes):
            raise RuntimeError('Existing cache shapes changed in empty world')
        for vector, start in ((0x458, 0x6000), (0x470, 0x7000)):
            end = struct.unpack('<Q', machine.cpu.mem_read(HEAP + vector + 8, 8))[0]
            if end != HEAP + start + 48:
                raise RuntimeError('Existing cache entry count changed')
        return (machine.get(HEAP + 0x43C, 3), machine.get(HEAP + 0x418, 6),
                machine.cpu.mem_read(HEAP + 0x488, 1)[0], list(queried))

    rng = random.Random(0xCA6E126)
    function = data['functions']['0x140f90ea0']
    lines = ['# BDS complete cache refresh v1', f'# input_sha256 {INPUT_SHA256}',
             f"# function 0x140f90ea0 sha256 {function['code_sha256']}",
             '# Q step box[6] movement[3] oldRegion[6] oldFlag capped[3] finalRegion[6] finalFlag queries query[6]*n',
             '# empty synthetic world; actual region subtraction and cache conversion; mechanical storage/memset']
    for index in range(512):
        origin = (0, 1000.125, -300000.375)[index % 3]
        box = (origin - 0.3, 0, -0.3, origin + 0.3, 1.8, 0.3)
        movement = tuple(rng.uniform(-20, 20) for _ in range(3))
        step = (0, 0.5625, 1)[index % 3]
        _, _, _, initial = run(box, movement, step, (0,) * 6, 0)
        if len(initial) != 1:
            raise RuntimeError('Initial BDS query did not produce one region')
        region = list(struct.unpack('<6f', struct.pack('<6I', *initial[0])))
        old = region.copy()
        mode = index % 10
        if mode == 0:
            old = [0] * 6
        elif mode == 2:
            old = [v - 1 if i < 3 else v + 1 for i, v in enumerate(region)]
        elif mode in (3, 4, 5):
            axis = mode - 3
            old[axis] += 0.5
            old[axis + 3] += 0.5
        elif mode == 6:
            old = [v + 0.25 if i < 3 else v - 0.25 for i, v in enumerate(region)]
        elif mode == 7:
            distance = region[3] - region[0] + 1
            old[0] += distance
            old[3] += distance
        elif mode == 8:
            old[3] = old[0]
        elif mode == 9:
            old[0] = region[3] - 1e-6
        flag = index & 1
        capped, final, final_flag, queries = run(box, movement, step, old, flag)
        values = [bits(step), *(bits(v) for v in box), *(bits(v) for v in movement),
                  *(bits(v) for v in old), flag, *capped, *final, final_flag, len(queries)]
        values.extend(v for query in queries for v in query)
        lines.append('Q ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
