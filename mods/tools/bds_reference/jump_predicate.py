"""Execute native jump permission branches with supplied support-block and ECS services."""

import argparse
import json
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, RETURN, Machine


def generate(data):
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    stand = lightweight = False
    services = []

    def service(cpu, pc, size, user):
        if pc == 0x143262B80:
            result = cpu.reg_read(x86.UC_X86_REG_RCX)
            machine.put(result, [7, 8, 9])
        elif pc == RETURN + 0x100:
            result = HEAP + 0x1000
        elif pc == 0x14412E060:
            result = HEAP + 0x1200
        elif pc == RETURN + 0x200:
            services.append(0)
            result = int(stand)
        elif pc == RETURN + 0x300:
            services.append(1)
            result = int(lightweight)
        else:
            raise RuntimeError('Unexpected predicate service')
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = int.from_bytes(cpu.mem_read(sp, 8), 'little')
        cpu.reg_write(x86.UC_X86_REG_RAX, result)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    for pc in (0x143262B80, 0x14412E060, RETURN + 0x100, RETURN + 0x200, RETURN + 0x300):
        if not any(lo <= pc <= hi for lo, hi, _ in machine.cpu.mem_regions()):
            machine.cpu.mem_map(pc & ~4095, 4096)
        machine.cpu.mem_write(pc, b'\xc3')
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, service, begin=pc, end=pc)
    lines = ['# BDS ground-jump permission predicate v1', f'# input_sha256 {INPUT_SHA256}',
             f"# function 0x140defbf0 sha256 {data['functions']['0x140defbf0']['code_sha256']}",
             '# P actorUpperFlags powderSnow canStandOnSnow lightweight outputAllowed queryCount queryKind*n',
             '# support selection, block lookup/identity and ECS queries are supplied services; permission branches execute BDS']
    for flags in range(256):
        for snow in range(2):
            for stand in range(2):
                for lightweight in range(2):
                    entry = machine.call(0x140DEFBF0, [HEAP, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300, HEAP + 0x400])
                    machine.qword(HEAP + 8, flags)
                    machine.qword(HEAP + 0x100, HEAP + 0x500)
                    machine.qword(HEAP + 0x510, RETURN + 0x100)
                    machine.qword(HEAP + 0x400, HEAP + 0x600)
                    machine.qword(HEAP + 0x628, RETURN + 0x200)
                    machine.qword(HEAP + 0x630, RETURN + 0x300)
                    machine.qword(HEAP + 0x10F8, 0x1234 if snow else 0x5678)
                    machine.qword(HEAP + 0x12F8, 0x1234)
                    services.clear()
                    machine.run(entry, budget=10000)
                    output = machine.cpu.reg_read(x86.UC_X86_REG_RAX) & 255
                    values = [flags, snow, stand, lightweight, output, len(services), *services]
                    lines.append('P ' + ' '.join(f'{v:08x}' for v in values))
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
