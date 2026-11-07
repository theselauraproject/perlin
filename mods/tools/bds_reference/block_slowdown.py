"""Execute BDS block slowdown callbacks and first-application construction."""

import argparse
import json
import random
import subprocess
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, Machine, bits


def generate(data, runner, pages, scratch):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])

    def service(cpu, pc, size, user):
        if pc == 0x14030D2C0:
            result = cpu.reg_read(x86.UC_X86_REG_RDX)
            machine.qword(result + 8, 1)
        else:
            result = HEAP + 0x500
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = int.from_bytes(cpu.mem_read(sp, 8), 'little')
        cpu.reg_write(x86.UC_X86_REG_RAX, result)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    for pc in (0x14030D2C0, 0x1405086B0, 0x140B612B0, 0x140B6D6D0):
        if not any(lo <= pc <= hi for lo, hi, _ in machine.cpu.mem_regions()):
            machine.cpu.mem_map(pc & ~4095, 4096)
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, service, begin=pc, end=pc)
    rng = random.Random(0x510D126)
    edge = [0, 0x80000000, 1, 0x80000001, 0x33FFFFFF, 0x34000000, 0x34000001,
            bits(0.25), bits(0.05), bits(1), bits(-1), 0x7F7FFFFF, 0x7F800000,
            0xFF800000, 0x7FC01234, 0xFFC05678, 0x7FA01234, 0xFFFFFFFF]
    lines = ['# BDS block slowdown v1', f'# input_sha256 {INPUT_SHA256}',
             *(f"# function {hex(a)} sha256 {data['functions'][hex(a)]['code_sha256']}"
               for a in (0x140C1EF20, 0x140B58EA0, 0x140B80D80, 0x140C1EF10, 0x140C1EE80, 0x140C1EDC0)),
             '# D multiplier[3] movement[3] velocity[3] outputMultiplier[3] outputMovement[3] outputVelocity[3]',
             '# P oldApplied(0 absent,1 false,2 true) oldMultiplier[3] incoming[3] outputApplied outputMultiplier[3]',
             '# F appliedByte fallDistance outputFallDistance',
             '# I abilityFlags multiplier[3] outputMultiplier[3]; absent applied record']
    direct = []
    for i in range(1024):
        if i < 512:
            multiplier = [edge[(i // (len(edge) ** axis)) % len(edge)] for axis in range(3)]
            movement = [edge[(i + 3 * axis) % len(edge)] for axis in range(3)]
        else:
            multiplier = [rng.getrandbits(32) for _ in range(3)]
            movement = [rng.getrandbits(32) for _ in range(3)]
        velocity = [rng.getrandbits(32) for _ in range(3)]
        entry = machine.call(0x140C1EF20, [HEAP, HEAP + 0x100, HEAP + 0x200])
        machine.put(HEAP, multiplier)
        machine.put(HEAP + 0x13C, movement)
        machine.put(HEAP + 0x218, velocity)
        machine.run(entry)
        values = [*multiplier, *movement, *velocity, *machine.get(HEAP, 3),
                  *machine.get(HEAP + 0x13C, 3), *machine.get(HEAP + 0x218, 3)]
        direct.append('D ' + ' '.join(f'{v:08x}' for v in values))
    scratch.write_text('\n'.join(direct) + '\n')
    native = subprocess.run([str(runner.resolve()), str(pages.resolve()), str(scratch.resolve())],
                            check=True, capture_output=True, text=True).stdout
    checked = [line for line in native.splitlines() if line.startswith('D ')]
    if len(checked) != len(direct):
        raise RuntimeError('Native slowdown cross-check returned incomplete output')
    corrections = 0
    for old, new in zip(direct, checked):
        before, after = old.split(), new.split()
        if before[:10] != after[:10]:
            raise RuntimeError('Native runner changed input fields')
        for a, b in zip(before[10:], after[10:]):
            x, y = int(a, 16), int(b, 16)
            if x != y:
                if not ((x & 0x7FFFFFFF) > 0x7F800000 and (y & 0x7FFFFFFF) > 0x7F800000):
                    raise RuntimeError('Unicorn/native slowdown mismatch outside NaN payloads')
                corrections += 1
    lines.append(f'# native_x86_crosscheck {len(checked)} cases; {corrections} Unicorn NaN payload fields corrected')
    lines.extend(checked)
    for i in range(768):
        multiplier = [edge[(i // (len(edge) ** axis)) % len(edge)] for axis in range(3)]
        incoming = [edge[(i + axis) % len(edge)] for axis in range(3)]
        present = i % 3
        applied = present
        if not present:
            entry = machine.call(0x140B80D80, [HEAP, HEAP + 0x100, HEAP + 0x200, 0, HEAP + 0x300])
            machine.put(HEAP + 0x300, multiplier)
            machine.run(entry)
            applied = machine.cpu.mem_read(HEAP + 0x500, 1)[0] + 1
        entry = machine.call(0x140B58EA0, [HEAP, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300])
        machine.put(HEAP + 0x100, multiplier)
        machine.put(HEAP + 0x300, incoming)
        machine.run(entry)
        values = [present, *multiplier, *incoming, applied, *machine.get(HEAP + 0x100, 3)]
        lines.append('P ' + ' '.join(f'{v:08x}' for v in values))
    for byte in range(256):
        fall = edge[byte % len(edge)]
        entry = machine.call(0x140C1EF10, [HEAP, HEAP + 0x100])
        machine.cpu.mem_write(HEAP, bytes([byte]))
        machine.put(HEAP + 0x100, [fall])
        machine.run(entry)
        lines.append(f'F {byte:08x} {fall:08x} {machine.get(HEAP + 0x100, 1)[0]:08x}')
    for flags in range(256):
        multiplier = [edge[(flags + axis) % len(edge)] for axis in range(3)]
        entry = machine.call(0x140C1EE80, [HEAP, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300])
        machine.put(HEAP + 0x100, multiplier)
        machine.put(HEAP + 0x204, [flags])
        machine.run(entry)
        values = [flags, *multiplier, *machine.get(HEAP + 0x100, 3)]
        lines.append('I ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('export', 'native_runner', 'native_pages', 'scratch', 'out'):
        parser.add_argument('--' + name.replace('_', '-'), required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text()), args.native_runner, args.native_pages, args.scratch))
