"""Execute BDS vector sine and sincos on exponent/mantissa boundaries.

Both CPU dispatch implementations and the complete rare-value helper run
without substitutions. Fixtures store float bits and numerical results only.
"""

import argparse
import json
import random
from pathlib import Path

from unicorn import x86_const as x86

from generate import INPUT_SHA256, Machine, bits


def generate(data):
    if data["input_sha256"] != INPUT_SHA256 or data["input_name"] != "Windows Server 1.26.10":
        raise ValueError("Wrong BDS reference")
    machine = Machine(data["functions"])
    if not any(begin <= 0x14712EC10 <= end for begin, end, permissions in machine.cpu.mem_regions()):
        machine.cpu.mem_map(0x14712E000, 4096)
    inputs = {0x7FC12345, 0x7F812345, 0xFFC12345, 0xFF812345}
    for exponent in range(256):
        for mantissa in (0, 1, 0x3FFFFF, 0x400000, 0x7FFFFE, 0x7FFFFF):
            for sign in (0, 0x80000000):
                inputs.add(sign | (exponent << 23) | mantissa)
    for boundary in (bits(0.0001220703125), bits(10000), bits(1.57079637), bits(3.14159274),
                     bits(6.28318548), bits(1e20), bits(1e30)):
        for adjacent in (-1, 0, 1):
            inputs.add(boundary + adjacent)
            inputs.add((boundary + adjacent) | 0x80000000)
    rng = random.Random(0x12610B)
    inputs.update(rng.getrandbits(32) for _ in range(512))
    lines = ["# BDS trig v1", f"# input_sha256 {INPUT_SHA256}",
             "# unicorn native SSE2/SSE4; no substituted calls; MXCSR 0x1f80"]
    for address in ("0x146251ea0", "0x1462527e0", "0x146251e30", "0x146253d80"):
        lines.append(f"# function {address} sha256 {data['functions'][address]['code_sha256']}")
    lines.append(f"# inputs {len(inputs)}")
    for word in sorted(inputs):
        sine = []
        for routine in (0x146251EA0, 0x1462527E0):
            entry = machine.call(routine, [])
            machine.cpu.reg_write(x86.UC_X86_REG_XMM0, word * ((1 << 96) + (1 << 64) + (1 << 32) + 1))
            machine.run(entry, budget=10000)
            result = machine.cpu.reg_read(x86.UC_X86_REG_XMM0)
            lanes = [(result >> (lane * 32)) & 0xFFFFFFFF for lane in range(4)]
            if len(set(lanes)) != 1:
                raise RuntimeError("Equal native input lanes produced different results")
            sine.append(lanes[0])
        lines.append("S " + " ".join(f"{v:08x}" for v in (word, *sine)))
        pair = []
        for dispatch in (0, 2):
            entry = machine.call(0x146251E30, [])
            machine.put(0x14712EC10, [dispatch])
            machine.cpu.reg_write(x86.UC_X86_REG_XMM0, word)
            machine.run(entry, budget=10000)
            result = machine.cpu.reg_read(x86.UC_X86_REG_XMM0)
            pair.extend((result & 0xFFFFFFFF, (result >> 32) & 0xFFFFFFFF))
        lines.append("C " + " ".join(f"{v:08x}" for v in (word, *pair)))
    return '\n'.join(lines) + '\n'


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
