"""Execute numeric BDS routines with Unicorn; emit bit-exact conformance vectors.

Usage: python generate.py --export build/bds-reference.json --out tests/data/bds_numeric.txt --sine-out tests/data/bds_sine.bin
Requires unicorn==2.1.4. The export is produced by export_ida.py in IDA, not by
this script. The fixture contains inputs/outputs and hashes, never machine code.
"""

import argparse
import hashlib
import json
import random
import struct
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

INPUT_SHA256 = "f35469677a9d0db236e8559e9e72c4d6532230edf54497e88677eceff4cde539"
HEAP = 0x10000000
STACK = 0x20000000
RETURN = 0x30000000


def words(values):
    return struct.pack("<" + "I" * len(values), *values)


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


class Machine:
    def __init__(self, functions):
        self.cpu = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
        pages = {}
        for function in functions.values():
            for address, data in function["pages"].items():
                if address in pages and pages[address] != data:
                    raise ValueError(f"Inconsistent exported page: {address}")
                pages[address] = data
        for address, data in pages.items():
            self.cpu.mem_map(int(address, 16), 4096)
            self.cpu.mem_write(int(address, 16), bytes.fromhex(data))
        for address, function in functions.items():
            digest = hashlib.sha256()
            for start, end in function["chunks"]:
                digest.update(self.cpu.mem_read(start, end - start))
            if digest.hexdigest() != function["code_sha256"]:
                raise ValueError(f"Exported function bytes do not match their hash: {address}")
        self.cpu.mem_map(HEAP, 0x10000)
        self.cpu.mem_map(STACK, 0x10000)
        self.cpu.mem_map(RETURN, 4096)
        self.last_fault = None

        def invalid_memory(cpu, access, address, size, value, user):
            self.last_fault = (access, hex(address), size)
            return False

        self.cpu.hook_add(unicorn.UC_HOOK_MEM_INVALID, invalid_memory)

    def put(self, address, values):
        self.cpu.mem_write(address, words(values))

    def qword(self, address, value):
        self.cpu.mem_write(address, struct.pack("<Q", value))

    def get(self, address, count):
        return struct.unpack("<" + "I" * count, self.cpu.mem_read(address, count * 4))

    def call(self, address, arguments, reset=True):
        sp = STACK + 0xFFF8 - 0x100
        if reset:
            self.cpu.mem_write(HEAP, bytes(0x10000))
        self.cpu.mem_write(STACK, bytes(0x10000))
        self.cpu.reg_write(x86.UC_X86_REG_RSP, sp)
        self.cpu.reg_write(x86.UC_X86_REG_MXCSR, 0x1F80)
        self.qword(sp, RETURN)
        for register, value in zip((x86.UC_X86_REG_RCX, x86.UC_X86_REG_RDX,
                                    x86.UC_X86_REG_R8, x86.UC_X86_REG_R9), arguments):
            self.cpu.reg_write(register, value)
        for index, value in enumerate(arguments[4:]):
            self.qword(sp + 40 + index * 8, value)
        return address

    def run(self, address, budget=1000):
        try:
            self.cpu.emu_start(address, RETURN, count=budget)
        except unicorn.UcError as error:
            raise RuntimeError(f"BDS execution failed at {self.cpu.reg_read(x86.UC_X86_REG_RIP):#x}; "
                               f"memory fault {self.last_fault}") from error
        if self.cpu.reg_read(x86.UC_X86_REG_RIP) != RETURN:
            raise RuntimeError("BDS routine did not return within the instruction budget")


