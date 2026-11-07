"""Execute the BDS MoveCollisionSystem callback and record its world query.

The synthetic world has no collision shapes. Region construction, movement cap,
cache refresh and dispatch execute BDS instructions without arithmetic hooks.
"""

import argparse
import json
import random
import struct
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, RETURN, Machine, bits


def generate(data):
    if data["input_sha256"] != INPUT_SHA256 or data["input_name"] != "Windows Server 1.26.10":
        raise ValueError("Wrong BDS reference")
    machine = Machine(data["functions"])
    regions = []

    def query(cpu, pc, size, user):
        region = cpu.reg_read(x86.UC_X86_REG_R8)
        regions.append(struct.unpack("<6I", cpu.mem_read(region, 24)))
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = struct.unpack("<Q", cpu.mem_read(sp, 8))[0]
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    machine.cpu.hook_add(unicorn.UC_HOOK_CODE, query, begin=RETURN + 0x200, end=RETURN + 0x200)
    lines = ["# BDS complete broad-phase callback v1", f"# input_sha256 {INPUT_SHA256}"]
    for address in ("0x140f90ea0", "0x140f90c90"):
        lines.append(f"# function {address} sha256 {data['functions'][address]['code_sha256']}")
    rng = random.Random(0xB205126)
    for index in range(512):
        origin = (0, 1000.125, -300000.375)[index % 3]
        width = (0.6, 0.025, 0, -0.1)[index % 4]
        height = (1.8, 1.5, 0.6, -0.1, 0)[index % 5]
        box = (origin - width / 2, 0, -width / 2, origin + width / 2, height, width / 2)
        step = (-0.5, 0, 0.5625, 1, 2)[index % 5]
        if index < 16:
            movement = ((0, 0, 0), (-0.0, 0, -0.0), (16, 0, 0), (0, 16.000002, 0),
                        (10, 10, 10), (100, -100, 100), (-16, 0, 0), (0, -16.000002, 0))[index % 8]
        else:
            movement = tuple(rng.uniform(-50, 50) for _ in range(3))
        address = machine.call(0x140F90EA0,
                               [HEAP, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300,
                                HEAP + 0x400, HEAP + 0x800, HEAP + 0x900, HEAP + 0xC00,
                                0, 0, HEAP + 0xA00, HEAP + 0xB00, 0])
        machine.put(HEAP + 0x100, [bits(value) for value in box])
        machine.put(HEAP + 0x200, [bits(step)])
        machine.qword(HEAP + 0x300, HEAP + 0xD00)
        machine.put(HEAP + 0x43C, [bits(value) for value in movement])
        machine.qword(HEAP + 0xA00, HEAP + 0x2000)
        machine.qword(HEAP + 0xA08, HEAP + 0x2000)
        machine.qword(HEAP + 0xB00, HEAP + 0x3000)
        machine.qword(HEAP + 0xB08, HEAP + 0x3000)
        machine.qword(HEAP + 0xC00, HEAP + 0xE00)
        machine.qword(HEAP + 0xEA8, RETURN + 0x200)
        regions.clear()
        machine.run(address, budget=100000)
        if len(regions) != 1 or machine.get(HEAP + 0x418, 6) != regions[0]:
            raise RuntimeError("BDS broad-phase did not refresh the expected empty cache")
        values = (bits(step), *(bits(value) for value in (*box, *movement)),
                  *machine.get(HEAP + 0x43C, 3), *regions[0])
        lines.append("B " + " ".join(f"{value:08x}" for value in values))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
