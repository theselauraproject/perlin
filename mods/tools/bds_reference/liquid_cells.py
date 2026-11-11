"""Execute the complete BDS liquid collector with a supplied deterministic world."""

import argparse
import json
import struct
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, RETURN, INPUT_SHA256, Machine, bits


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    queries = []
    seed = 0

    def query(cpu, pc, size, user):
        position = struct.unpack('<iii', cpu.mem_read(cpu.reg_read(x86.UC_X86_REG_RDX), 12))
        queries.append(position)
        x, y, z = position
        material = (0, 5, 6, 25)[(x * 3 + y * 5 + z * 7 + seed) & 3]
        machine.put(HEAP + 0x1800, [material])
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        cpu.reg_write(x86.UC_X86_REG_RAX, HEAP + 0x1000)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, int.from_bytes(cpu.mem_read(sp, 8), 'little'))

    machine.cpu.mem_write(RETURN + 0x100, b'\xc3')
    machine.cpu.hook_add(unicorn.UC_HOOK_CODE, query, begin=RETURN + 0x100, end=RETURN + 0x100)
    lines = ['# BDS common liquid collector v1', f'# input_sha256 {INPUT_SHA256}',
             *[f"# function {hex(pc)} sha256 {data['functions'][hex(pc)]['code_sha256']}"
               for pc in (0x140FC94C0, 0x140FC8FA0, 0x1447976A0)],
             '# Q bounds[6] material retain seed found queryCount queryPosition[3]*n recordCount recordPosition[3],faceMask*n',
             '# supplied world material = [0,5,6,25][(x*3+y*5+z*7+seed)&3]; no secondary bodies',
             '# Complete native collector, bounds conversion, vector writes and early-exit logic; only block fetch is synthetic.']
    for number in range(512):
        origin = (-33554432, -16777216, -10000, -2, -1.0000001, -0.99999994, -0.0, 0.0,
                  0.99999994, 1, 1.0000001, 2, 10000, 16777216, 33554428, 33554430)[number % 16]
        extents = (0, 0.001, 0.1, 0.4, 0.6, 0.99999994, 1.4, 2)
        bounds = (origin, -1.0, -0.0, origin + extents[(number // 16) % 8],
            -1.0 + extents[(number // 64) % 8], extents[(number // 16) % 8])
        material = (0, 5, 6, 25)[number % 4]
        retain = (number // 4) % 2
        seed = number // 8 % 16
        entry = machine.call(0x140FC94C0, [HEAP + 0x800, material, HEAP + 0x100,
            HEAP + 0x200, HEAP + 0x300 if retain else 0])
        machine.put(HEAP + 0x100, [bits(v) for v in bounds])
        machine.qword(HEAP + 0x300, HEAP + 0x4000)
        machine.qword(HEAP + 0x308, HEAP + 0x4000)
        machine.qword(HEAP + 0x310, HEAP + 0x8000)
        machine.qword(HEAP + 0x800, HEAP + 0x900)
        machine.qword(HEAP + 0x928, RETURN + 0x100)
        machine.qword(HEAP + 0x1058, HEAP + 0x1400)
        machine.qword(HEAP + 0x1540, HEAP + 0x1800)
        queries.clear()
        machine.run(entry, budget=100000)
        found = machine.cpu.reg_read(x86.UC_X86_REG_RAX) & 0xFF
        end = int.from_bytes(machine.cpu.mem_read(HEAP + 0x308, 8), 'little')
        if not HEAP + 0x4000 <= end <= HEAP + 0x8000 or (end - HEAP - 0x4000) % 24:
            raise RuntimeError('Invalid native record vector')
        count = (end - HEAP - 0x4000) // 24
        records = []
        for index in range(count):
            address = HEAP + 0x4000 + index * 24
            if int.from_bytes(machine.cpu.mem_read(address, 8), 'little') != HEAP + 0x1000:
                raise RuntimeError('Unexpected native block reference')
            records.extend((*machine.get(address + 8, 3), machine.cpu.mem_read(address + 20, 1)[0]))
        values = [*(bits(v) for v in bounds), material, retain, seed, found, len(queries),
            *(v & 0xFFFFFFFF for position in queries for v in position), count, *records]
        lines.append('Q ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
