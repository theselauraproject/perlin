"""Execute BDS entity-inside cell traversal and region readiness."""

import argparse
import json
import math
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
    regions, reads, visits, chunks = [], [], [], []
    ready = mode = 0

    def service(cpu, pc, size, user):
        p = cpu.reg_read(x86.UC_X86_REG_RDX)
        if pc == RETURN + 0x100:
            regions.append(machine.get(p, 6))
            if cpu.reg_read(x86.UC_X86_REG_R8) != 0:
                raise RuntimeError('Inside region query did not use native false readiness flag')
            result = ready
        elif pc == RETURN + 0x200:
            reads.append(struct.unpack('<3i', cpu.mem_read(p, 12)))
            result = HEAP + 0x5000 + (len(reads) % 8) * 8
        elif pc == RETURN + 0x300:
            position = cpu.reg_read(x86.UC_X86_REG_R8)
            visits.append((p, *struct.unpack('<3i', cpu.mem_read(position, 12))))
            result = 0
        elif pc == RETURN + 0x400:
            x, z = struct.unpack('<2i', cpu.mem_read(p, 8))
            chunks.append((x, z))
            exists = mode != 1 and (mode != 2 or not ((x + z) & 1)) and (mode != 4 or len(chunks) <= 2)
            machine.cpu.mem_write(HEAP + 0xA000 + 5352, bytes([int(mode == 3)]))
            result = HEAP + 0xA000 if exists else 0
        else:
            raise RuntimeError('Unexpected inside-query service')
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = int.from_bytes(cpu.mem_read(sp, 8), 'little')
        cpu.reg_write(x86.UC_X86_REG_RAX, result)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    for offset in (0x100, 0x200, 0x300, 0x400):
        pc = RETURN + offset
        machine.cpu.mem_write(pc, b'\xc3')
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, service, begin=pc, end=pc)
    lines = ['# BDS entity-inside queries v1', f'# input_sha256 {INPUT_SHA256}',
             *(f"# function {hex(a)} sha256 {data['functions'][hex(a)]['code_sha256']}"
               for a in (0x1410EE9B0, 0x143E8D580)),
             '# T enabled ready expansion bounds[6] result regionCount region[6]*n readCount cell[3]*n visitCount (opaqueBlock cell[3])*n',
             '# R mode rejectField5352 bounds[6] result chunkCount (x z)*n']
    rng = random.Random(0x1A51DE126)
    inputs = []
    for i in range(512):
        origin = (0, -0.001, 0.001, -16, 16, 1000.125, -300000.375, 30000000)[i % 8]
        width = (0, 0.001, 0.002, 0.6, 3, -0.1)[(i // 8) % 6]
        bounds = (origin, -0.001, -0.3, origin + width, 1.8, 0.3)
        expansion = (0, 0.001, 0.025, 0.125, -0.025)[i % 5]
        inputs.append((i % 7 != 0, i % 5 != 0, bits(expansion), tuple(bits(v) for v in bounds)))
    for i in range(256):
        low = tuple(rng.uniform(-10, 10) for _ in range(3))
        bounds = (*low, *(v + rng.uniform(-0.1, 3) for v in low))
        inputs.append((True, True, bits(rng.uniform(-0.05, 0.25)), tuple(bits(v) for v in bounds)))
    for odd in (0x7F800000, 0xFF800000, 0x7FC01234, 0xFFFFFFFF, 0xCF000001, 0x4F000000):
        for axis in range(6):
            bounds = [bits(v) for v in (0, 0, 0, 1, 1, 1)]
            bounds[axis] = odd
            inputs.append((True, False, 0, tuple(bounds)))
    for enabled, ready, expansion, bounds in inputs:
        entry = machine.call(0x1410EE9B0, [HEAP, HEAP + 0x100, 0, HEAP + 0x200])
        machine.cpu.reg_write(x86.UC_X86_REG_XMM2, expansion)
        machine.put(HEAP + 0x100, bounds)
        machine.qword(HEAP, HEAP + 0x1000)
        machine.qword(HEAP + 0x1070, RETURN + 0x100)
        machine.qword(HEAP + 0x1010, RETURN + 0x200)
        machine.qword(HEAP + 0x238, HEAP + 0x300 if enabled else 0)
        machine.qword(HEAP + 0x300, HEAP + 0x1100)
        machine.qword(HEAP + 0x1110, RETURN + 0x300)
        regions.clear(); reads.clear(); visits.clear()
        machine.run(entry, budget=1000000)
        result = machine.cpu.reg_read(x86.UC_X86_REG_RAX) & 255
        values = [int(enabled), int(ready), expansion, *bounds, result, len(regions),
                  *(v for r in regions for v in r), len(reads),
                  *(v & 0xFFFFFFFF for cell in reads for v in cell), len(visits),
                  *(v for visit in visits for v in (visit[0], *(p & 0xFFFFFFFF for p in visit[1:])))]
        for i, visit in enumerate(visits):
            if visit[0] != HEAP + 0x5000 + ((i + 1) % 8) * 8 or visit[1:] != reads[i]:
                raise RuntimeError('Inside visitor arguments differ from acquired block/cell')
        lines.append('T ' + ' '.join(f'{v:08x}' for v in values))
    for i in range(512):
        x, z = (rng.choice((-32, -16, 0, 16, 1000000)) + rng.uniform(-1, 1) for _ in range(2))
        bounds = (x, -100000, z, x + rng.uniform(0, 40), 100000, z + rng.uniform(0, 40))
        if i % 16 == 0:
            edge = math.floor(x / 16) * 16
            bounds = (edge, 0, z, edge - 1, 1, z)
        mode = i % 5
        reject = i & 1
        entry = machine.call(0x143E8D580, [HEAP, HEAP + 0x100, reject])
        machine.put(HEAP + 0x100, [bits(v) for v in bounds])
        machine.qword(HEAP, HEAP + 0x1000)
        machine.qword(HEAP + 0x1140, RETURN + 0x400)
        chunks.clear()
        machine.run(entry, budget=1000000)
        result = machine.cpu.reg_read(x86.UC_X86_REG_RAX) & 255
        values = [mode, reject, *(bits(v) for v in bounds), result, len(chunks),
                  *(v & 0xFFFFFFFF for chunk in chunks for v in chunk)]
        lines.append('R ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
