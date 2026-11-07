"""Execute the BDS common block collision collector with supplied geometry/offsets."""

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
    supplied_offset, supplied_fallback = (), ()

    def service(cpu, pc, size, user):
        out = cpu.reg_read(x86.UC_X86_REG_RDX)
        machine.put(out, supplied_offset if pc == 0x143EECEF0 else supplied_fallback)
        cpu.reg_write(x86.UC_X86_REG_RAX, out)
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = int.from_bytes(cpu.mem_read(sp, 8), 'little')
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    if not any(lo <= 0x143EECEF0 <= hi for lo, hi, _ in machine.cpu.mem_regions()):
        machine.cpu.mem_map(0x143EEC000, 4096)
    for pc in (0x143EECEF0, RETURN + 0x200):
        machine.cpu.mem_write(pc, b'\xc3')
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, service, begin=pc, end=pc)
    inputs = []
    for mode in range(3):
        for count in (0, 1, 8, 9, 16, 32):
            for query_mode in range(3):
                local = [(i / 64, 0, 0, i / 64 + 1 / 128, 1, 1) for i in range(count)]
                inputs.append((mode, (-0.125, 0.25, 1.125), (1000, -16, -1000),
                               (1000, -16, -1000, 1001, -15, -999),
                               None if query_mode == 0 else
                               ((998, -18, -1002, 1002, -14, -996) if query_mode == 1 else
                                (1001, -16, -1000, 1002, -15, -999)), local))
    rng = random.Random(0xB10CC011)
    for _ in range(256):
        pos = tuple(rng.randrange(-1000000, 1000000) for _ in range(3))
        offset = tuple(rng.uniform(-1.25, 1.25) for _ in range(3))
        lower = tuple(v + rng.uniform(-2, 2) for v in pos)
        fallback = (*lower, *(v + rng.uniform(-0.25, 2) for v in lower))
        lower = tuple(v + rng.uniform(-2, 2) for v in pos)
        query = None if rng.randrange(3) == 0 else (*lower, *(v + rng.uniform(0, 4) for v in lower))
        local = []
        for _ in range(rng.randrange(33)):
            lower = tuple(rng.uniform(-0.5, 1.5) for _ in range(3))
            local.append((*lower, *(v + rng.uniform(-0.25, 1.5) for v in lower)))
        inputs.append((rng.randrange(3), offset, pos, fallback, query, local))
    function = data['functions']['0x143f386b0']
    lines = ['# BDS common block collision collector v1', f'# input_sha256 {INPUT_SHA256}',
             f"# function 0x143f386b0 sha256 {function['code_sha256']}",
             '# G mode localCount hasQuery offset[3] blockPosition[3i] fallback[6] query[6] local[6]*n return count output[6]*n']
    for mode, offset, pos, fallback, query, local in inputs:
        entry = machine.call(0x143F386B0, [HEAP, HEAP + 0x100, 0, HEAP + 0x200,
                                         HEAP + 0x300 if query else 0, HEAP + 0x400, 0])
        machine.qword(HEAP, HEAP + 0x600)
        machine.qword(HEAP + 0x600 + 40, RETURN + 0x200)
        machine.qword(HEAP + 0x100 + 112, HEAP + 0x500 if mode else 0)
        machine.cpu.mem_write(HEAP + 0x500, bytes([int(mode == 2)]))
        machine.qword(HEAP + 0x500 + 32, HEAP + 0x1000)
        machine.qword(HEAP + 0x500 + 40, HEAP + 0x1000 + len(local) * 24)
        machine.put(HEAP + 0x200, [v & 0xFFFFFFFF for v in pos])
        if query:
            machine.put(HEAP + 0x300, [bits(v) for v in query])
        for relative, value in ((0, HEAP + 0x4000), (8, HEAP + 0x4000), (16, HEAP + 0x4600)):
            machine.qword(HEAP + 0x400 + relative, value)
        for i, box in enumerate(local):
            machine.put(HEAP + 0x1000 + i * 24, [bits(v) for v in box])
        supplied_offset = tuple(bits(v) for v in offset)
        supplied_fallback = tuple(bits(v) for v in fallback)
        machine.run(entry, budget=100000)
        end = int.from_bytes(machine.cpu.mem_read(HEAP + 0x408, 8), 'little')
        count = (end - HEAP - 0x4000) // 24
        returned = machine.cpu.reg_read(x86.UC_X86_REG_RAX) & 0xFF
        values = [mode, len(local), int(query is not None), *supplied_offset,
                  *(v & 0xFFFFFFFF for v in pos), *supplied_fallback,
                  *(bits(v) for v in (query or (0,) * 6)), *(bits(v) for box in local for v in box), returned, count]
        values.extend(machine.get(HEAP + 0x4000, count * 6))
        lines.append('G ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
