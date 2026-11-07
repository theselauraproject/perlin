"""Execute BDS's direction helper and install its external dependencies.

The sine table is the separately executed/verified BDS table. BDS imports
fmodf from the Windows CRT, whose bytes are absent from the IDB. That imported
IEEE remainder service alone is supplied by host fmod on binary32 arguments;
all wrapping, interpolation and direction arithmetic execute natively in BDS.
"""

import argparse
import hashlib
import json
import math
import random
import struct
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, Machine, bits

SINE_SHA256 = "5834520a8a07b71081b530851803cf051427446a99643b9de13368cc17635628"


def install_direction_dependencies(machine, sine_path):
    table = sine_path.read_bytes()
    if len(table) != 65536 * 4 or hashlib.sha256(table).hexdigest() != SINE_SHA256:
        raise ValueError("Wrong BDS sine table")
    regions = list(machine.cpu.mem_regions())
    base = 0x1472B2560
    for page in range(base & ~4095, (base + len(table) + 4095) & ~4095, 4096):
        if not any(begin <= page <= end for begin, end, permissions in regions):
            machine.cpu.mem_map(page, 4096)
    machine.cpu.mem_write(base, table)

    def fmodf(cpu, pc, size, user):
        x = struct.unpack('<f', struct.pack('<I', cpu.reg_read(x86.UC_X86_REG_XMM0) & 0xFFFFFFFF))[0]
        y = struct.unpack('<f', struct.pack('<I', cpu.reg_read(x86.UC_X86_REG_XMM1) & 0xFFFFFFFF))[0]
        cpu.reg_write(x86.UC_X86_REG_XMM0, bits(math.fmod(x, y)))
        sp = cpu.reg_read(x86.UC_X86_REG_RSP)
        target = struct.unpack('<Q', cpu.mem_read(sp, 8))[0]
        cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
        cpu.reg_write(x86.UC_X86_REG_RIP, target)

    machine.cpu.hook_add(unicorn.UC_HOOK_CODE, fmodf, begin=0x146254354, end=0x146254354)


def generate(data, sine_path):
    if data["input_sha256"] != INPUT_SHA256 or data["input_name"] != "Windows Server 1.26.10":
        raise ValueError("Wrong BDS reference")
    machine = Machine(data["functions"])
    install_direction_dependencies(machine, sine_path)
    lines = ["# BDS direction v1", f"# input_sha256 {INPUT_SHA256}",
             f"# sine_table sha256 {SINE_SHA256}", "# external CRT fmodf: host IEEE remainder on binary32 arguments"]
    address = "0x143261390"
    lines.append(f"# function {address} sha256 {data['functions'][address]['code_sha256']}")
    rng = random.Random(0x12610A)
    rotations = [(-90, -180), (-89.999, 180), (-30, -359.999), (-0.0, -0.0),
                 (0, 0), (0.001, 359.999), (30, 45), (89.999, 1080), (90, -1080)]
    inputs = [(p, y, cp, cy, alpha) for p, y in rotations for cp, cy in rotations
              for alpha in (0.0, 0.25, 1.0, 1.25)]
    inputs += [(rng.uniform(-90, 90), rng.uniform(-1080, 1080), rng.uniform(-90, 90),
                rng.uniform(-1080, 1080), rng.uniform(-0.25, 1.25)) for _ in range(512)]
    for values in inputs:
        entry = machine.call(0x143261390, [HEAP, HEAP + 0x100, HEAP + 0x200])
        words = [bits(v) for v in values]
        machine.put(HEAP + 0x100, words[:2])
        machine.put(HEAP + 0x200, words[2:4])
        machine.cpu.reg_write(x86.UC_X86_REG_XMM3, words[4])
        machine.run(entry, budget=10000)
        result = (*words, *machine.get(HEAP, 3))
        lines.append("R " + " ".join(f"{v:08x}" for v in result))
    lines.append(f"# function 0x140eafaf0 sha256 {data['functions']['0x140eafaf0']['code_sha256']} block 0x140eafd9c..0x140eafdfa")
    for yaw in (bits(11796480), bits(-11796480), bits(1e20), bits(-1e20),
                0x7F800000, 0xFF800000, 0x7FC12345, 0x7F812345, 0xFFC12345, 0x80000000):
        machine.call(0x140EAFD9C, [])
        machine.cpu.reg_write(x86.UC_X86_REG_R12, HEAP)
        machine.cpu.reg_write(x86.UC_X86_REG_RDI, HEAP + 0x100)
        machine.put(HEAP + 4, [yaw])
        indices = []

        def record_lookup(cpu, access, address, size, value, user):
            if 0x1472B2560 <= address < 0x1472F2560:
                if size != 4 or (address - 0x1472B2560) % 4:
                    raise RuntimeError("Unexpected BDS sine lookup")
                indices.append((address - 0x1472B2560) // 4)

        hook = machine.cpu.hook_add(unicorn.UC_HOOK_MEM_READ, record_lookup)
        machine.cpu.emu_start(0x140EAFD9C, 0x140EAFDFA, count=100)
        machine.cpu.hook_del(hook)
        if machine.cpu.reg_read(x86.UC_X86_REG_RIP) != 0x140EAFDFA or len(indices) != 2:
            raise RuntimeError("BDS sprint block did not finish")
        result = (yaw, *indices, *machine.get(HEAP + 0x118, 1), *machine.get(HEAP + 0x120, 1))
        lines.append("J " + " ".join(f"{v:08x}" for v in result))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--sine", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text()), args.sine))
