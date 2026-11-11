"""Execute the BDS contact and sequential movement routines from an IDA export."""

import argparse
import json
import random
from pathlib import Path

from generate import INPUT_SHA256, HEAP, Machine, bits
import struct
import unicorn
from unicorn import x86_const as x86


def generate(data):
    if data["input_sha256"] != INPUT_SHA256 or data["input_name"] != "Windows Server 1.26.10":
        raise ValueError("Wrong BDS reference")
    machine = Machine(data["functions"])
    lines = ["# BDS collision conformance v1", f"# input_sha256 {INPUT_SHA256}"]
    for address in ("0x144795d10", "0x14103be00", "0x140bbd770", "0x140bbd140"):
        lines.append(f"# function {address} sha256 {data['functions'][address]['code_sha256']}")
    rng = random.Random(0xBD512610)
    shapes = [(0, 0, 0, 1, 1, 1), (-0.3, 0, -0.3, 0.3, 1.8, 0.3),
              (0, 0, 0, 0, 1, 1), (0, 0, 0, 1, 0, 1), (0, 0, 0, 1, 1, 0),
              (1, 0, 0, 0, 1, 1)]
    cases = []
    for stationary in shapes:
        for axis in range(3):
            for offset in (-1.25, -1, -0.9999995, -0.999999, -0.999998, -0.5,
                           0, 0.5, 0.999998, 0.999999, 0.9999995, 1, 1.25):
                moving = [0, 0, 0, 1, 1, 1]
                moving[axis] += offset
                moving[axis + 3] += offset
                for speed in (-1, -0.000001, -0.0, 0, 0.000001, 1):
                    velocity = [0, 0, 0]
                    velocity[axis] = speed
                    cases.append((stationary, moving, velocity))
    for _ in range(1000):
        low = [rng.uniform(-3, 3) for _ in range(3)]
        high = [value + rng.uniform(0.01, 3) for value in low]
        stationary = low + high
        low = [rng.uniform(-3, 3) for _ in range(3)]
        high = [value + rng.uniform(0.01, 3) for value in low]
        cases.append((stationary, low + high, [rng.uniform(-4, 4) for _ in range(3)]))
    for stationary, moving, velocity in cases:
        address = machine.call(0x144795D10, [HEAP, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300])
        values = tuple(bits(value) for value in (*stationary, *moving, *velocity))
        machine.put(HEAP + 0x100, values[:6])
        machine.put(HEAP + 0x200, values[6:12])
        machine.put(HEAP + 0x300, values[12:])
        machine.run(address)
        lines.append("C " + " ".join(f"{word:08x}" for word in (*values, *machine.get(HEAP, 8))))
    for index in range(512):
        moving = (-0.3, 0, -0.3, 0.3, 1.8, 0.3)
        velocity = tuple(rng.uniform(-1.5, 1.5) for _ in range(3))
        if index % 3:
            moves = ((0, velocity[1], 0), (velocity[0], 0, 0), (0, 0, velocity[2]))
        else:
            moves = (velocity,)
        colliders = []
        for _ in range(index % 7):
            low = [rng.uniform(-1, 1) for _ in range(3)]
            colliders.append((*low, *(value + rng.uniform(0.1, 2) for value in low)))
        limits = (0, 0, 0) if index % 2 else (1, 1, 1)
        address = machine.call(0x14103BE00, [HEAP, HEAP + 0x100, HEAP + 0x200,
                                           HEAP + 0x300, HEAP + 0x400])
        machine.qword(HEAP + 0x100, len(moves))
        machine.qword(HEAP + 0x108, HEAP + 0x500)
        machine.put(HEAP + 0x200, [bits(value) for value in moving])
        machine.qword(HEAP + 0x300, HEAP + 0x1000)
        machine.qword(HEAP + 0x308, HEAP + 0x1000 + len(colliders) * 24)
        machine.put(HEAP + 0x400, [bits(value) for value in limits])
        machine.put(HEAP + 0x500, [bits(value) for move in moves for value in move])
        machine.put(HEAP + 0x1000, [bits(value) for box in colliders for value in box])
        machine.run(address, budget=100000)
        values = (len(moves), len(colliders), *(bits(value) for value in (*moving, *limits)),
                  *(bits(value) for move in moves for value in move),
                  *(bits(value) for box in colliders for value in box), *machine.get(HEAP, 10))
        lines.append("M " + " ".join(f"{word:08x}" for word in values))
    for flags in range(32):
        for history in (False, True):
            for minimum in ((0, 0, 0), (-1, 0.25, 2), (0.125, 1, 0.75)):
                for custom in (False, True):
                    custom_minimum = (0.5, 3, -1)
                    address = machine.call(0x140BBD770, [0, HEAP + 0x100, HEAP + 0x200])
                    machine.put(HEAP + 0x100, [flags, *(bits(value) for value in minimum)])
                    machine.qword(HEAP + 0x110, HEAP + 0x1000)
                    machine.qword(HEAP + 0x118, HEAP + 0x1000 + (24 if history else 0))
                    machine.put(HEAP + 0x128, [bits(value) for value in custom_minimum])
                    machine.cpu.mem_write(HEAP + 0x134, bytes([custom]))
                    machine.run(address)
                    values = (flags, int(history), *(bits(value) for value in minimum),
                              int(custom), *(bits(value) for value in custom_minimum),
                              *machine.get(HEAP + 0x248, 3))
                    lines.append("R " + " ".join(f"{word:08x}" for word in values))
    for flags in range(32):
        for penetrated in (False, True):
            for push in (False, True):
                address = machine.call(0x140BBD140,
                                       [HEAP, HEAP + 0x100, HEAP + 0x200,
                                        HEAP + 0x300, HEAP + 0x400])
                machine.cpu.mem_write(HEAP + 0x154, bytes([penetrated]))
                machine.qword(HEAP + 0x208, (1 << 45) if push else 0)
                machine.put(HEAP + 0x300, [flags])
                machine.cpu.mem_write(HEAP + 0x334, bytes([1]))
                machine.qword(HEAP + 0x1008, HEAP + 0x1100)
                machine.qword(HEAP + 0x1010, HEAP + 0x1108)
                machine.qword(HEAP + 0x1100, HEAP + 0x1200)

                def lookup(cpu, pc, size, user):
                    sp = cpu.reg_read(x86.UC_X86_REG_RSP)
                    target = struct.unpack("<Q", cpu.mem_read(sp, 8))[0]
                    cpu.reg_write(x86.UC_X86_REG_RAX, HEAP + 0x1000)
                    cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
                    cpu.reg_write(x86.UC_X86_REG_RIP, target)

                hook = machine.cpu.hook_add(unicorn.UC_HOOK_CODE, lookup,
                                            begin=0x140BBA5C0, end=0x140BBA5C0)
                machine.run(address)
                machine.cpu.hook_del(hook)
                result = machine.get(HEAP + 0x300, 1)[0]
                custom = machine.cpu.mem_read(HEAP + 0x334, 1)[0]
                values = (flags, int(penetrated), int(push), result, custom)
                lines.append("U " + " ".join(f"{word:08x}" for word in values))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
