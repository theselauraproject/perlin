"""Execute the complete BDS WaterDrag callback with supplied component values.

The depth-strider maximum comes from the real BDS enchantment virtual method,
using the ID established by the BDS registry initializer. ECS component inputs
are synthetic; their producer systems and the complete tick are not emulated.
"""

import argparse
import json
import random
from pathlib import Path

from generate import HEAP, INPUT_SHA256, Machine, bits


def generate(data):
    if data["input_sha256"] != INPUT_SHA256 or data["input_name"] != "Windows Server 1.26.10":
        raise ValueError("Wrong BDS reference")
    machine = Machine(data["functions"])
    pages = {int(address, 16) for function in data["functions"].values()
             for address in function["pages"]}
    if (0x1473FF070 & ~4095) not in pages:
        machine.cpu.mem_map(0x1473FF000, 4096)

    lines = ["# BDS water drag v1", f"# input_sha256 {INPUT_SHA256}"]
    for address in ("0x14103d110", "0x143972690"):
        lines.append(f"# function {address} sha256 {data['functions'][address]['code_sha256']}")
    rng = random.Random(0x12610D)
    vectors = [(0, 0, 0), (0x80000000,) * 3,
               (bits(0.42), bits(-0.08), bits(10)),
               (bits(-3.25), bits(0.5), bits(1e-6))]
    vectors += [tuple(bits(rng.uniform(-16, 16)) for _ in range(3)) for _ in range(5)]
    for grounded in (False, True):
        for sprint in (False, True):
            for custom in (None, 0.0, 0.25, 0.99):
                for multiplier in (0.0, 0.5, 1.0, 1.000001, 2.0):
                    for level in (-1, 0, 1, 2, 3, 255):
                        for velocity in vectors:
                            entry = machine.call(0x14103D110,
                                                 [0, HEAP + 0x100, HEAP + 0x200,
                                                  HEAP + 0x300, HEAP + 0x400,
                                                  HEAP + 0x500, HEAP + 0x600])
                            for handle, pool, present in ((0x100, 0x1000, grounded),
                                                          (0x200, 0x2000, custom is not None)):
                                machine.qword(HEAP + handle, HEAP + pool)
                                machine.qword(HEAP + pool + 8, HEAP + pool + 0x100)
                                machine.qword(HEAP + pool + 16, HEAP + pool + 0x100 + (8 if present else 0))
                                machine.qword(HEAP + pool + 0x100, HEAP + pool + 0x200)
                                machine.qword(HEAP + pool + 80, HEAP + pool + 0x300)
                                machine.qword(HEAP + pool + 0x300, HEAP + pool + 0x400)
                            machine.put(HEAP + 0x2400, [bits(custom or 0)])
                            machine.qword(HEAP + 0x300, 8 if sprint else 0)
                            machine.put(HEAP + 0x400, [bits(multiplier)])
                            machine.put(HEAP + 0x500, [level & 0xFFFFFFFF])
                            machine.put(HEAP + 0x618, velocity)
                            machine.qword(0x1473FF070, HEAP + 0x7000)
                            machine.qword(HEAP + 0x7038, HEAP + 0x7100)
                            machine.qword(HEAP + 0x7100, HEAP + 0x7200)
                            machine.cpu.mem_write(HEAP + 0x7108, b'\x07')
                            machine.qword(HEAP + 0x7228, 0x143972690)
                            machine.run(entry, budget=10000)
                            values = (int(grounded), int(sprint), int(custom is not None), bits(custom or 0),
                                      bits(multiplier), level & 0xFFFFFFFF, *velocity,
                                      *machine.get(HEAP + 0x618, 3))
                            lines.append("W " + " ".join(f"{v:08x}" for v in values))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
