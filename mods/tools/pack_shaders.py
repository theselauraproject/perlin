"""Compile slang programs for every graphics api and pack them into one file the engine loads.

A program is one .slang file with entry points vsMain / fsMain (graphics) or csMain (compute). Each entry is compiled
to the requested targets; the uniform block layout comes from slang's reflection, so the engine copies named values
into the block without a hand-written mirror of the struct.

Pack layout (little endian):
  u32 magic 'PRSH', u32 version, u32 indexBytes, u32 programCount
  index: u16 textureSlotCount, per slot: str name, u8 slot, u8 flags (bit 0 = 2d array, bit 1 = point sampled only: depth and 32 bit float maps); then the programs
  per program: str name, u32 uniformBytes, u16 uniformCount, per uniform: str name, u32 offset, u32 bytes, u16 elements
               u8 stageCount, per stage: u8 stage, u8 targetCount, per target: u8 target, u32 offset, u32 bytes
  str = u16 length + utf-8 bytes; offsets are from the start of the data area, which follows the index
"""
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

MAGIC = 0x48535250  # 'PRSH'
VERSION = 1

STAGES = {'vertex': (0, 'vsMain'), 'fragment': (1, 'fsMain'), 'compute': (2, 'csMain')}
# target id, slangc target, extra arguments, file extension
TARGETS = {
    'spirv': (0, 'spirv', []),
    'dxil': (1, 'dxil', ['-profile', 'sm_6_0']),
    'hlsl': (2, 'hlsl', ['-profile', 'sm_5_0']),
    'msl': (3, 'metal', []),
    'wgsl': (4, 'wgsl', []),
}


def includes_of(path, dirs, seen):
    text = path.read_text(encoding='utf-8')
    for name in re.findall(r'^\s*#include\s+"([^"]+)"', text, re.M):
        for directory in [path.parent] + dirs:
            candidate = Path(directory) / name
            if candidate.exists():
                if candidate not in seen:
                    seen.add(candidate)
                    includes_of(candidate, dirs, seen)
                break
    return seen


def fingerprint(path, dirs, extra):
    h = hashlib.sha256(extra.encode())
    h.update(path.read_bytes())
    for inc in sorted(includes_of(path, dirs, set())):
        h.update(inc.read_bytes())
    return h.hexdigest()


class Packer:
    def __init__(self, slangc, dirs, cache, env):
        self.slangc = slangc
        self.dirs = [str(d) for d in dirs]
        self.cache = cache
        self.env = env
        self.version = subprocess.run([slangc, '-v'], capture_output=True, text=True, env=env).stdout.strip() or 'unknown'

    def compile(self, program, stage, target):
        stage_id, entry = STAGES[stage]
        target_id, slang_target, extra = TARGETS[target]
        key = fingerprint(program, [Path(d) for d in self.dirs], f'{self.version}|{stage}|{target}|{extra}')
        cached = self.cache / key if self.cache else None
        if cached and cached.exists():
            return cached.read_bytes()
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'out.bin'
            command = [self.slangc, str(program)]
            for d in self.dirs:
                command += ['-I', d]
            command += ['-target', slang_target] + extra + ['-entry', entry, '-stage', stage, '-matrix-layout-row-major', '-O2',
                                                           '-o', str(out)]
            result = subprocess.run(command, capture_output=True, text=True, env=self.env)
            if result.returncode != 0 or not out.exists():
                raise RuntimeError(f'{program.name} {stage} {target}:\n{result.stdout}{result.stderr}')
            data = out.read_bytes()
        # msl carries no workgroup size, and metal wants it at dispatch: leave it in a comment the backend reads
        if stage == 'compute' and slang_target == 'metal':
            size = self.reflect(program, stage)['entryPoints'][0]['threadGroupSize']
            data = f'// perlin:threads {size[0]} {size[1]} {size[2]}\n'.encode() + data
        if cached:
            self.cache.mkdir(parents=True, exist_ok=True)
            cached.write_bytes(data)
        return data

    def reflect(self, program, stage):
        stage_id, entry = STAGES[stage]
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'out.spv'
            refl = Path(tmp) / 'refl.json'
            command = [self.slangc, str(program)]
            for d in self.dirs:
                command += ['-I', d]
            command += ['-target', 'spirv', '-entry', entry, '-stage', stage, '-matrix-layout-row-major',
                        '-reflection-json', str(refl), '-o', str(out)]
            result = subprocess.run(command, capture_output=True, text=True, env=self.env)
            if result.returncode != 0:
                raise RuntimeError(f'reflection {program.name}:\n{result.stdout}{result.stderr}')
            return json.loads(refl.read_text())