def generate(data, sine_out):
    if data["input_name"] != "Windows Server 1.26.10" or data["input_sha256"] != INPUT_SHA256:
        raise ValueError("Expected the audited Windows Server 1.26.10 binary")
    machine = Machine(data["functions"])
    lines = ["# BDS numeric conformance v1", f"# input_sha256 {INPUT_SHA256}",
             f"# unicorn {unicorn.__version__}; x86-64; Windows ABI; MXCSR 0x1f80"]
    for address, function in data["functions"].items():
        lines.append(f"# function {address} sha256 {function['code_sha256']}")
    scale = struct.unpack("<f", machine.cpu.mem_read(0x146C51680, 4))[0]
    sine_tables = []
    for routine in (0x146251EA0, 0x1462527E0):
        table = bytearray()
        for index in range(0, 65536, 4):
            address = machine.call(routine, [])
            angles = struct.pack("<4f", *((index + lane) * scale for lane in range(4)))
            machine.cpu.reg_write(x86.UC_X86_REG_XMM0, int.from_bytes(angles, "little"))
            machine.run(address)
            table.extend(machine.cpu.reg_read(x86.UC_X86_REG_XMM0).to_bytes(16, "little"))
        sine_tables.append(table)
    if sine_tables[0] != sine_tables[1]:
        raise RuntimeError("BDS SSE2/SSE4 sine tables disagree; CPU dispatch must be modeled")
    sine_out.write_bytes(sine_tables[0])
    lines.append(f"# sine_table sha256 {hashlib.sha256(sine_tables[0]).hexdigest()} entries 65536")
    sine_base = 0x1472B2560
    mapped_pages = {int(address, 16) for function in data["functions"].values()
                    for address in function["pages"]}
    for page in range(sine_base & ~4095, (sine_base + 0x40000 + 4095) & ~4095, 4096):
        if page not in mapped_pages:
            machine.cpu.mem_map(page, 4096)
    machine.cpu.mem_write(sine_base, bytes(sine_tables[0]))
    rng = random.Random(0x12610)
    boundary = [0, 0x80000000, bits(1), bits(-1), 0x33FFFFFF, 0x34000000,
                0x34000001, 0xB3FFFFFF, 0xB4000000, 0xB4000001,
                bits(0.42), bits(-3.25), bits(300)]
    vectors = [tuple(boundary[(i + j * 3) % len(boundary)] for j in range(4))
               for i in range(len(boundary))]
    vectors += [tuple(bits(rng.uniform(-20, 20)) for _ in range(4)) for _ in range(96)]
    for values in vectors:
        address = machine.call(0x140EB2C20, [0, HEAP, HEAP + 0x100, HEAP + 0x200])
        machine.put(HEAP, values[:3])
        machine.put(HEAP + 0x100, values[3:])
        machine.run(address)
        result = (*machine.get(HEAP, 3), *machine.get(HEAP + 0x100, 1),
                  *machine.get(HEAP + 0x200, 3))
        lines.append("I " + " ".join(f"{v:08x}" for v in (*values, *result)))
    for special in (False, True):
        for vertical in (False, True):
            for coefficient, multiplier in ((0.91, 0.6), (0.91, 1), (0.91, 0.98),
                                             (0.375, 0.91), (0, 1), (1, 1), (-0.5, 1)):
                for values in vectors:
                    address = machine.call(0x14103D9D0,
                                           [0, HEAP, 0, HEAP + 0x100,
                                            HEAP + 0x200, 0, HEAP + 0x300])
                    machine.put(HEAP + 28, [bits(coefficient)])
                    machine.cpu.mem_write(HEAP + 40, bytes([vertical]))
                    machine.put(HEAP + 0x200, [bits(multiplier)])
                    machine.put(HEAP + 0x318, values[:3])
                    machine.qword(HEAP + 0x100, HEAP + 0x400)
                    if special:
                        machine.qword(HEAP + 0x408, HEAP + 0x500)
                        machine.qword(HEAP + 0x410, HEAP + 0x508)
                        machine.qword(HEAP + 0x500, HEAP + 0x600)
                    machine.run(address)
                    result = machine.get(HEAP + 0x318, 3)
                    fields = (int(special), int(vertical), bits(coefficient), bits(multiplier),
                              *values[:3], *result)
                    lines.append("F " + " ".join(f"{v:08x}" for v in fields))
    for delay in (-2147483648, -20, -1, 0, 1, 2, 10, 2147483647):
        address = machine.call(0x140EB0DA0, [HEAP])
        machine.put(HEAP + 16, [delay & 0xFFFFFFFF])
        machine.run(address)
        lines.append(f"D {delay & 0xFFFFFFFF:08x} {machine.get(HEAP + 16, 1)[0]:08x}")
    yaws = [0, -0.0, 45, 90, 135, 180, 225, 270, 315, 360, -45, -90, -180, -270,
            -360, 33.3, -33.3, 720, -720, 36000, -36000]
    yaws += [rng.uniform(-720, 720) for _ in range(256)]
    for yaw in yaws:
        values = (bits(yaw), bits(rng.uniform(-2, 2)), bits(rng.uniform(-2, 2)))
        machine.call(0x140EAFD9C, [])
        machine.cpu.reg_write(x86.UC_X86_REG_R12, HEAP)
        machine.cpu.reg_write(x86.UC_X86_REG_RDI, HEAP + 0x100)
        machine.put(HEAP + 4, values[:1])
        machine.put(HEAP + 0x118, values[1:2])
        machine.put(HEAP + 0x120, values[2:3])
        indices = []

        def record_lookup(cpu, access, address, size, value, user):
            if sine_base <= address < sine_base + 0x40000:
                if size != 4 or (address - sine_base) % 4:
                    raise RuntimeError("Unexpected BDS sine lookup")
                indices.append((address - sine_base) // 4)

        hook = machine.cpu.hook_add(unicorn.UC_HOOK_MEM_READ, record_lookup)
        machine.cpu.emu_start(0x140EAFD9C, 0x140EAFDFA, count=100)
        machine.cpu.hook_del(hook)
        if machine.cpu.reg_read(x86.UC_X86_REG_RIP) != 0x140EAFDFA or len(indices) != 2:
            raise RuntimeError("BDS sprint impulse block did not finish")
        result = (*indices, *machine.get(HEAP + 0x118, 1), *machine.get(HEAP + 0x120, 1))
        lines.append("J " + " ".join(f"{v:08x}" for v in (*values, *result)))
    for variant, routine in enumerate((0x140E104A0, 0x140E105A0)):
        for block_kind in range(3):
            for exempt in (False, True):
                for friction in (0, 0.4, 0.6, 0.8, 0.98, 1, -0.5):
                    for speed in (0.1, 0.13, 0.027, 0.234567):
                        definition_friction = 0.7654321
                        address = machine.call(routine, [HEAP, HEAP + 0x100,
                                                         HEAP + 0x200, 0, exempt])
                        machine.cpu.reg_write(x86.UC_X86_REG_XMM3, bits(speed))
                        machine.qword(HEAP, HEAP + 0x300)
                        machine.qword(HEAP + 0x310, RETURN + 0x100)
                        machine.put(HEAP + 0x100, [bits(1.5), bits(3), bits(-3.5)])
                        machine.put(HEAP + 0x204, [bits(2.0625)])
                        machine.qword(HEAP + 0x458, HEAP + 0x500)
                        machine.put(HEAP + 0x4A0, [bits(friction)])
                        machine.put(HEAP + 0x6A0, [bits(definition_friction)])
                        machine.qword(0x147150F08, 0xA11)
                        machine.qword(HEAP + 0x5C8, (0xC0FFEE, 0x93E2BB1CB7F99FC6, 0xA11)[block_kind])
                        queries = []

                        def query(cpu, pc, size, user):
                            coordinates = cpu.reg_read(x86.UC_X86_REG_RDX)
                            queries.append(struct.unpack("<3i", cpu.mem_read(coordinates, 12)))
                            sp = cpu.reg_read(x86.UC_X86_REG_RSP)
                            return_address = struct.unpack("<Q", cpu.mem_read(sp, 8))[0]
                            cpu.reg_write(x86.UC_X86_REG_RAX, HEAP + 0x400)
                            cpu.reg_write(x86.UC_X86_REG_RSP, sp + 8)
                            cpu.reg_write(x86.UC_X86_REG_RIP, return_address)

                        hook = machine.cpu.hook_add(unicorn.UC_HOOK_CODE, query,
                                                    begin=RETURN + 0x100, end=RETURN + 0x100)
                        machine.run(address)
                        machine.cpu.hook_del(hook)
                        if queries != [(1, 1, -4)]:
                            raise RuntimeError(f"Unexpected BDS ground query: {queries}")
                        result = machine.cpu.reg_read(x86.UC_X86_REG_XMM0) & 0xFFFFFFFF
                        values = (variant, block_kind, int(exempt), bits(friction),
                                  bits(definition_friction), bits(speed), result)
                        lines.append("A " + " ".join(f"{v:08x}" for v in values))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--sine-out", required=True, type=Path)
    args = parser.parse_args()
    args.out.write_text(generate(json.loads(args.export.read_text()), args.sine_out))
