"""Execute BDS gravity selection, levitation, application and vertical drag.

Synthetic ECS storage is preallocated. The recorded pipeline follows the
registration order and explicit travel-tag exclusions; this is not a full tick.
"""

import argparse
import json
import struct
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, Machine, bits


def generate(data):
    if data["input_sha256"] != INPUT_SHA256 or data["input_name"] != "Windows Server 1.26.10":
        raise ValueError("Wrong BDS reference")
    machine = Machine(data["functions"])
    mapped_pages = {int(address, 16) for function in data["functions"].values()
                    for address in function["pages"]}
    for address in (0x147337950, 0x147337958, 0x147147150):
        page = address & ~4095
        if page not in mapped_pages:
            machine.cpu.mem_map(page, 4096)
            mapped_pages.add(page)
    gravity_present = False

    def lookup(cpu, pc, size, user):
        nonlocal gravity_present
        component = cpu.reg_read(x86.UC_X86_REG_RDX) & 0xFFFFFFFF
        if pc == 0x14104A290 and component == 0xB71B6811:
            result = HEAP + 0x1000
            gravity_present = True
        elif pc == 0x140341710 and component == 4029909219:
            result = HEAP + 0x3000
        else:
            raise RuntimeError("Unexpected vertical-motion ECS lookup")
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = struct.unpack("<Q", cpu.mem_read(sp, 8))[0]
        cpu.reg_write(x86.UC_X86_REG_RAX, result)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    for pc in (0x14104A290, 0x140341710):
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, lookup, begin=pc, end=pc)

    def prepare(velocity, has_gravity, scaffolding, slow, level):
        nonlocal gravity_present
        machine.call(0, [])
        gravity_present = False
        machine.qword(HEAP + 0x1008, HEAP + 0x1100)
        machine.qword(HEAP + 0x1010, HEAP + 0x1108)
        machine.qword(HEAP + 0x1100, HEAP + 0x1200)
        machine.qword(HEAP + 0x1050, HEAP + 0x1300)
        machine.qword(HEAP + 0x1300, HEAP + 0x1400)
        machine.qword(HEAP + 0x3008, HEAP + 0x3100)
        machine.qword(HEAP + 0x3010, HEAP + 0x3108)
        machine.qword(HEAP + 0x3100, HEAP + 0x3200)
        machine.qword(HEAP + 0x400, (1 << 49) if has_gravity else 0)
        machine.qword(HEAP + 0x408, scaffolding << 5)
        machine.put(HEAP + 0x201C, [velocity])
        machine.put(HEAP + 0x500, [bits(11.25)])
        machine.qword(HEAP + 0x600, HEAP + 0x6000)
        machine.qword(HEAP + 0x608, HEAP + 0x6000 + 28 * 136)
        machine.cpu.mem_write(HEAP + 0x6000, bytes(28 * 136))
        if slow:
            machine.put(HEAP + 0x6000 + 27 * 136, [27])
        if level:
            machine.put(HEAP + 0x6000 + 24 * 136, [24])
            machine.put(HEAP + 0x6000 + 24 * 136 + 32, [level - 1])
        machine.qword(0x147337950, HEAP + 0xE000)
        machine.put(HEAP + 0xE008, [27])
        machine.qword(0x147337958, HEAP + 0xE100)
        machine.put(HEAP + 0xE108, [24])
        machine.put(0x147147150, [0])

    def call(address, arguments):
        machine.run(machine.call(address, arguments, reset=False), budget=100000)

    lines = ["# BDS vertical motion v1", f"# input_sha256 {INPUT_SHA256}"]
    for address in ("0x14103e8f0", "0x14104f970", "0x14104f680", "0x141051e00",
                    "0x14104fc50", "0x141051c40"):
        lines.append(f"# function {address} sha256 {data['functions'][address]['code_sha256']}")
    velocities = [bits(v) for v in (-10, -0.42, -0.08, -0.000001, -0.0, 0, 0.000001, 0.42, 10)]
    for has_gravity in (False, True):
        for scaffolding in range(8):
            for slow in (False, True):
                for velocity in velocities:
                    prepare(velocity, has_gravity, scaffolding, slow, 0)
                    call(0x14103E8F0, [0, HEAP, HEAP + 0x400, HEAP + 0x600, HEAP + 0x2000, 0])
                    acceleration = machine.get(HEAP + 0x1400, 1)[0] if gravity_present else 0
                    reset_fall = machine.cpu.mem_read(HEAP + 0x1404, 1)[0] if gravity_present else 0
                    values = (int(has_gravity), scaffolding, int(slow), velocity,
                              int(gravity_present), acceleration, reset_fall)
                    lines.append("G " + " ".join(f"{v:08x}" for v in values))
    for mode in range(3):
        for has_gravity in (False, True):
            for flags in range(8):
                scaffolding = 5 if flags & 1 else 0
                auto_climb = bool(flags & 2)
                discard = bool(flags & 4)
                for level in (0, 1, 5, 255):
                    for slow in (False, True):
                        for velocity in velocities:
                            prepare(velocity, has_gravity, scaffolding, slow, level)
                            swimming = slow
                            if mode == 2:
                                call(0x141051C40, [0, HEAP + 0x2000, HEAP + 0x500])
                            else:
                                if level:
                                    call(0x14104F680, [HEAP, HEAP + 0x600, HEAP + 0x2000, 0])
                                elif mode == 0 and not auto_climb:
                                    call(0x14103E8F0, [0, HEAP, HEAP + 0x400, HEAP + 0x600, HEAP + 0x2000, 0])
                                elif mode == 1:
                                    machine.qword(HEAP + 0x400, (1 << 57) if swimming else 0)
                                    call(0x14104FC50, [HEAP, HEAP + 0x400, 0])
                                if gravity_present:
                                    call(0x14104F970, [0, HEAP + 0x1400, HEAP + 0x500, HEAP + 0x2000])
                                if mode == 0 and not auto_climb and not discard:
                                    call(0x141051E00, [0, HEAP + 0x400, HEAP + 0x2000])
                            values = (mode, int(has_gravity), flags, level, int(slow), int(swimming),
                                      velocity, *machine.get(HEAP + 0x201C, 1), *machine.get(HEAP + 0x500, 1))
                            lines.append("V " + " ".join(f"{v:08x}" for v in values))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
