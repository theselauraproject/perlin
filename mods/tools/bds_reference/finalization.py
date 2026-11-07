"""Execute the complete BDS FinalizeMoveSystem callback and its tag helpers.

Synthetic sparse pools supply membership and mechanical insertion/removal.
The actual callback and tag-decision helpers execute without replacements.
"""

import argparse
import json
import random
import struct
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, RETURN, Machine, bits

TAGS = ((0xD8773ED9, 0x3000), (0x990A4EC3, 0x3600), (0xC3E299A3, 0x3C00),
        (0xCDF8D319, 0x4200), (0x43027696, 0x4800))


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    pools = {component: HEAP + offset for component, offset in TAGS}

    def runtime(cpu, pc, size, user):
        a = cpu.reg_read(x86.UC_X86_REG_RCX)
        b = cpu.reg_read(x86.UC_X86_REG_RDX)
        if pc == 0x146251B16:
            value = cpu.reg_read(x86.UC_X86_REG_XMM0) & 0xFFFFFFFF
            exponent = value & 0x7F800000
            if exponent == 0x7F800000:
                raise RuntimeError('Position classification encountered a non-finite value')
            result = 0 if value & 0x7FFFFFFF == 0 else (-2 if exponent == 0 else -1)
        elif pc == 0x14030D2C0:
            if a not in pools.values():
                raise RuntimeError('Unexpected finalization tag insertion')
            machine.put(a + 0x300, [0])
            result = b
        elif pc == RETURN + 0x500:
            if a not in pools.values():
                raise RuntimeError('Unexpected finalization tag erasure')
            machine.put(a + 0x300, [0xFFFFFFFF])
            result = 0
        else:
            component = b & 0xFFFFFFFF
            if component not in pools:
                raise RuntimeError('Unexpected finalization component lookup')
            result = pools[component]
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = struct.unpack('<Q', cpu.mem_read(sp, 8))[0]
        cpu.reg_write(x86.UC_X86_REG_RAX, result & 0xFFFFFFFFFFFFFFFF)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    for pc in (0x140C70450, 0x140C706A0, 0x140BA9900, 0x140C70850,
               0x140BECF40, 0x14030D2C0, RETURN + 0x500, 0x146251B16):
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, runtime, begin=pc, end=pc)
    epsilon = 2 ** -23
    next_epsilon = struct.unpack('<f', struct.pack('<I', bits(epsilon) + 1))[0]
    motions = [((0, 0, 0), (0, 0, 0)), ((1, -0.08, 0.3), (1, 0, 0.3)),
               ((1, 0.42, -0.5), (0, 0.1, 0)), ((1, 0, 1), (1 + epsilon, epsilon, 1)),
               ((0, 0, 0), (next_epsilon, next_epsilon, -next_epsilon)),
               ((1, 0, 1), (1 - epsilon / 2, 0, 1)),
               ((0.1, -1e-8, 0), (0.1, 0, 0)), ((0.1, -next_epsilon, 0), (0.1, 0, 0))]
    inputs = []
    for flags in range(32):
        for mode in range(3):
            for preserve in (False, True):
                for index, (requested, resolved) in enumerate(motions):
                    origin = (0, 1000.125, -1000.375)[index % 3]
                    body = (origin - 0.3, 0, -0.3, origin + 0.3, 1.8, 0.3)
                    offset = (0, 1.62, -0.5)[index % 3]
                    inputs.append((mode, preserve, flags, offset, body, requested, resolved, (1.25, -0.42, -3.5)))
    rng = random.Random(0xF1A1126)
    for index in range(256):
        body = tuple(rng.uniform(-10000, 10000) for _ in range(6))
        requested = tuple(rng.uniform(-1, 1) for _ in range(3))
        resolved = tuple(v if rng.randrange(2) else rng.uniform(-1, 1) for v in requested)
        inputs.append((rng.randrange(3), bool(rng.randrange(2)), rng.randrange(32), rng.uniform(-2, 2),
                       body, requested, resolved, tuple(rng.uniform(-16, 16) for _ in range(3))))
    lines = ['# BDS complete move finalization v1', f'# input_sha256 {INPUT_SHA256}',
             '# synthetic sparse pools and mechanical tag storage; host finite fdclass']
    for address in ('0x140c638a0', '0x140c70600', '0x140c77f80', '0x140c69760', '0x140c78040'):
        lines.append(f"# function {address} sha256 {data['functions'][address]['code_sha256']}")
    for mode, preserve, flags, offset, body, requested, resolved, velocity in inputs:
        entry = machine.call(0x140C638A0,
                             [HEAP, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300, HEAP + 0x400,
                              HEAP + 0x500, HEAP + 0x600, HEAP + 0x700])
        machine.put(HEAP + 0x100, [bits(v) for v in body])
        machine.put(HEAP + 0x230, [bits(v) for v in (*requested, *resolved)])
        machine.cpu.mem_write(HEAP + 0x288, bytes([int(preserve)]))
        machine.put(HEAP + 0x300, [bits(offset)])
        machine.put(HEAP + 0x418, [bits(v) for v in velocity])
        machine.qword(HEAP + 0x500, pools[0xC3E299A3])
        machine.qword(HEAP + 0x600, HEAP + 0x1000)
        machine.qword(HEAP + 0x700, HEAP + 0x800)
        for pool in (*pools.values(), HEAP + 0x1000):
            machine.qword(pool, pool + 0x100)
            machine.qword(pool + 0x110, RETURN + 0x500)
            machine.qword(pool + 8, pool + 0x200)
            machine.qword(pool + 16, pool + 0x208)
            machine.qword(pool + 0x200, pool + 0x300)
            machine.qword(pool + 80, pool + 0x400)
            machine.qword(pool + 0x400, pool + 0x500)
        for index, (component, _) in enumerate(TAGS):
            machine.put(pools[component] + 0x300, [0 if flags & (1 << index) else 0xFFFFFFFF])
        machine.put(HEAP + 0x1300, [0 if mode else 0xFFFFFFFF])
        machine.put(HEAP + 0x1504, [16 if mode == 2 else 0])
        machine.run(entry, budget=100000)
        final_flags = sum((1 << index) for index, (component, _) in enumerate(TAGS)
                          if machine.get(pools[component] + 0x300, 1)[0] < 0x3FFFF)
        values = (mode, int(preserve), flags, bits(offset), *(bits(v) for v in body),
                  *(bits(v) for v in requested), *(bits(v) for v in resolved), *(bits(v) for v in velocity),
                  *machine.get(HEAP + 0x400, 3), *machine.get(HEAP + 0x418, 3), final_flags)
        lines.append('F ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
