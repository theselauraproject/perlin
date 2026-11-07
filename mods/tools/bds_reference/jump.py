"""Execute both complete BDS ground-jump callbacks with synthetic world/ECS services."""

import argparse
import json
import random
import struct
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, RETURN, Machine, bits


def generate(data, sine):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    sine_base = 0x1472B2560
    for page in range(sine_base & ~4095, (sine_base + 0x40000 + 4095) & ~4095, 4096):
        if not any(lo <= page <= hi for lo, hi, _ in machine.cpu.mem_regions()):
            machine.cpu.mem_map(page, 4096)
    table = sine.read_bytes()
    import hashlib
    if hashlib.sha256(table).hexdigest() != '5834520a8a07b71081b530851803cf051427446a99643b9de13368cc17635628':
        raise ValueError('Wrong BDS sine table')
    machine.cpu.mem_write(sine_base, table)
    queries = []
    custom_height = attribute_type = 0

    def service(cpu, pc, size, user):
        result = 0
        if pc == RETURN + 0x100:
            p = cpu.reg_read(x86.UC_X86_REG_RDX)
            queries.append(struct.unpack('<3i', cpu.mem_read(p, 12)))
            result = HEAP + (0x2400 if len(queries) == 1 else 0x2600)
        elif pc == RETURN + 0x200:
            cpu.reg_write(x86.UC_X86_REG_XMM0, custom_height)
        elif pc == RETURN + 0x300:
            result = attribute_type
        elif pc == 0x140B5AF00:
            result = HEAP + 0x1800
        elif pc == 0x14030D2C0:
            result = cpu.reg_read(x86.UC_X86_REG_RDX)
            machine.qword(result + 8, 1)
        elif pc == 0x14030A5D0:
            result = HEAP + 0x1C00
        else:
            raise RuntimeError('Unexpected jump service')
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = int.from_bytes(cpu.mem_read(sp, 8), 'little')
        cpu.reg_write(x86.UC_X86_REG_RAX, result)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    for pc in (RETURN + 0x100, RETURN + 0x200, RETURN + 0x300, 0x140B5AF00, 0x14030D2C0, 0x14030A5D0):
        if not any(lo <= pc <= hi for lo, hi, _ in machine.cpu.mem_regions()):
            machine.cpu.mem_map(pc & ~4095, 4096)
        machine.cpu.mem_write(pc, b'\xc3')
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, service, begin=pc, end=pc)
    lines = ['# BDS complete ground jumps v1', f'# input_sha256 {INPUT_SHA256}',
             *(f"# function {hex(a)} sha256 {data['functions'][hex(a)]['code_sha256']}"
               for a in (0x140EAFAF0, 0x140EAFFD0, 0x140EC4780)),
             '# J variant actorFlags forceLow heightKind height effectKind amplifier oldTrigger prop0 prop1 minY position[3] velocity[3] yaw attributeKind attributeLevel outputLastPosition[3] outputVelocity[3] outputTrigger(bool,pos[3]) queryCount query[3]*n',
             '# synthetic block properties, height/type providers and sparse storage; callback arithmetic/control flow unchanged']
    rng = random.Random(0xA0A126)
    for variant in range(2):
        for index in range(768):
            actor_flags = 8 if index & 1 else 0
            force = int(index % 13 == 0)
            height_kind = index % 3
            custom_height = bits((0, -0.0, -0.25, 0.125, 0.42, 1, 3.25)[(index // 3) % 7])
            effect_kind = (index // 3) % 4
            amplifier = (-2, -1, 0, 1, 3, 127, -2147483648, 2147483647)[(index // 12) % 8]
            old_trigger = int(index % 5 == 0)
            properties0 = (0, 1 << 8, 1 << 43, (1 << 43) | (1 << 8))[index % 4]
            properties1 = (1 << 43) if (index // 4) % 2 else 0
            minimum_y = (-1.0, -0.25, 0.0, 0.9375, 1.0, 1.125, 1000.25)[(index // 8) % 7]
            position = (rng.uniform(-20, 20), minimum_y + 1.62, rng.uniform(-20, 20))
            velocity = (rng.uniform(-2, 2), (-1, -0.0, 0, 0.42, 1, 100)[index % 6], rng.uniform(-2, 2))
            yaw = (0, 45, 90, 135, 180, 225, 270, 360, -270, 1e20)[index % 10]
            attribute_kind = (index // 4) % 3
            attribute_type = 2 if attribute_kind == 1 else 1
            attribute_level = (-4, -1, 0, 1, 3, 2147483647)[(index // 12) % 6]
            if variant:
                args = [HEAP, HEAP + 0x100, HEAP + 0x1200, HEAP + 0x800,
                        HEAP + 0x400, HEAP + 0xA00, HEAP + 0xC00]
                routine = 0x140EAFFD0
            else:
                args = [HEAP, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300, HEAP + 0x400, 0 if height_kind == 0 else HEAP + 0x500,
                        HEAP + 0x700, HEAP + 0x800, HEAP + 0xA00, HEAP + 0xC00, force]
                routine = 0x140EAFAF0
            entry = machine.call(routine, args)
            machine.put(HEAP + 0x104, [bits(minimum_y)])
            machine.cpu.mem_write(HEAP + 0x200, bytes([actor_flags]))
            machine.put(HEAP + 0x304, [bits(yaw)])
            machine.put(HEAP + 0x700, [bits(v) for v in (-9, -8, -7)])
            machine.put(HEAP + 0x800, [bits(v) for v in position])
            machine.put(HEAP + 0x818, [bits(v) for v in velocity])
            machine.qword(0x147337878, HEAP + 0x1000)
            machine.put(HEAP + 0x1008, [2 if effect_kind == 3 else 0])
            machine.put(0x147147150, [0xFFFFFFFF])
            machine.qword(HEAP + 0x400, HEAP + 0x3000)
            machine.qword(HEAP + 0x408, HEAP + 0x3000 + (0 if effect_kind == 0 else 136))
            machine.put(HEAP + 0x3000, [0xFFFFFFFF if effect_kind == 2 else 8])
            machine.put(HEAP + 0x3020, [amplifier & 0xFFFFFFFF])
            if height_kind == 2:
                machine.qword(HEAP + 0x550, HEAP + 0x580)
                machine.qword(HEAP + 0x580, HEAP + 0x600)
                machine.qword(HEAP + 0x628, RETURN + 0x200)
            machine.qword(HEAP + 0xC00, HEAP + 0xE00)
            machine.qword(HEAP + 0xE10, RETURN + 0x100)
            for block, legacy, properties in ((0x2400, 0x2800, properties0), (0x2600, 0x2A00, properties1)):
                machine.qword(HEAP + block + 88, HEAP + legacy)
                machine.qword(HEAP + legacy + 296, properties)
            machine.cpu.mem_write(HEAP + 0x1C00, bytes([1]))
            machine.put(HEAP + 0x1C04, [v & 0xFFFFFFFF for v in (-11, -12, -13)])
            if old_trigger:
                machine.qword(HEAP + 0x1808, HEAP + 0x1900)
                machine.qword(HEAP + 0x1810, HEAP + 0x1908)
                machine.qword(HEAP + 0x1900, HEAP + 0x1A00)
            if attribute_kind:
                machine.qword(HEAP + 0x1200, HEAP + 0x1400)
                machine.qword(HEAP + 0x1208, HEAP + 0x1418)
                machine.qword(HEAP + 0x1410, HEAP + 0x1E00)
                machine.qword(HEAP + 0x1E00, HEAP + 0x2000)
                machine.put(HEAP + 0x1E0C, [attribute_level & 0xFFFFFFFF])
                machine.qword(HEAP + 0x2010, RETURN + 0x300)
            queries.clear()
            machine.run(entry, budget=100000)
            trigger = [machine.cpu.mem_read(HEAP + 0x1C00, 1)[0], *machine.get(HEAP + 0x1C04, 3)]
            values = [variant, actor_flags, force, height_kind, custom_height, effect_kind, amplifier & 0xFFFFFFFF,
                      old_trigger, properties0, properties1, bits(minimum_y), *(bits(v) for v in position),
                      *(bits(v) for v in velocity), bits(yaw), attribute_kind, attribute_level & 0xFFFFFFFF,
                      *machine.get(HEAP + 0x700, 3), *machine.get(HEAP + 0x818, 3), *trigger, len(queries),
                      *(v & 0xFFFFFFFF for query in queries for v in query)]
            lines.append('J ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('export', 'sine', 'out'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text()), args.sine))
