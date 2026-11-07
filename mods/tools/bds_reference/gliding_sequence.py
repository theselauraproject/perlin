"""Chain native glide, effect-refresh and expiry callbacks across 96-tick runs.

This verifies velocity/effect state transitions with supplied rotations in an
empty-world scenario. It does not execute collision, networking or the ECS
scheduler, and makes no claim about the rotation-history producer itself.
"""

import argparse
import json
from pathlib import Path

from generate import HEAP, INPUT_SHA256, Machine, bits
from rotation import SINE_SHA256, install_direction_dependencies


def generate(data, sine_path):
    if data["input_sha256"] != INPUT_SHA256 or data["input_name"] != "Windows Server 1.26.10":
        raise ValueError("Wrong BDS reference")
    machine = Machine(data["functions"])
    install_direction_dependencies(machine, sine_path)
    for address in (0x147337950, 0x147147150):
        if not any(begin <= address <= end for begin, end, permissions in machine.cpu.mem_regions()):
            machine.cpu.mem_map(address & ~4095, 4096)
    lines = ["# BDS chained gliding v1", f"# input_sha256 {INPUT_SHA256}",
             f"# sine_table sha256 {SINE_SHA256}", "# external CRT fmodf: host IEEE remainder on binary32 arguments"]
    for address in ("0x140c66f70", "0x143261390", "0x14387a590", "0x1410114b0"):
        lines.append(f"# function {address} sha256 {data['functions'][address]['code_sha256']}")

    def call(address, arguments):
        machine.run(machine.call(address, arguments, reset=False), budget=10000)

    velocities = ((0, 0, 0), (1, -0.5, 0.3), (8, 0.5, -8))
    for scenario, velocity in enumerate(velocities):
        machine.call(0, [])
        machine.put(HEAP + 0x718, [bits(v) for v in velocity])
        machine.put(HEAP + 0x600, [bits(11.25)])
        machine.qword(HEAP + 0x400, HEAP + 0x6000)
        machine.qword(HEAP + 0x408, HEAP + 0x6000 + 28 * 136)
        machine.qword(0x147337950, HEAP + 0xE000)
        machine.put(HEAP + 0xE008, [27])
        machine.put(0x147147150, [0])
        machine.qword(HEAP + 0x500, HEAP + 0x1000)
        machine.qword(HEAP + 0x1008, HEAP + 0x1100)
        machine.qword(HEAP + 0x1010, HEAP + 0x1108)
        machine.qword(HEAP + 0x1100, HEAP + 0x1200)
        machine.qword(HEAP + 0x1050, HEAP + 0x1300)
        machine.qword(HEAP + 0x1300, HEAP + 0x1400)
        machine.qword(HEAP + 0x1400, HEAP + 0x1500)
        machine.qword(HEAP + 0x1408, HEAP + 0x1520)
        machine.qword(HEAP + 0x1410, HEAP + 0x1520)
        machine.put(HEAP + 0x1500, [0, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0xFFFFFFFF])
        initial = (scenario, *(bits(v) for v in velocity), bits(11.25), 0, 0)
        lines.append("B " + " ".join(f"{v:08x}" for v in initial))
        previous = (0, 0)
        for tick in range(96):
            pitch = (-45.0, -0.0001, 30.0, 0.0, 89.999, -89.999)[(tick // 8 + scenario) % 6]
            yaw = ((tick * 17.25 + scenario * 45 + 180) % 360) - 180
            slow = 10 <= tick < 24 or 70 <= tick < 82
            events = ({0: 5, 2: 1, 15: 20, 16: 1, 45: 0},
                      {0: -1, 20: 3}, {30: 60, 60: 30})[scenario]
            duration = events.get(tick)
            source_tick = 0x100000000 + tick
            if duration is not None:
                machine.put(HEAP + 0x900, [source_tick & 0xFFFFFFFF, source_tick >> 32,
                                          duration & 0xFFFFFFFF, 0])
                call(0x14387A590, [HEAP + 0x800, HEAP + 0x1400, 0, 0, HEAP + 0x900, 1])
            machine.put(HEAP + 0x300, [bits(pitch), bits(yaw), *(bits(v) for v in previous)])
            machine.put(HEAP + 0x6000 + 27 * 136, [27 if slow else 0])
            call(0x140C66F70, [0, 0, HEAP + 0x300, HEAP + 0x400, HEAP + 0x500,
                                HEAP + 0x600, HEAP + 0x700])
            call(0x1410114B0, [0, HEAP + 0x1400])
            values = (int(slow), bits(pitch), bits(yaw), int(duration is not None),
                      (duration or 0) & 0xFFFFFFFF, source_tick & 0xFFFFFFFF, source_tick >> 32,
                      *machine.get(HEAP + 0x718, 3), *machine.get(HEAP + 0x600, 1),
                      *machine.get(HEAP + 0x1500, 4))
            lines.append("C " + " ".join(f"{v:08x}" for v in values))
            previous = (pitch, yaw)
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--sine", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text()), args.sine))
