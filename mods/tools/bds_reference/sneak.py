"""Execute BDS sneak eligibility and full cached-shape edge clipping without hooks."""

import argparse
import json
import random
from pathlib import Path

from generate import HEAP, INPUT_SHA256, Machine, bits


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    rng = random.Random(0x5AEA126)
    lines = ['# BDS sneak movement v1', f'# input_sha256 {INPUT_SHA256}',
             *(f"# function {hex(a)} sha256 {data['functions'][hex(a)]['code_sha256']}"
               for a in (0x140E76AD0, 0x140E76C70)),
             '# S flags controlMembership groundMembership controlByte oldRequest85 step body[6] movement[3] velocity[3] shapeCount shape[6]*n outputRequest85 outputMovement[3] outputVelocity[3]',
             '# membership 0 absent,1 valid,2 stale-generation; no runtime hooks']
    edge = [0.0, -0.0, 1e-8, -1e-8, 1.1920928955078125e-7, -1.1920928955078125e-7,
            0.049999997, 0.05, 0.050000004, -0.05, -0.050000004, 0.15, -0.15, 0.4, -0.4]
    for index in range(1024):
        flags = index & 255
        control, ground = (index // 3) % 3, index % 3
        control_byte = (index // 9) & 1
        old = index & 1
        step = (0.0, 0.25, 0.5625, 1.0, -0.25)[index % 5]
        origin = (0.0, 0.125, -0.125, 1000.125, -300000.375)[index % 5]
        width = (0.6, 0.01, 0.05, 0.04, -0.1)[(index // 5) % 5]
        body = (origin - width / 2, 1, -width / 2, origin + width / 2, 2.8, width / 2)
        movement = (edge[index % len(edge)], 0.2, edge[(index // len(edge)) % len(edge)])
        if index >= 512:
            flags = 2 if index & 1 else 0x42
            control, ground = 0, 1
            width = 0.6
            body = (origin - 0.3, 1, -0.3, origin + 0.3, 2.8, 0.3)
            movement = (rng.uniform(-1.5, 1.5), rng.uniform(-0.5, 0.5), rng.uniform(-1.5, 1.5))
        velocity = (rng.uniform(-2, 2), rng.uniform(-2, 2), rng.uniform(-2, 2)) if index & 2 else (0.0, -0.0, 0.0)
        shapes = []
        if index % 7:
            shapes.append((origin - 1, 0, -1, origin + 0.15, 1, 0.15))
        if index % 7 > 2:
            shapes.append((origin + 0.2, 0, -1, origin + 1, 1, 0.1))
        if index % 7 > 4:
            shapes.append((origin - 1, 0, 0.2, origin + 0.1, 1, 1))
        entry = machine.call(0x140E76C70, [HEAP, HEAP + 0x100, HEAP + 0x200,
                                        HEAP + 0x300, HEAP + 0x400, HEAP + 0x500, HEAP + 0x700])
        machine.cpu.mem_write(HEAP, bytes([flags]))
        machine.put(HEAP + 0x100, [bits(v) for v in body])
        machine.put(HEAP + 0x200, [bits(step)])
        machine.put(HEAP + 0x53C, [bits(v) for v in movement])
        machine.put(HEAP + 0x718, [bits(v) for v in velocity])
        machine.cpu.mem_write(HEAP + 0x555, bytes([old]))
        machine.qword(HEAP + 0x558, HEAP + 0x5000)
        machine.qword(HEAP + 0x560, HEAP + 0x5000 + len(shapes) * 24)
        for i, box in enumerate(shapes):
            machine.put(HEAP + 0x5000 + i * 24, [bits(v) for v in box])
        for view, pool, present in ((0x300, 0x2000, control), (0x400, 0x3000, ground)):
            machine.qword(HEAP + view, HEAP + pool)
            machine.put(HEAP + view + 8, [0])
            machine.qword(HEAP + pool + 8, HEAP + pool + 0x200)
            machine.qword(HEAP + pool + 16, HEAP + pool + 0x208)
            machine.qword(HEAP + pool + 0x200, HEAP + pool + 0x400)
            machine.put(HEAP + pool + 0x400, [0 if present == 1 else 0x40000 if present == 2 else 0xFFFFFFFF])
            machine.qword(HEAP + pool + 80, HEAP + pool + 0x600)
            machine.qword(HEAP + pool + 0x600, HEAP + pool + 0x800)
        machine.cpu.mem_write(HEAP + 0x2800, bytes([control_byte]))
        machine.run(entry, budget=1000000)
        values = [flags, control, ground, control_byte, old, bits(step), *(bits(v) for v in body),
                  *(bits(v) for v in movement), *(bits(v) for v in velocity), len(shapes),
                  *(bits(v) for shape in shapes for v in shape), machine.cpu.mem_read(HEAP + 0x555, 1)[0],
                  *machine.get(HEAP + 0x53C, 3), *machine.get(HEAP + 0x718, 3)]
        lines.append('S ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
