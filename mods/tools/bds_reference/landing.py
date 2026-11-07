"""Execute BDS vertical collision, block handlers, restitution and bounce writes.

Only world lookup and mechanical ECS pool/storage services are substituted.
Native visitors, membership checks, block decisions and arithmetic execute.
"""

import argparse
import json
import random
import struct
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, RETURN, Machine, bits

COMPONENTS = (0x0EC93C91, 0x33475976, 0x76E7A1DF, 0xC140391A, 0x3A5A9567, 0x870ED430)
HANDLERS = (0x143F3BF60, 0x143F0ECA0, 0x144282EC0)


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    pools = {component: HEAP + 0x3000 + i * 0x800 for i, component in enumerate(COMPONENTS)}

    def runtime(cpu, pc, size, user):
        a = cpu.reg_read(x86.UC_X86_REG_RCX)
        b = cpu.reg_read(x86.UC_X86_REG_RDX)
        if pc == 0x14478D6B0:
            machine.qword(b + 16, HEAP + 0x1000)
            result = b
        elif pc == 0x1403566F0:
            if a != pools[0x76E7A1DF]:
                raise RuntimeError('Unexpected restitution insertion')
            machine.put(a + 0x300, [0])
            machine.put(a + 0x500, [0, 0, 0])
            machine.qword(b + 8, 1)
            result = b
        elif pc == 0x140335160:
            result = HEAP + 0x2800
        else:
            component = b & 0xFFFFFFFF
            if component not in pools:
                raise RuntimeError(f'Unexpected landing component {component:#x}')
            result = pools[component]
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = struct.unpack('<Q', cpu.mem_read(sp, 8))[0]
        cpu.reg_write(x86.UC_X86_REG_RAX, result)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    for pc in (0x14478D6B0, 0x1403566F0, 0x140335160, 0x1402F0E60,
               0x1402F1010, 0x140C218D0, 0x140BB4FC0, 0x1402EA500, 0x1404FB850):
        page = pc & ~4095
        if not any(lo <= pc <= hi for lo, hi, _ in machine.cpu.mem_regions()):
            machine.cpu.mem_map(page, 4096)
        machine.cpu.mem_write(pc, b'\xc3')
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, runtime, begin=pc, end=pc)

    values = (0.0, -0.0, -1e-8, -2**-23, -1e-6, -0.01, -0.079, -0.08,
              -0.08000012, -0.081, -0.42, -1, -16, -100, 1e-8, 0.01, 0.08, 1)
    inputs = [(kind, flags, (0.5, 2.75, -1.125), (1.25, vy, -2.5))
              for kind in range(3) for flags in range(32) for vy in values]
    rng = random.Random(0xB0A1CE)
    for _ in range(128):
        inputs.append((rng.randrange(3), rng.randrange(32),
                       tuple(rng.uniform(-10000, 10000) for _ in range(3)),
                       tuple(rng.uniform(-20, 20) for _ in range(3))))
    lines = ['# BDS vertical collision and block restitution v1', f'# input_sha256 {INPUT_SHA256}',
             '# flags: sneak=1 minecart=2 bounce=4 player=8 priorRestitution=16',
             '# synthetic world lookup, ECS pool lookup and mechanical insertion only']
    for address in (0x140F1D820, *HANDLERS, 0x140F22DD0, 0x140F1D190, 0x140C1BA90):
        lines.append(f"# function {address:#x} sha256 {data['functions'][hex(address)]['code_sha256']}")
    for kind, flags, position, velocity in inputs:
        entry = machine.call(0x140F1D820, [HEAP, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300])
        machine.qword(HEAP + 0x300, HEAP + 0x400)
        machine.qword(HEAP + 0x1000 + 88, HEAP + 0x1800)
        machine.qword(HEAP + 0x1800, HEAP + 0x2000)
        machine.qword(HEAP + 0x1800 + 648, HEAP + 0x2400)
        machine.qword(HEAP + 0x2000 + 520, HANDLERS[kind])
        for index, component in enumerate(COMPONENTS):
            pool = pools[component]
            machine.qword(pool + 8, pool + 0x200)
            machine.qword(pool + 16, pool + 0x208)
            machine.qword(pool + 0x200, pool + 0x300)
            machine.qword(pool + 80, pool + 0x400)
            machine.qword(pool + 0x400, pool + 0x500)
            machine.qword(pool + 32, pool + 0x600)
            machine.qword(pool + 40, pool + 0x604)
            present = index < 2 or bool(flags & (16, 2, 4, 8)[index - 2])
            machine.put(pool + 0x300, [0 if present else 0xFFFFFFFF])
        state = pools[0x0EC93C91] + 0x500
        machine.put(state, [bits(v) for v in position])
        machine.put(state + 24, [bits(v) for v in velocity])
        machine.put(pools[0x33475976] + 0x500, [2 if flags & 1 else 0])
        rest = pools[0x76E7A1DF] + 0x500
        previous = (0.125, -0.25, 0.5) if flags & 16 else (0, 0, 0)
        machine.put(rest, [bits(v) for v in previous])
        bounce = pools[0x3A5A9567] + 0x500
        machine.put(bounce, [0x7FFFFFFF] * 3)
        machine.run(entry, budget=100000)
        if machine.get(pools[0x76E7A1DF] + 0x300, 1)[0] == 0:
            entry = machine.call(0x140C1BA90, [rest, state], reset=False)
            machine.run(entry)
        written_block = struct.unpack('<Q', machine.cpu.mem_read(bounce + 16, 8))[0]
        if written_block not in (0, HEAP + 0x2400):
            raise RuntimeError('Unexpected bounce block')
        output = (kind, flags, *(bits(v) for v in position), *(bits(v) for v in velocity),
                  *(bits(v) for v in previous), *machine.get(rest, 3), *machine.get(state + 24, 3),
                  *machine.get(bounce, 3), int(written_block != 0),
                  int(machine.get(pools[0x76E7A1DF] + 0x300, 1)[0] == 0))
        lines.append('V ' + ' '.join(f'{v:08x}' for v in output))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