def uniform_layout(reflection):
    """(total bytes, [(name, offset, bytes, elements)]) of the `Uniforms` constant buffer, or (0, [])"""
    for param in reflection.get('parameters', []):
        if param.get('name') != 'Uniforms':
            continue
        element = param['type']['elementType']
        total = element['sizes'][0]['value'] if element.get('sizes') else 0
        members = []
        for field in element['fields']:
            binding = field['binding']
            elements = field['type'].get('elementCount', 1) if field['type']['kind'] == 'array' else 1
            members.append((field['name'], binding['offset'], binding['size'], elements))
        return total, members
    return 0, []


def pack_str(text):
    raw = text.encode('utf-8')
    return struct.pack('<H', len(raw)) + raw


def build(slangc, dirs, programs, targets, cache, out, env, slots):
    packer = Packer(slangc, dirs, cache, env)
    jobs = []
    plan = []
    for program in programs:
        source = program.read_text(encoding='utf-8')
        stages = [s for s, (_, entry) in STAGES.items() if re.search(r'\b' + entry + r'\s*\(', source)]
        if not stages:
            raise RuntimeError(f'{program}: no vsMain/fsMain/csMain entry point')
        plan.append((program, stages))
        for stage in stages:
            for target in targets:
                jobs.append((program, stage, target))

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        compiled = dict(zip(jobs, pool.map(lambda j: packer.compile(*j), jobs)))
        reflected = {program: pool.submit(packer.reflect, program, stages[0]) for program, stages in plan}

    data = bytearray()
    index = bytearray()
    index += struct.pack('<H', len(slots))
    for name, (slot, dim, *flags) in slots.items():
        bits = (1 if dim == '2darray' else 0) | (2 if 'unfilterable' in flags else 0)
        index += pack_str(name) + struct.pack('<BB', slot, bits)
    index += struct.pack('<I', len(plan))
    for program, stages in plan:
        total, members = uniform_layout(reflected[program].result())
        index += pack_str(program.stem)
        index += struct.pack('<IH', total, len(members))
        for name, offset, size, elements in members:
            index += pack_str(name) + struct.pack('<IIH', offset, size, elements)
        index += struct.pack('<B', len(stages))
        for stage in stages:
            index += struct.pack('<BB', STAGES[stage][0], len(targets))
            for target in targets:
                blob = compiled[(program, stage, target)]
                while len(data) % 16:
                    data.append(0)
                index += struct.pack('<BII', TARGETS[target][0], len(data), len(blob))
                data += blob
    header = struct.pack('<IIII', MAGIC, VERSION, len(index), len(plan))
    out.write_bytes(header + bytes(index) + bytes(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--slangc', required=True)
    parser.add_argument('-I', dest='dirs', action='append', default=[])
    parser.add_argument('--slots', type=Path, required=True, help='texture_slots.json')
    parser.add_argument('--targets', default='spirv,dxil,hlsl,msl,wgsl')
    parser.add_argument('--cache', type=Path)
    parser.add_argument('--library-path', help='directory with libdxcompiler/libdxil for dxil output')
    parser.add_argument('-o', dest='output', type=Path, required=True)
    parser.add_argument('programs', nargs='+', type=Path)
    args = parser.parse_args()

    env = dict(os.environ)
    if args.library_path:
        for var in ('LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'PATH'):
            env[var] = args.library_path + os.pathsep + env.get(var, '')
    programs = []
    for item in args.programs:
        programs += sorted(item.glob('*.slang')) if item.is_dir() else [item]
    targets = args.targets.split(',')
    build(args.slangc, args.dirs, programs, targets, args.cache, args.output, env, json.loads(args.slots.read_text()))
    print(f'packed {len(programs)} programs x {len(targets)} targets -> {args.output} ({args.output.stat().st_size} bytes)')


if __name__ == '__main__':
    sys.exit(main())
