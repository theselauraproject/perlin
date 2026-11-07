"""Execute the complete BDS AutoStepSystem callback with synthetic ECS storage.

Host services provide allocation, memmove, free and finite float classification.
The registry returns an existing tag. All step and collision arithmetic executes
the BDS code, with no patched instructions or substituted movement routines.
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
    allocation = HEAP + 0x4000
    allocations = set()
    requested = False

    def runtime(cpu, pc, size, user):
        nonlocal allocation, requested
        a = cpu.reg_read(x86.UC_X86_REG_RCX)
        b = cpu.reg_read(x86.UC_X86_REG_RDX)
        if pc == 0x1454FAE40:
            if a <= 0 or a >= 4096 or allocation + a >= HEAP + 0xF000:
                raise RuntimeError("Unexpected oracle allocation")
            result = allocation
            allocations.add(result)
            allocation = (allocation + a + 15) & ~15
        elif pc == 0x146251860:
            size = cpu.reg_read(x86.UC_X86_REG_R8)
            if size >= 4096:
                raise RuntimeError("Unexpected oracle memmove size")
            cpu.mem_write(a, bytes(cpu.mem_read(b, size)))
            result = a
        elif pc == 0x146250390:
            if a not in allocations:
                raise RuntimeError("Unexpected oracle free")
            allocations.remove(a)
            result = 0
        elif pc == 0x146251B16:
            value = cpu.reg_read(x86.UC_X86_REG_XMM0) & 0xFFFFFFFF
            exponent = value & 0x7F800000
            if exponent == 0x7F800000:
                raise RuntimeError("Auto-step oracle encountered a non-finite value")
            result = 0 if (value & 0x7FFFFFFF) == 0 else (-2 if exponent == 0 else -1)
        elif pc == 0x140C2C180:
            if b != 2331328133:
                raise RuntimeError("Unexpected auto-step component lookup")
            result = HEAP + 0x2000
        elif pc == 0x140BEA420:
            if b != 1592658383:
                raise RuntimeError("Unexpected one-shot-step flag lookup")
            result = HEAP + 0x2000
        elif pc == 0x1403420E0:
            if b != 1534721851:
                raise RuntimeError("Unexpected auto-step request lookup")
            result = HEAP + 0x5000
        elif pc == 0x14030D2C0:
            if a != HEAP + 0x5000:
                raise RuntimeError("Unexpected auto-step tag insertion")
            requested = True
            result = 0
        elif pc == RETURN + 0x300:
            if a != HEAP + 0x2000:
                raise RuntimeError("Unexpected one-shot tag removal")
            machine.put(HEAP + 0x2200, [0xFFFFFFFF])
            result = 0
        else:
            raise RuntimeError("Unexpected runtime hook")
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = struct.unpack("<Q", cpu.mem_read(sp, 8))[0]
        cpu.reg_write(x86.UC_X86_REG_RAX, result & 0xFFFFFFFFFFFFFFFF)
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    for pc in (0x1454FAE40, 0x146251860, 0x146250390, 0x146251B16, 0x140C2C180,
               0x140BEA420, 0x1403420E0, 0x14030D2C0, RETURN + 0x300):
        machine.cpu.hook_add(unicorn.UC_HOOK_CODE, runtime, begin=pc, end=pc)
    lines = ["# BDS complete auto-step callback v1", f"# input_sha256 {INPUT_SHA256}"]
    for address in ("0x140c1c8e0", "0x14103be00", "0x144795d10", "0x140c1c5d0"):
        lines.append(f"# function {address} sha256 {data['functions'][address]['code_sha256']}")
    rng = random.Random(0xA1705126)
    for index in range(512):
        origin = (0, 1000.125, -1000.375)[index % 3]
        body_height = (1.8, 1.5, 0.6)[index % 3]
        moving = (origin - 0.3, 0, -0.3, origin + 0.3, body_height, 0.3)
        height = (0.5625, 0.25, 0.6, 1.0)[index % 4]
        limits = (0, 0, 0) if index % 2 else (1, 1, 1)
        if index < 256:
            velocity = ((0.1, 0.3, 0.6, -0.3)[index % 4], (-0.08, 0, -0.3, 0.2)[index % 4],
                        (0, 0.15, -0.4)[index % 3])
            obstacle_height = (0.25, 0.5, 0.5625, 0.6, 1.0)[index % 5]
            shapes = [(origin - 4, -1, -4, origin + 4, 0, 4),
                      (origin + 0.35, 0, -1, origin + 1.35, obstacle_height, 1)]
            if index % 5 == 0:
                shapes.append((origin - 1, body_height + 0.125, -1, origin + 2, body_height + 1, 1))
        else:
            velocity = tuple(rng.uniform(-1, 1) for _ in range(3))
            shapes = []
            for _ in range(index % 7):
                low = [rng.uniform(-1, 1) for _ in range(3)]
                low[0] += origin
                shapes.append((*low, *(value + rng.uniform(0.1, 2) for value in low)))
        if index % 2:
            shapes.reverse()
        address = machine.call(0x14103BE00, [HEAP, HEAP + 0x100, HEAP + 0x200,
                                           HEAP + 0x300, HEAP + 0x400])
        machine.qword(HEAP + 0x100, 3)
        machine.qword(HEAP + 0x108, HEAP + 0x500)
        machine.put(HEAP + 0x200, [bits(v) for v in moving])
        machine.qword(HEAP + 0x300, HEAP + 0x1000)
        machine.qword(HEAP + 0x308, HEAP + 0x1000 + len(shapes) * 24)
        machine.put(HEAP + 0x400, [bits(v) for v in limits])
        machine.put(HEAP + 0x500, [bits(v) for v in (0, velocity[1], 0, velocity[0], 0, 0, 0, 0, velocity[2])])
        machine.put(HEAP + 0x1000, [bits(v) for box in shapes for v in box])
        machine.run(address, budget=100000)
        normal = machine.get(HEAP, 10)
        address = machine.call(0x140C1C8E0,
                               [0, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300,
                                HEAP + 0x500, HEAP + 0x600, HEAP + 0x700])
        allocation = HEAP + 0x4000
        allocations.clear()
        machine.put(HEAP + 0x200, [bits(height)])
        machine.put(HEAP + 0x300, [bits(v) for v in moving])
        machine.put(HEAP + 0x330, [bits(v) for v in velocity])
        machine.put(HEAP + 0x33C, normal[:3])
        machine.put(HEAP + 0x348, [bits(v) for v in limits])
        machine.qword(HEAP + 0x358, HEAP + 0x1000)
        machine.qword(HEAP + 0x360, HEAP + 0x1000 + len(shapes) * 24)
        machine.put(HEAP + 0x500, normal[4:])
        machine.qword(HEAP + 0x600, HEAP + 0x1800)
        machine.qword(HEAP + 0x608, HEAP + 0x1800)
        machine.put(HEAP + 0x1000, [bits(v) for box in shapes for v in box])
        machine.qword(HEAP + 0x2008, HEAP + 0x2100)
        machine.qword(HEAP + 0x2010, HEAP + 0x2108)
        machine.qword(HEAP + 0x2100, HEAP + 0x2200)
        machine.run(address, budget=100000)
        if allocations:
            raise RuntimeError("BDS auto-step did not release its temporary shapes")
        values = (bits(height), len(shapes), *(bits(v) for v in (*moving, *velocity, *limits)),
                  *(bits(v) for box in shapes for v in box),
                  *machine.get(HEAP + 0x33C, 3), *machine.get(HEAP + 0x500, 6))
        lines.append("S " + " ".join(f"{v:08x}" for v in values))
    for height in (-0.1, 0, 0.5625, 1):
        for horizontal in (False, True):
            for initial_y, resolved_y in ((-0.08, -0.08), (-0.08, 0), (0.2, 0), (0, 0)):
                for ground in (False, True):
                    for ability in (False, True):
                        for always in (False, True):
                            desired = (1, initial_y, 0.1)
                            resolved = (0.25 if horizontal else 1, resolved_y, 0.1)
                            address = machine.call(0x140C1C5D0,
                                                   [HEAP, HEAP + 0x100, HEAP + 0x200,
                                                    HEAP + 0x300, HEAP + 0x400,
                                                    HEAP + 0x500, HEAP + 0x600])
                            machine.put(HEAP + 0x130, [bits(v) for v in desired])
                            machine.put(HEAP + 0x13C, [bits(v) for v in resolved])
                            machine.put(HEAP + 0x200, [bits(height)])
                            for handle, pool, present in ((0x300, 0x3000, ground),
                                                          (0x400, 0x4000, ability),
                                                          (0x500, 0x2000, always)):
                                machine.qword(HEAP + handle, HEAP + pool)
                                if present:
                                    machine.qword(HEAP + pool + 8, HEAP + pool + 0x100)
                                    machine.qword(HEAP + pool + 16, HEAP + pool + 0x108)
                                    machine.qword(HEAP + pool + 0x100, HEAP + pool + 0x200)
                            machine.qword(HEAP + 0x4050, HEAP + 0x4300)
                            machine.qword(HEAP + 0x4300, HEAP + 0x4400)
                            machine.put(HEAP + 0x4404, [int(ability)])
                            machine.qword(HEAP + 0x2000, HEAP + 0x6000)
                            machine.qword(HEAP + 0x6010, RETURN + 0x300)
                            requested = False
                            machine.run(address, budget=100000)
                            remaining = always and machine.get(HEAP + 0x2200, 1)[0] != 0xFFFFFFFF
                            values = (bits(height), *(bits(v) for v in (*desired, *resolved)),
                                      int(ground), int(ability), int(always), int(requested), int(remaining))
                            lines.append("T " + " ".join(f"{v:08x}" for v in values))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
