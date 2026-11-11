"""Execute BDS movement-effect application, ticking, and swim-speed selection.

Storage has capacity for both valid BDS effect types. Context notifications and
outgoing packet construction are disabled by supplying null contexts, as the
native API allows; all effect-state decisions execute without substitutions.
"""

import argparse
import json
from pathlib import Path

from generate import HEAP, INPUT_SHA256, Machine


def generate(data):
    if data["input_sha256"] != INPUT_SHA256 or data["input_name"] != "Windows Server 1.26.10":
        raise ValueError("Wrong BDS reference")
    machine = Machine(data["functions"])
    lines = ["# BDS movement effects v1", f"# input_sha256 {INPUT_SHA256}"]
    for address in ("0x1410114b0", "0x140b6fd10", "0x14387a590"):
        lines.append(f"# function {address} sha256 {data['functions'][address]['code_sha256']}")
    durations = (-2147483648, -2, -1, 0, 1, 2, 3, 60, 2147483647)
    types = (-1, 0, 1, 2)

    def vector(length=2):
        machine.qword(HEAP + 0x200, HEAP + 0x1000)
        machine.qword(HEAP + 0x208, HEAP + 0x1000 + length * 16)
        machine.qword(HEAP + 0x210, HEAP + 0x1040)

    for first in durations:
        for first_type in types:
            for second in durations:
                for second_type in types:
                    entry = machine.call(0x1410114B0, [0, HEAP + 0x200])
                    vector()
                    values = [0x12345678, 0x9ABCDEF0, first & 0xFFFFFFFF, first_type & 0xFFFFFFFF,
                              0xFEDCBA98, 0x76543210, second & 0xFFFFFFFF, second_type & 0xFFFFFFFF]
                    machine.put(HEAP + 0x1000, values)
                    machine.run(entry)
                    result = (*values, *machine.get(HEAP + 0x1000, 8))
                    lines.append("T " + " ".join(f"{v:08x}" for v in result))
    for swimming in (False, True):
        for length in (0, 1, 2, 4):
            for effect_type in types:
                for duration in durations:
                    entry = machine.call(0x140B6FD10, [0, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300])
                    vector(length)
                    machine.qword(HEAP + 0x100, (1 << 57) if swimming else 0)
                    machine.put(HEAP + 0x1018, [duration & 0xFFFFFFFF, effect_type & 0xFFFFFFFF])
                    machine.run(entry)
                    result = (int(swimming), length, effect_type & 0xFFFFFFFF, duration & 0xFFFFFFFF,
                              *machine.get(HEAP + 0x300, 1))
                    lines.append("S " + " ".join(f"{v:08x}" for v in result))
    for index in (0, 1):
        for old in durations:
            for new in durations:
                for old_type in (-1, index):
                    for tick in (0, 0x123456789ABCDEF0):
                        entry = machine.call(0x14387A590,
                                             [HEAP + 0x300, HEAP + 0x200, 0, 0, HEAP + 0x400, 1])
                        vector()
                        values = [0, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0xFFFFFFFF]
                        values[index * 4:index * 4 + 4] = [0xDEADBEEF, 0xABCDEF01,
                                                          old & 0xFFFFFFFF, old_type & 0xFFFFFFFF]
                        machine.put(HEAP + 0x1000, values)
                        machine.put(HEAP + 0x400, [tick & 0xFFFFFFFF, tick >> 32, new & 0xFFFFFFFF, index])
                        machine.run(entry, budget=10000)
                        result = (index, old & 0xFFFFFFFF, old_type & 0xFFFFFFFF, new & 0xFFFFFFFF,
                                  tick & 0xFFFFFFFF, tick >> 32, *machine.get(HEAP + 0x1000, 8))
                        lines.append("A " + " ".join(f"{v:08x}" for v in result))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text())))
