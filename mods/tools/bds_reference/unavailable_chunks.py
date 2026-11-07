"""Execute BDS unavailable-chunk collision collection with synthetic chunk states."""

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
    visited = []
    mode = 0

    def get_chunk(cpu, pc, size, user):
        address = cpu.reg_read(x86.UC_X86_REG_RDX)
        x, z = struct.unpack('<2i', cpu.mem_read(address, 8))
        visited.append((x, z))
        state = mode if mode != 6 else (x + 2 * z) & 7
        if state == 2:
            state = 1 if (x + z) & 1 else 0
        chunk = HEAP + 0x9000
        machine.qword(chunk + 120, 0x8000000080000000 if state == 3 else 0)
        cpu.mem_write(chunk + 130, bytes([int(state == 4)]))
        cpu.mem_write(chunk + 5352, bytes([int(state == 5)]))
        cpu.reg_write(x86.UC_X86_REG_RAX, 0 if state == 1 else chunk)
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = int.from_bytes(cpu.mem_read(sp, 8), 'little')
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    machine.cpu.mem_write(RETURN + 0x100, b'\xc3')
    machine.cpu.hook_add(unicorn.UC_HOOK_CODE, get_chunk, begin=RETURN + 0x100, end=RETURN + 0x100)
    inputs = []
    for mode_id in range(7):
        for origin in (-32.001, -16, -8.001, -8, -0.001, 0, 7.999, 8, 15.999, 16, 1000000.125):
            for width in (0, 0.6, 16, 32):
                inputs.append((mode_id, (origin, -0.25, origin - 0.125, origin + width, 1.8, origin + width)))
    rng = random.Random(0xC8A126)
    for _ in range(256):
        position = tuple(rng.uniform(-10000, 10000) for _ in range(3))
        inputs.append((rng.randrange(7), (*position, *(v + rng.uniform(-1, 48) for v in position))))
    function = data['functions']['0x143ea1990']
    lines = ['# BDS unavailable-chunk barriers v1', f'# input_sha256 {INPUT_SHA256}',
             f"# function 0x143ea1990 sha256 {function['code_sha256']}",
             '# U mode bounds[6] visitedCount (cx cz)*count barrierCount (box[6] preserve)*count']
    for mode, bounds in inputs:
        entry = machine.call(0x143EA1990, [HEAP + 0x100, HEAP + 0x300, HEAP + 0x500])
        machine.qword(HEAP + 0x100, HEAP + 0x200)
        machine.qword(HEAP + 0x200 + 320, RETURN + 0x100)
        machine.qword(HEAP + 0x300, HEAP + 0x400)
        for offset, pointer in ((0, HEAP + 0x1000), (8, HEAP + 0x1000), (16, HEAP + 0x7000)):
            machine.qword(HEAP + 0x400 + offset, pointer)
        machine.put(HEAP + 0x500, [bits(v) for v in bounds])
        visited.clear()
        machine.run(entry, budget=100000)
        end = int.from_bytes(machine.cpu.mem_read(HEAP + 0x408, 8), 'little')
        count = (end - HEAP - 0x1000) // 48
        output = [mode, *(bits(v) for v in bounds), len(visited)]
        output.extend(v & 0xFFFFFFFF for pair in visited for v in pair)
        output.append(count)
        for i in range(count):
            record = HEAP + 0x1000 + i * 48
            if any(machine.cpu.mem_read(record, 8)) or any(machine.cpu.mem_read(record + 32, 12)):
                raise RuntimeError('Unexpected unavailable-chunk block metadata')
            output.extend([*machine.get(record + 8, 6), machine.cpu.mem_read(record + 44, 1)[0]])
        lines.append('U ' + ' '.join(f'{v:08x}' for v in output))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
