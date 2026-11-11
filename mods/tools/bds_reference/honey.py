"""Execute the complete BDS honey-contact movement callback without runtime hooks."""

import argparse
import json
import random
from pathlib import Path

from generate import HEAP, INPUT_SHA256, Machine, bits


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    lines = ['# BDS honey contacts v1', f'# input_sha256 {INPUT_SHA256}',
             f"# function 0x140b58cf0 sha256 {data['functions']['0x140b58cf0']['code_sha256']}",
             '# H position[3] velocity[3] bodyWidth fallDistance contactCount contactPosition[3]*n outputVelocity[3] outputFallDistance',
             '# finite inputs, complete callback, no runtime hooks; ECS eligibility is outside this callback']
    rng = random.Random(0xB58CF0)
    for index in range(640):
        width = (0, 0.6, 1, 2, -0.25)[index % 5]
        origin = (-10000, -1, 0, 1, 10000)[(index // 5) % 5]
        radius = float.fromhex('0x1.0p-1') * width + 0.43125
        x = (0.5, 0.5 + radius, 0.5 + radius + 1e-5, 0.5 - radius - 1e-5, -0.5)[(index // 25) % 5]
        position = (origin + x, (0, 0.9375, 0.93750006, 1.0, -0.1)[index % 5], 0.5)
        velocity = (rng.uniform(-3, 3), (-1, -0.120000005, -0.12, -0.11999999, -0.0, 0.0, 0.1, 3)[index % 8], rng.uniform(-3, 3))
        fall = (-0.0, 0.0, 0.125, 10)[index % 4]
        contacts = [(origin + offset[0], offset[1], offset[2]) for offset in
                    ((0, 0, 0), (1, 0, 0), (0, 1, 0), (0, -1, 1))[:(index // 8) % 5]]
        entry = machine.call(0x140B58CF0, [HEAP, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300])
        machine.qword(HEAP, HEAP + 0x400)
        machine.qword(HEAP + 8, HEAP + 0x400 + 24 * len(contacts))
        machine.put(HEAP + 0x118, [bits(width)])
        machine.put(HEAP + 0x200, [bits(v) for v in position])
        machine.put(HEAP + 0x218, [bits(v) for v in velocity])
        machine.put(HEAP + 0x300, [bits(fall)])
        for i, contact in enumerate(contacts):
            machine.put(HEAP + 0x400 + i * 24, [v & 0xFFFFFFFF for v in contact])
        machine.run(entry, budget=10000)
        values = [*(bits(v) for v in position), *(bits(v) for v in velocity), bits(width), bits(fall), len(contacts),
                  *(v & 0xFFFFFFFF for contact in contacts for v in contact),
                  *machine.get(HEAP + 0x218, 3), *machine.get(HEAP + 0x300, 1)]
        lines.append('H ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
