"""Execute the full BDS cached-region subtraction/merge helper."""

import argparse
import json
import random
import struct
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, Machine, bits


def install_storage(machine):
    def storage(cpu, pc, size, user):
        if pc == 0x140B7C620:
            vector = cpu.reg_read(x86.UC_X86_REG_RCX)
            source = cpu.reg_read(x86.UC_X86_REG_R8)
            begin, end, capacity = struct.unpack('<3Q', cpu.mem_read(vector, 24))
            if begin == 0:
                begin = end = HEAP + 0x2000
                capacity = begin + 32 * 24
                machine.qword(vector, begin)
                machine.qword(vector + 16, capacity)
            if end + 24 > capacity:
                raise RuntimeError('Unexpected cache-difference storage growth')
            cpu.mem_write(end, bytes(cpu.mem_read(source, 24)))
            machine.qword(vector + 8, end + 24)
            result = end
        elif pc == 0x146251880:
            result = cpu.reg_read(x86.UC_X86_REG_RCX)
            count = cpu.reg_read(x86.UC_X86_REG_R8)
            if count > 4096:
                raise RuntimeError('Unexpected cache-difference memset size')
            cpu.mem_write(result, bytes([cpu.reg_read(x86.UC_X86_REG_RDX) & 255]) * count)
        else:
            result = 0
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = int.from_bytes(cpu.mem_read(sp, 8), 'little')
        cpu.reg_write(x86.UC_X86_REG_RAX, result)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    for pc in (0x140B7C620, 0x146250390, 0x146251880):
        if not any(lo <= pc <= hi for lo, hi, _ in machine.cpu.mem_regions()):
            machine.cpu.mem_map(pc & ~4095, 4096)
        machine.cpu.mem_write(pc, b'\xc3')
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, storage, begin=pc, end=pc)


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    install_storage(machine)
    pairs = [((0, 0, 0, 2, 3, 4), (0, 0, 0, 2, 3, 4)),
             ((0, 0, 0, 2, 3, 4), (0.5, 0.5, 0.5, 1.5, 2.5, 3.5)),
             ((0, 0, 0, 2, 3, 4), (-1, -1, -1, 3, 4, 5)),
             ((0, 0, 0, 2, 3, 4), (1, 1, 1, 3, 4, 5)),
             ((0, 0, 0, 2, 3, 4), (3, 0, 0, 4, 3, 4)),
             ((0, 0, 0, 2, 3, 4), (-1, 0, 0, 1, 3, 4)),
             ((0, 0, 0, 2, 3, 4), (0, 2, 0, 2, 4, 4)),
             ((0, 0, 0, 2, 3, 4), (0, 0, 3, 2, 3, 5)),
             ((0, 0, 0, 2, 3, 4), (1.999999, 1, 1, 2, 2, 3)),
             ((0, 0, 0, 2, 3, 4), (1e-6, 1e-6, 1e-6, 2, 3, 4)),
             ((0, 0, 0, 2, 3, 4), (0.5, 0.5, 0.5, 0.500001, 0.500001, 0.500001))]
    inputs = []
    for origin in (0, 0.125, -0.25, 1000.125, -300000.375):
        for box, old in pairs:
            inputs.append((tuple(v + origin for v in box), tuple(v + origin for v in old)))
    inputs.extend([((-0.0, -0.0, -0.0, 2, 3, 4), (0, 0, 0, 1, 2, 3)),
                   ((0, 0, 0, 2, 3, 4), (-0.0, -0.0, -0.0, 1, 2, 3))])
    rng = random.Random(0xD1FFE126)
    for _ in range(512):
        origin = tuple(rng.uniform(-10, 10) for _ in range(3))
        box = (*origin, *(v + rng.uniform(0.001, 5) for v in origin))
        lower = tuple(v + rng.uniform(-1, 4) for v in origin)
        old = (*lower, *(v + rng.uniform(0.000001, 5) for v in lower))
        inputs.append((box, old))
    function = data['functions']['0x140f924e0']
    lines = ['# BDS cached-region subtraction v1', f'# input_sha256 {INPUT_SHA256}',
             f"# function 0x140f924e0 sha256 {function['code_sha256']}",
             '# E query[6] cachedRegion[6] outputCount output[6]*count; mechanical vector storage and memset']
    for box, old in inputs:
        entry = machine.call(0x140F924E0, [HEAP + 0x100, HEAP + 0x200, HEAP + 0x300])
        machine.put(HEAP + 0x200, [bits(v) for v in box])
        machine.put(HEAP + 0x300, [bits(v) for v in old])
        machine.run(entry, budget=1000000)
        begin, end, capacity = struct.unpack('<3Q', machine.cpu.mem_read(HEAP + 0x100, 24))
        count = (end - begin) // 24
        if count < 0 or count > 32:
            raise RuntimeError('Unexpected cache-difference output')
        output = [*(bits(v) for v in (*box, *old)), count]
        if count:
            output.extend(machine.get(begin, count * 6))
        lines.append('E ' + ' '.join(f'{v:08x}' for v in output))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
