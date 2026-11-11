"""Execute the complete BDS three-lane relative movement kernel.

Native x86 reexecution is required because Unicorn 2.1.4 propagates different
NaN payloads for some SSE operations. Finite results must agree independently.
"""

import argparse
import json
import random
import subprocess
import tempfile
from pathlib import Path

from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, Machine, bits
from native import compare_fixture


def generate(data):
    if data["input_sha256"] != INPUT_SHA256 or data["input_name"] != "Windows Server 1.26.10":
        raise ValueError("Wrong BDS reference")
    machine = Machine(data["functions"])
    if not any(begin <= 0x14712EC10 <= end for begin, end, permissions in machine.cpu.mem_regions()):
        machine.cpu.mem_map(0x14712E000, 4096)
    lines = ["# BDS relative movement v1", f"# input_sha256 {INPUT_SHA256}",
             "# complete kernel, bundled trig and rare helpers; no substituted calls"]
    for address in ("0x1432674c0", "0x146251e30", "0x146251ea0", "0x1462527e0"):
        lines.append(f"# function {address} sha256 {data['functions'][address]['code_sha256']}")
    yaw_values = (-1080, -359.999, -180, -90, -45, -0.001, -0.0, 0, 0.001, 33.3, 45, 90, 180, 359.999, 1080, 1e20)
    intents = ((0, 0, 0), (0.009999999, 0, 0), (0.01, 0, 0), (0.010000001, 0, 0),
               (-1, 0, 1), (0.98, 0, 0.98), (0.1, 1, -0.2), (0, -1, 0), (1.1, 0.42, -3.25))
    inputs = [(bits(yaw), *(bits(v) for v in intent), bits(speed), *(bits(v) for v in velocity))
              for yaw in yaw_values for intent in intents for speed in (0.0, 0.02, 0.1, -0.13, 0.54600006)
              for velocity in ((0, 0, 0), (-3.25, 0.42, 10))]
    rng = random.Random(0x12610C)
    inputs += [(rng.getrandbits(32), *(bits(rng.uniform(-2, 2)) for _ in range(3)),
                bits(rng.uniform(-1, 1)), *(bits(rng.uniform(-16, 16)) for _ in range(3))) for _ in range(256)]
    for unusual in (0x7F800000, 0xFF800000, 0x7FC12345, 0x7F812345, 0xFFC12345, 0xFF812345):
        ordinary = [bits(45), bits(0.98), 0, bits(0.98), bits(0.1), bits(-3.25), bits(0.42), bits(10)]
        for field in range(8):
            values = ordinary.copy()
            values[field] = unusual
            inputs.append(tuple(values))
    for first in (0x7FC12345, 0x7F812345, 0xFFCABCDE, 0xFF812345):
        for second in (0x7FC22222, 0x7F822222, 0xFFC22222, 0xFF822222):
            ordinary = [bits(45), bits(0.98), 0, bits(0.98), bits(0.1), bits(-3.25), bits(0.42), bits(10)]
            for fields in ((0, 5), (4, 5), (0, 4)):
                values = ordinary.copy()
                values[fields[0]], values[fields[1]] = first, second
                inputs.append(tuple(values))
    lines.append(f"# inputs {len(inputs)}; each CPU dispatch runs independently")
    for dispatch in (0, 2):
        for values in inputs:
            yaw, ix, iy, iz, speed, vx, vy, vz = values
            entry = machine.call(0x1432674C0, [HEAP, 0, 0, 0, iz, speed])
            machine.put(0x14712EC10, [dispatch])
            machine.put(HEAP, [vx, vy, vz])
            machine.cpu.reg_write(x86.UC_X86_REG_XMM1, yaw)
            machine.cpu.reg_write(x86.UC_X86_REG_XMM2, ix)
            machine.cpu.reg_write(x86.UC_X86_REG_XMM3, iy)
            machine.run(entry, budget=10000)
            result = (dispatch, *values, *machine.get(HEAP, 3))
            lines.append("M " + " ".join(f"{v:08x}" for v in result))
    return '\n'.join(lines) + '\n'


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--native-runner', required=True, type=Path)
    parser.add_argument('--native-pages', required=True, type=Path)
    args = parser.parse_args()
    emulated = generate(json.loads(args.export.read_text()))
    with tempfile.TemporaryDirectory(dir=args.out.parent) as temporary:
        fixture = Path(temporary) / 'emulated.txt'
        fixture.write_text(emulated)
        native = subprocess.run([str(args.native_runner.resolve()), str(args.native_pages.resolve()), str(fixture.resolve())],
                                check=True, text=True, capture_output=True).stdout
    differences = compare_fixture(emulated, native)
    args.out.write_text(f'# Native/emulator NaN-only field disagreements: {differences}; native results retained\n' + native)
