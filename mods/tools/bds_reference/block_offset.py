"""Execute BDS position seeding and complete random block offset generation."""

import argparse
import json
import random
import struct
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, Machine, bits


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    converted = []

    def construct(cpu, pc, size, user):
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        pointers = [cpu.reg_read(x86.UC_X86_REG_R8), cpu.reg_read(x86.UC_X86_REG_R9)]
        pointers.extend(struct.unpack('<4Q', cpu.mem_read(sp + 40, 32)))
        words = []
        for axis in range(3):
            words.extend(machine.get(pointers[2 * axis], 2))
            words.extend(machine.get(pointers[2 * axis + 1], 1))
        converted.append(words)
        target = int.from_bytes(cpu.mem_read(sp, 8), 'little')
        cpu.reg_write(x86.UC_X86_REG_RAX, cpu.reg_read(x86.UC_X86_REG_RCX))
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    pc = 0x140709510
    if not any(lo <= pc <= hi for lo, hi, _ in machine.cpu.mem_regions()):
        machine.cpu.mem_map(pc & ~4095, 4096)
    machine.cpu.mem_write(pc, b'\xc3')
    machine.cpu.hook_add(unicorn.UC_HOOK_CODE, construct, begin=pc, end=pc)
    rng = random.Random(0x0FF5E7126)
    positions = [(0, 0, 0), (1, 1, 1), (-1, -1, -1), (16, 0, -16), (16, 123, -16),
                 (0x7FFFFFFF, 0, -0x80000000), (-0x80000000, 0x7FFFFFFF, 0x7FFFFFFF),
                 (33554432, -64, -33554432), (-300000, 320, 300000)]
    positions.extend(tuple(rng.randrange(-0x80000000, 0x80000000) for _ in range(3)) for _ in range(128))
    profiles = [((0, 0, 0),) * 3,
                ((-0.25, 0.25, 0), (-0.5, 0, 0), (-0.25, 0.25, 0)),
                ((-0.25, 0.25, 1), (0.5, -0.5, 0), (1, 2, 1)),
                ((-0.25, 0.25, 2), (-0.5, 0, 3), (-0.25, 0.25, 4)),
                ((-1.25, 1.25, 31), (-1, 3, 255), (0, 1, 65535)),
                ((-1, 1, 0x80000000), (-1, 1, 0xFFFFFFFE), (-1, 1, 0xFFFFFFFF)),
                ((0.125, 0.125, 0), (0, -0.0, 1), (-0.0, -0.0, 2)),
                ((-1e-37, 1e-37, 0), (-1e-37, 1e-37, 1), (-1e-37, 1e-37, 9)),
                ((-3.4028234663852886e38, 3.4028234663852886e38, 0),
                 (-3.4028234663852886e38, 3.4028234663852886e38, 1),
                 (-3.4028234663852886e38, 3.4028234663852886e38, 2))]
    lines = ['# BDS position-seeded random offsets v1', f'# input_sha256 {INPUT_SHA256}',
             '# R position[3i] seedState[4u32]; O position[3i] (minimum maximum steps)*3 offset[3]',
             '# D writable description[9] component[9 if writable]; initialized synthetic TLS and mechanical constructor']
    for address in (0x14048C340, 0x1441FC5A0, 0x1441FC8B0):
        lines.append(f"# function {address:#x} sha256 {data['functions'][hex(address)]['code_sha256']}")
    for pos in positions:
        entry = machine.call(0x14048C340, [HEAP, HEAP + 0x100])
        machine.put(HEAP + 0x100, [v & 0xFFFFFFFF for v in pos])
        machine.run(entry, budget=10000)
        values = [*(v & 0xFFFFFFFF for v in pos), *machine.get(HEAP + 16, 4)]
        lines.append('R ' + ' '.join(f'{v:08x}' for v in values))
        for profile in profiles:
            entry = machine.call(0x1441FC5A0, [HEAP, HEAP + 0x100, HEAP + 0x200])
            config = [v for low, high, steps in profile for v in (bits(low), bits(high), steps)]
            machine.put(HEAP, config)
            machine.put(HEAP + 0x200, [v & 0xFFFFFFFF for v in pos])
            machine.run(entry, budget=10000)
            values = [*(v & 0xFFFFFFFF for v in pos), *config, *machine.get(HEAP + 0x100, 3)]
            lines.append('O ' + ' '.join(f'{v:08x}' for v in values))
    for profile in profiles:
        config = [v for low, high, steps in profile for v in (bits(low), bits(high), steps)]
        for enabled in (0, 1):
            entry = machine.call(0x1441FC8B0, [HEAP + 0x100, HEAP + 0x400])
            machine.cpu.reg_write(x86.UC_X86_REG_GS_BASE, HEAP + 0xF000)
            machine.qword(HEAP + 0xF058, HEAP + 0xE000)
            machine.qword(HEAP + 0xE000, HEAP + 0xD000)
            machine.put(HEAP + 0xD024, [0x7FFFFFFF])
            machine.put(HEAP + 0x110, config)
            machine.cpu.mem_write(HEAP + 0x448, bytes([enabled]))
            converted.clear()
            machine.run(entry, budget=10000)
            if len(converted) != enabled:
                raise RuntimeError('Unexpected random-offset component construction')
            values = [enabled, *config, *(converted[0] if enabled else [])]
            lines.append('D ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
