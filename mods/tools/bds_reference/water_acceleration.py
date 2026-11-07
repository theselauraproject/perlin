"""Execute the complete BDS WaterTravel callback with synthetic ECS inputs.

Player storage selection, optional attributes, dolphin/ground membership, and
the real depth-strider maximum-level method all run as BDS instructions.
"""

import argparse
import json
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
    lines = ["# BDS water acceleration v1", f"# input_sha256 {INPUT_SHA256}"]
    for address in ("0x140f1e690", "0x143972690"):
        lines.append(f"# function {address} sha256 {data['functions'][address]['code_sha256']}")
    values = ((0.1, 0.02, 0.3), (0.13, 0.04, 0.26), (0.0, -0.0, -0.42))
    for present in range(4):
        for player_mode in range(3):
            for dolphin in (False, True):
                for grounded in (False, True):
                    for multiplier in (0.0, 0.5, 1.0, 1.000001, 1.2, 2.0):
                        for level in (-1, 0, 1, 2, 3, 4, 255, 2147483647):
                            for movement, underwater, fallback in values:
                                entry = machine.call(0x140F1E690,
                                                     [HEAP, HEAP + 0x200, HEAP + 0x300,
                                                      HEAP + 0x400, HEAP + 0x500, HEAP + 0x600,
                                                      HEAP + 0x700, HEAP + 0x800, HEAP + 0x900])
                                machine.put(HEAP, [0, 6 if player_mode == 0 else 7])
                                machine.put(HEAP + 0x200, [bits(movement), int(bool(present & 1)),
                                                          bits(underwater), int(bool(present & 2))])
                                machine.put(HEAP + 0x300, [bits(fallback)])
                                machine.put(HEAP + 0x400, [bits(multiplier)])
                                machine.put(HEAP + 0x500, [level & 0xFFFFFFFF])
                                machine.qword(HEAP + 0x700, HEAP + 0x1000)
                                machine.qword(HEAP + 0x800, HEAP + 0x2000)
                                machine.qword(HEAP + 0x900, HEAP + 0x4000)
                                machine.qword(HEAP + 0x908, HEAP + 0x5000)
                                machine.put(HEAP + 0x4160, [7])
                                for pool, member in ((0x1000, dolphin), (0x2000, grounded),
                                                     (0x5000, player_mode == 2)):
                                    machine.qword(HEAP + pool + 8, HEAP + pool + 0x100)
                                    machine.qword(HEAP + pool + 16, HEAP + pool + 0x100 + (8 if member else 0))
                                    machine.qword(HEAP + pool + 0x100, HEAP + pool + 0x200)
                                machine.qword(0x1473FF070, HEAP + 0x7000)
                                machine.qword(HEAP + 0x7038, HEAP + 0x7100)
                                machine.qword(HEAP + 0x7100, HEAP + 0x7200)
                                machine.cpu.mem_write(HEAP + 0x7108, b'\x07')
                                machine.qword(HEAP + 0x7228, 0x143972690)
                                machine.run(entry, budget=10000)
                                row = (present, player_mode, int(dolphin), int(grounded), bits(multiplier),
                                       level & 0xFFFFFFFF, bits(movement), bits(underwater), bits(fallback),
                                       *machine.get(HEAP + 0x60C, 1))
                                lines.append("A " + " ".join(f"{v:08x}" for v in row))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
