"""Execute the unmodified BDS collision-cache converter with preallocated vectors."""

import argparse
import json
import random
import struct
from pathlib import Path

from generate import HEAP, INPUT_SHA256, Machine, bits


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    rng = random.Random(0xCA4E126)
    function = data['functions']['0x140f90c90']
    lines = ['# BDS collision-cache append v1', f'# input_sha256 {INPUT_SHA256}',
             f"# function 0x140f90c90 sha256 {function['code_sha256']}",
             '# K oldCount newCount oldPreserve region[6] (box[6] blockId pos[3i] preserve)*n outputPreserve (box[6] blockId pos[3i])*n']
    for index in range(512):
        old_count = index % 4
        new_count = (index // 4) % 11
        preserve = index & 1
        records = []
        for i in range(old_count + new_count):
            box = tuple(rng.uniform(-10000, 10000) for _ in range(6))
            block_id = i + 1 if rng.randrange(3) else 0
            position = tuple(rng.randrange(-1000, 1000) for _ in range(3))
            flag = bool(rng.randrange(2))
            records.append((box, block_id, position, flag))
        region = tuple(rng.uniform(-10000, 10000) for _ in range(6))
        entry = machine.call(0x140F90C90, [HEAP + 0x100, HEAP + 0x200, HEAP + 0x400])
        for offset, pointer in ((0, HEAP + 0x1000), (8, HEAP + 0x1000 + 48 * new_count),
                                (16, HEAP + 0x1000 + 48 * new_count)):
            machine.qword(HEAP + 0x100 + offset, pointer)
        for offset, start in ((88, 0x4000), (112, 0x6000)):
            machine.qword(HEAP + 0x400 + offset, HEAP + start)
            machine.qword(HEAP + 0x400 + offset + 8, HEAP + start + old_count * 24)
            machine.qword(HEAP + 0x400 + offset + 16, HEAP + start + 32 * 24)
        machine.cpu.mem_write(HEAP + 0x400 + 136, bytes([preserve]))
        machine.put(HEAP + 0x200, [bits(v) for v in region])
        values = [old_count, new_count, preserve, *(bits(v) for v in region)]
        for i, (box, block_id, position, flag) in enumerate(records):
            pointer = HEAP + 0xA000 + block_id * 8 if block_id else 0
            if i < old_count:
                machine.put(HEAP + 0x4000 + i * 24, [bits(v) for v in box])
                machine.qword(HEAP + 0x6000 + i * 24, pointer)
                machine.put(HEAP + 0x6000 + i * 24 + 8, [v & 0xFFFFFFFF for v in position])
            else:
                record = HEAP + 0x1000 + (i - old_count) * 48
                machine.qword(record, pointer)
                machine.put(record + 8, [bits(v) for v in box])
                machine.put(record + 32, [v & 0xFFFFFFFF for v in position])
                machine.cpu.mem_write(record + 44, bytes([int(flag)]))
            values.extend([*(bits(v) for v in box), block_id, *(v & 0xFFFFFFFF for v in position), int(flag)])
        machine.run(entry, budget=100000)
        if machine.get(HEAP + 0x418, 6) != tuple(bits(v) for v in region):
            raise RuntimeError('Cache region was not copied exactly')
        for offset, start in ((96, 0x4000), (120, 0x6000)):
            end = struct.unpack('<Q', machine.cpu.mem_read(HEAP + 0x400 + offset, 8))[0]
            if end != HEAP + start + 24 * len(records):
                raise RuntimeError('Unexpected appended cache length')
        values.append(machine.cpu.mem_read(HEAP + 0x488, 1)[0])
        for i in range(len(records)):
            pointer = struct.unpack('<Q', machine.cpu.mem_read(HEAP + 0x6000 + i * 24, 8))[0]
            block_id = (pointer - HEAP - 0xA000) // 8 if pointer else 0
            values.extend([*machine.get(HEAP + 0x4000 + i * 24, 6), block_id,
                           *machine.get(HEAP + 0x6000 + i * 24 + 8, 3)])
        lines.append('K ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
