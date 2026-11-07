"""Execute the complete BDS GlideMove callback with synthetic component data."""

import argparse
import json
import random
from pathlib import Path

from generate import HEAP, INPUT_SHA256, Machine, bits
from rotation import SINE_SHA256, install_direction_dependencies


def generate(data, sine_path):
    if data["input_sha256"] != INPUT_SHA256 or data["input_name"] != "Windows Server 1.26.10":
        raise ValueError("Wrong BDS reference")
    machine = Machine(data["functions"])
    install_direction_dependencies(machine, sine_path)
    if not any(begin <= 0x147337950 <= end for begin, end, permissions in machine.cpu.mem_regions()):
        machine.cpu.mem_map(0x147337000, 4096)
    if not any(begin <= 0x147147150 <= end for begin, end, permissions in machine.cpu.mem_regions()):
        machine.cpu.mem_map(0x147147000, 4096)
    lines = ["# BDS gliding v1", f"# input_sha256 {INPUT_SHA256}",
             f"# sine_table sha256 {SINE_SHA256}", "# external CRT fmodf: host IEEE remainder on binary32 arguments"]
    for address in ("0x140c66f70", "0x143261390"):
        lines.append(f"# function {address} sha256 {data['functions'][address]['code_sha256']}")
    rotations = [(-90, 0, 0, 0), (-45, 45, 0, 0), (0, 0, 0, 0), (0, 180, 0, -180),
                 (30, 359.999, 45, -359.999), (90, -180, 89.999, 180),
                 (89.999, 33, -89.999, -87), (-0.0001, 1080, 0.0001, -1080),
                 (-89.999, 720, 89.999, -720)]
    velocities = [(-0.0, 0, -0.0), (0.1, -0.49999998, 0.2), (0.1, -0.5, 0.2),
                  (0.1, -0.50000006, 0.2), (1, -1, 0), (0, 0.42, 1), (8, 0, -8), (-3, 10, 4)]
    inputs = [(slow, mode, *rotation, *velocity, 11.25) for slow in (False, True)
              for mode in range(4) for rotation in rotations for velocity in velocities]
    rng = random.Random(0x12610F)
    inputs += [(bool(rng.randrange(2)), rng.randrange(4), rng.uniform(-90, 90), rng.uniform(-1080, 1080),
                rng.uniform(-90, 90), rng.uniform(-1080, 1080), *(rng.uniform(-16, 16) for _ in range(3)),
                rng.uniform(0, 128)) for _ in range(128)]
    for slow, mode, pitch, yaw, previous_pitch, previous_yaw, vx, vy, vz, fall in inputs:
        entry = machine.call(0x140C66F70,
                             [0, 0, HEAP + 0x300, HEAP + 0x400, HEAP + 0x500,
                              HEAP + 0x600, HEAP + 0x700])
        words = [bits(v) for v in (pitch, yaw, previous_pitch, previous_yaw, vx, vy, vz, fall)]
        machine.put(HEAP + 0x300, words[:4])
        machine.put(HEAP + 0x718, words[4:7])
        machine.put(HEAP + 0x600, words[7:])
        machine.qword(HEAP + 0x400, HEAP + 0x6000)
        machine.qword(HEAP + 0x408, HEAP + 0x6000 + 28 * 136)
        if slow:
            machine.put(HEAP + 0x6000 + 27 * 136, [27])
        machine.qword(0x147337950, HEAP + 0xE000)
        machine.put(HEAP + 0xE008, [27])
        machine.put(0x147147150, [0])
        machine.qword(HEAP + 0x500, HEAP + 0x1000)
        machine.qword(HEAP + 0x1008, HEAP + 0x1100)
        machine.qword(HEAP + 0x1010, HEAP + 0x1100 + (8 if mode else 0))
        machine.qword(HEAP + 0x1100, HEAP + 0x1200)
        machine.qword(HEAP + 0x1050, HEAP + 0x1300)
        machine.qword(HEAP + 0x1300, HEAP + 0x1400)
        machine.qword(HEAP + 0x1400, HEAP + 0x1500)
        machine.qword(HEAP + 0x1408, HEAP + 0x1500 + (16 if mode >= 2 else 0))
        machine.put(HEAP + 0x1500, [42, 0, 1, 0xFFFFFFFF if mode == 2 else 0])
        machine.run(entry, budget=10000)
        result = (int(slow), mode, *words, *machine.get(HEAP + 0x718, 3), *machine.get(HEAP + 0x600, 1))
        lines.append("G " + " ".join(f"{v:08x}" for v in result))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--sine", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text()), args.sine))
