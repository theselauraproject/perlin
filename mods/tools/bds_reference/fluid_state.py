"""Run complete BDS fluid-state callbacks; substitute only ECS storage services."""

import argparse
import json
import random
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, RETURN, INPUT_SHA256, Machine, bits


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    pool_address = HEAP + 0x6000
    splash = HEAP + 0x800
    services = (0x140B5FD00, 0x140E02F20, 0x14030D2C0,
                0x140E13100, 0x140E1C230, 0x14031FD50, RETURN + 0x100,
                RETURN + 0x200, 0x140FC94C0, 0x146251B16)
    calls = []
    boat = False
    entity = 0
    water_intersects = lava_intersects = False
    material_queries = []
    query_bounds = []
    ready_bounds = []

    def service(cpu, pc, size, user):
        if pc in (0x140B5FD00, 0x140E02F20):
            result = pool_address
        elif pc == 0x14030D2C0:
            inserted = machine.get(cpu.reg_read(x86.UC_X86_REG_R8), 1)[0]
            if inserted != entity:
                raise RuntimeError('Unexpected inserted entity')
            calls.append('insert')
            write_membership(pool_address, entity, 1)
            result = cpu.reg_read(x86.UC_X86_REG_RDX)
        elif pc == RETURN + 0x100:
            calls.append('remove')
            write_membership(pool_address, entity, 0)
            result = 0
        elif pc == 0x140E13100:
            calls.append('splash')
            result = splash
        elif pc == 0x140E1C230:
            result = HEAP + 0xA000
        elif pc == 0x14031FD50:
            if machine.get(cpu.reg_read(x86.UC_X86_REG_RDX), 1)[0] != 123:
                raise RuntimeError('Boat query used the wrong passenger reference')
            calls.append('boat')
            result = int(boat)
        elif pc == RETURN + 0x200:
            ready_bounds.append(machine.get(cpu.reg_read(x86.UC_X86_REG_RDX), 6))
            result = 1
        elif pc == 0x140FC94C0:
            material = cpu.reg_read(x86.UC_X86_REG_RDX)
            material_queries.append(material)
            query_bounds.append(machine.get(cpu.reg_read(x86.UC_X86_REG_R8), 6))
            result = int(lava_intersects if material == 6 else water_intersects)
        elif pc == 0x146251B16:
            value = cpu.reg_read(x86.UC_X86_REG_XMM0) & 0xFFFFFFFF
            exponent = value & 0x7F800000
            if exponent == 0x7F800000:
                raise RuntimeError('Non-finite bounds outside this oracle scope')
            result = 0 if value & 0x7FFFFFFF == 0 else (-2 if exponent == 0 else -1)
        else:
            raise RuntimeError('Unexpected storage service')
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        cpu.reg_write(x86.UC_X86_REG_RAX, result & 0xFFFFFFFFFFFFFFFF)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, int.from_bytes(cpu.mem_read(sp, 8), 'little'))

    for pc in services:
        if not any(lo <= pc <= hi for lo, hi, _ in machine.cpu.mem_regions()):
            machine.cpu.mem_map(pc & ~4095, 4096)
        machine.cpu.mem_write(pc, b'\xc3')
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, service, begin=pc, end=pc)

    def write_membership(address, entity, membership):
        index = entity & 0x3FFFF
        page = index >> 11
        machine.qword(address, HEAP + 0xE000)
        machine.qword(address + 8, address + 0x100)
        machine.qword(address + 16, address + 0x100 + (page + (membership != 3)) * 8)
        machine.qword(address + 0x100 + page * 8, 0 if membership == 4 else address + 0x800)
        packed = (entity & 0xFFFC0000) | 17
        if membership == 0:
            packed = 0xFFFFFFFF
        elif membership == 2:
            packed ^= 0x40000
        elif membership == 5:
            packed = (entity & 0xFFFC0000) | 0x3FFFF
        machine.put(address + 0x800 + (index & 0x7FF) * 4, [packed])

    lines = ['# BDS fluid-state callbacks v1', f'# input_sha256 {INPUT_SHA256}',
             *[f"# function {hex(pc)} sha256 {data['functions'][hex(pc)]['code_sha256']}"
               for pc in (0x140E0E820, 0x140E0E6F0, 0x140FC98E0)],
             '# membership 0 absent,1 valid,2 stale,3 out-of-range page,4 null page,5 reserved index',
             '# W entity material fallingMembership passengerMembership waterMembership registryMatches boat fallBits splashPresent splashValue outputWater outputFallBits outputSplashPresent outputSplashValue inserts removes boatQueries splashGets',
             '# C same fields as W, then chainId tick; 32 chains of 16 callbacks retaining native output state',
             '# L entity material lavaMembership outputLava inserts removes',
             '# K waterIntersects lavaIntersects outputMaterial outputWaterIntersects; complete fetch callback with synthetic collection/readiness and finite fdclass, flow disabled',
             '# B inputBounds[6] bypassContraction outputLavaBounds[6] outputWaterBounds[6] outputReadinessBounds[6]; finite bounds, complete fetch callback',
             '# Complete callback instructions; pool getters/mutation and boat-view query are synthetic services.']
    rng = random.Random(0xE0E820)
    previous = None
    for number in range(2560):
        entity = (0, 1, 0x7FF, 0x800, 0x1FFFF, 0x3FFFE)[number % 6] | ((number % 4) << 18)
        material = (0, 5, 6, 0xFFFFFFFF)[number % 4]
        falling, passenger, water = (rng.randrange(6) for _ in range(3))
        registry_matches, boat, splash_present = (rng.randrange(2) for _ in range(3))
        fall = (0, 0x80000000, 0x3F800000, 0x7FC01234, 0x7F800000, 0xFF800000)[number % 6]
        splash_value = (0, 1, 2, 0xFFFFFFFF)[number % 4]
        chained = number >= 2048
        tick = (number - 2048) % 16
        if chained:
            chain = (number - 2048) // 16
            entity = (0, 1, 0x7FF, 0x800, 0x1FFFF, 0x3FFFE)[chain % 6] | ((chain % 4) << 18)
            material = (0, 5, 5, 5, 6, 5, 5, 0)[tick % 8]
            if tick:
                water, fall, splash_present, splash_value = previous
        entry = machine.call(0x140E0E820, [HEAP, HEAP + 0x100, HEAP + 0x200,
            HEAP + 0x300, HEAP + 0x400, HEAP + 0x500, HEAP + 0x600, HEAP + 0x700])
        machine.put(HEAP, [entity])
        machine.put(HEAP + 0x100, [material])
        machine.put(HEAP + 0x200, [fall])
        for view, address, membership in ((0x300, 0x1000, falling), (0x400, 0x3800, passenger), (0x500, 0x6000, water)):
            machine.qword(HEAP + view, HEAP + address)
            machine.put(HEAP + view + 8, [entity])
            write_membership(HEAP + address, entity, membership)
        machine.qword(HEAP + 0x3850, HEAP + 0xC000)
        machine.qword(HEAP + 0xC000, HEAP + 0xD000)
        machine.put(HEAP + 0xD000 + 16 * 17, [123, 77 if registry_matches else 78])
        machine.qword(HEAP + 0x700, HEAP + 0x900)
        machine.put(HEAP + 0x900 + 352, [77])
        machine.qword(HEAP + 0xE010, RETURN + 0x100)
        machine.put(splash, [splash_value])
        calls.clear()
        machine.run(entry, budget=10000)
        output_water = int(('insert' in calls) or (water == 1 and 'remove' not in calls))
        output_splash = int(splash_present or 'splash' in calls)
        values = [entity, material, falling, passenger, water, registry_matches, int(boat), fall,
            splash_present, splash_value, output_water, machine.get(HEAP + 0x200, 1)[0],
            output_splash, machine.get(splash, 1)[0], calls.count('insert'), calls.count('remove'),
            calls.count('boat'), calls.count('splash')]
        previous = (output_water, machine.get(HEAP + 0x200, 1)[0], output_splash, machine.get(splash, 1)[0])
        if chained:
            values.extend(((number - 2048) // 16, tick))
        lines.append(('C ' if chained else 'W ') + ' '.join(f'{v:08x}' for v in values))
    for number in range(128):
        entity = (number % 4) << 18 | (number % 3) * 0x800
        material = (0, 5, 6, 0xFFFFFFFF)[number % 4]
        membership = (number // 4) % 6
        entry = machine.call(0x140E0E6F0, [HEAP, HEAP + 0x100, HEAP + 0x600])
        machine.put(HEAP, [entity])
        machine.put(HEAP + 0x100, [material])
        write_membership(pool_address, entity, membership)
        machine.qword(HEAP + 0xE010, RETURN + 0x100)
        calls.clear()
        machine.run(entry, budget=10000)
        output = int('insert' in calls or (membership == 1 and 'remove' not in calls))
        values = [entity, material, membership, output, calls.count('insert'), calls.count('remove')]
        lines.append('L ' + ' '.join(f'{v:08x}' for v in values))
    for number in range(1056):
        water_intersects = bool(number & 1)
        lava_intersects = bool(number & 2)
        entity = number
        entry = machine.call(0x140FC98E0, [HEAP, HEAP + 0x100, HEAP + 0x200,
            HEAP + 0x300, HEAP + 0x400, HEAP + 0x500, HEAP + 0x600, HEAP + 0x800])
        machine.put(HEAP, [entity])
        if number < 32:
            body = (0, 0, 0, 0.6, 1.8, 0.6)
            bypass = False
        else:
            origin = (-10000, -1, -0.0, 0, 1, 10000)[number % 6]
            extents = (0, 0.001, 0.002, 0.19999999, 0.2, 0.6, 0.8, 0.802, 1.8, 4, -0.1)
            body = (origin, -1.0, origin, origin + extents[number % len(extents)],
                -1.0 + extents[(number // 11) % len(extents)], origin + extents[(number // 121) % len(extents)])
            bypass = number % 3 == 0
        machine.put(HEAP + 0x300, [bits(v) for v in body])
        machine.qword(HEAP + 0x500, pool_address)
        machine.put(HEAP + 0x508, [entity])
        write_membership(pool_address, entity, int(bypass))
        machine.qword(HEAP + 0x600, HEAP + 0xF000)
        machine.qword(HEAP + 0x608, HEAP + 0xF000)
        machine.qword(HEAP + 0x610, HEAP + 0xFF00)
        machine.qword(HEAP + 0x800, HEAP + 0x900)
        machine.qword(HEAP + 0x970, RETURN + 0x200)
        material_queries.clear()
        query_bounds.clear()
        ready_bounds.clear()
        machine.run(entry, budget=10000)
        if material_queries != [6, 5]:
            raise RuntimeError('Unexpected liquid collection order')
        if number < 32:
            values = [int(water_intersects), int(lava_intersects), machine.get(HEAP + 0x200, 1)[0],
                machine.cpu.mem_read(HEAP + 0x206, 1)[0]]
            lines.append('K ' + ' '.join(f'{v:08x}' for v in values))
        else:
            if len(query_bounds) != 2 or len(ready_bounds) != 1:
                raise RuntimeError('Missing native query bounds')
            values = [*(bits(v) for v in body), int(bypass), *query_bounds[0], *query_bounds[1], *ready_bounds[0]]
            lines.append('B ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
