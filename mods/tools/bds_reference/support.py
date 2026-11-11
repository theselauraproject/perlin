"""Execute the unmodified BDS cached supporting-block selector, without hooks."""

import argparse
import json
import random
from pathlib import Path

from generate import HEAP, INPUT_SHA256, Machine, bits


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    floor = (-0.5, -1, -0.5, 0.5, 0, 0.5)
    left = (-1, -1, -0.5, 0, 0, 0.5)
    right = (0, -1, -0.5, 1, 0, 0.5)
    high = (-0.5, 0, -0.5, 0.5, 1, 0.5)
    low = (-0.5, -2, -0.5, 0.5, -1, 0.5)
    invalid = (-0.5, -0.4, -0.5, -0.5, 0, 0.5)
    patterns = [[], [(floor, True)], [(floor, False)], [(floor, True), (low, True)],
                [(low, True), (floor, True)], [(left, True), (right, True)],
                [(right, True), (left, True)], [(invalid, True), (floor, True)],
                [(floor, True), (invalid, True)], [(high, True), (floor, True)],
                [(high, True)], [(floor, False), (low, True)],
                [(left, False), (right, True)], [(right, True), (left, False)],
                [((0.2, -0.4, 0.2, 0.4, 0.2, 0.4), True), (floor, True)],
                [((3, -1, 3, 4, 0, 4), True), (low, True)]]
    inputs = []
    for index in range(16):
        origin = (0, 1000.125, -1000.375, 1000000.25)[index // 4]
        y = (0, 0.2, 0.3, -0.3)[index % 4]
        body = (origin - 0.3, y, -0.3, origin + 0.3, y + 1.8, 0.3)
        for pattern in patterns:
            entries = [(tuple(v + origin if i in (0, 3) else v for i, v in enumerate(box)), block)
                       for box, block in pattern]
            inputs.append((body, entries))
    rng = random.Random(0xB10C126)
    for _ in range(1024):
        pos = [rng.uniform(-10000, 10000) for _ in range(3)]
        body = (*pos, *(v + rng.uniform(0, 2) for v in pos))
        entries = []
        for _ in range(rng.randrange(11)):
            lower = [v + rng.uniform(-3, 3) for v in pos]
            entries.append(((*lower, *(v + rng.uniform(-0.25, 2) for v in lower)), bool(rng.randrange(4))))
        inputs.append((body, entries))
    function = data['functions']['0x14478d6b0']
    lines = ['# BDS cached supporting-block selection v1', f'# input_sha256 {INPUT_SHA256}',
             f"# function 0x14478d6b0 sha256 {function['code_sha256']}",
             '# S count body[6] (shape[6] blockId)*count position[3i] blockId adjustedBody[6]']
    for body, entries in inputs:
        entry = machine.call(0x14478D6B0, [HEAP, HEAP + 0x100, HEAP + 0x200])
        machine.qword(HEAP, HEAP + 0x1000)
        machine.qword(HEAP + 8, HEAP + 0x1000 + 24 * len(entries))
        machine.qword(HEAP + 24, HEAP + 0x4000)
        machine.put(HEAP + 0x200, [bits(v) for v in body])
        words = [len(entries), *(bits(v) for v in body)]
        for i, (box, has_block) in enumerate(entries):
            block_id = i + 1 if has_block else 0
            machine.put(HEAP + 0x1000 + 24 * i, [bits(v) for v in box])
            machine.qword(HEAP + 0x4000 + 24 * i, HEAP + 0x9000 + 16 * block_id if block_id else 0)
            words.extend([*(bits(v) for v in box), block_id])
        machine.run(entry, budget=100000)
        pointer = int.from_bytes(machine.cpu.mem_read(HEAP + 0x110, 8), 'little')
        block_id = (pointer - HEAP - 0x9000) // 16 if pointer else 0
        if block_id < 0 or block_id > len(entries):
            raise RuntimeError('Unexpected supporting-block pointer')
        words.extend([*machine.get(HEAP + 0x100, 3), block_id, *machine.get(HEAP + 0x200, 6)])
        lines.append('S ' + ' '.join(f'{v:08x}' for v in words))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
