"""Execute the BDS JumpFromGround request classifier with synthetic sparse pools."""

import argparse
import itertools
import json
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, Machine


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    getters = {0x140EB51B0: 0, 0x140EB5000: 3, 0x140EB4E50: 2, 0x140EB4CA0: 1}
    queried = []
    inserted = []

    def service(cpu, pc, size, user):
        if pc in getters:
            queried.append(getters[pc])
            result = HEAP + 0x8800
        elif pc == 0x14030D2C0:
            entity = int.from_bytes(cpu.mem_read(cpu.reg_read(x86.UC_X86_REG_R8), 4), 'little')
            inserted.append(entity)
            result = cpu.reg_read(x86.UC_X86_REG_RDX)
        else:
            raise RuntimeError('Unexpected filter service')
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = int.from_bytes(cpu.mem_read(sp, 8), 'little')
        cpu.reg_write(x86.UC_X86_REG_RAX, result)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    for pc in (*getters, 0x14030D2C0):
        if not any(lo <= pc <= hi for lo, hi, _ in machine.cpu.mem_regions()):
            machine.cpu.mem_map(pc & ~4095, 4096)
        machine.cpu.mem_write(pc, b'\xc3')
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, service, begin=pc, end=pc)

    def pool(address, entity, membership):
        index = entity & 0x3FFFF
        page = index >> 11
        machine.qword(address + 8, address + 0x100)
        machine.qword(address + 16, address + 0x100 + (page + (membership != 3)) * 8)
        machine.qword(address + 0x100 + page * 8, 0 if membership == 4 else address + 0x800)
        value = (entity & 0xFFFC0000) | 17
        if membership == 0:
            value = 0xFFFFFFFF
        elif membership == 2:
            value ^= 0x40000
        elif membership == 5:
            value = (entity & 0xFFFC0000) | 0x3FFFF
        machine.put(address + 0x800 + (index & 0x7FF) * 4, [value])

    lines = ['# BDS JumpFromGround request classifier v1', f'# input_sha256 {INPUT_SHA256}',
             f"# function 0x140eaf890 sha256 {data['functions']['0x140eaf890']['code_sha256']}",
             '# F entity local lavaSlime squid destinationMembership outputRoute outputInsertCount outputInsertedEntity',
             '# membership 0 absent,1 valid,2 stale,3 page out of range,4 null page,5 reserved packed index',
             '# route 0 localPlayer,1 other,2 squid,3 lavaSlime; storage getters/insertion synthetic']
    for number, memberships in enumerate(itertools.product(range(6), repeat=4)):
        entity = (0, 1, 0x7FF, 0x800, 0x1FFFF, 0x3FFFE)[number % 6] | ((number % 4) << 18)
        entry = machine.call(0x140EAF890, [HEAP, HEAP + 0x100, HEAP + 0x110, HEAP + 0x120, HEAP + 0x200])
        machine.put(HEAP, [entity])
        for view, address, membership in zip((0x100, 0x110, 0x120), (0x1000, 0x3800, 0x6000), memberships):
            machine.qword(HEAP + view, HEAP + address)
            machine.put(HEAP + view + 8, [entity])
            pool(HEAP + address, entity, membership)
        pool(HEAP + 0x8800, entity, memberships[3])
        queried.clear()
        inserted.clear()
        machine.run(entry, budget=10000)
        if len(queried) != 1 or len(inserted) > 1:
            raise RuntimeError('Unexpected classifier storage calls')
        values = [entity, *memberships, queried[0], len(inserted), inserted[0] if inserted else 0]
        lines.append('F ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
