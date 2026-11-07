"""Migrate four legacy BedSim traces using independently replayed native BDS sprint impulses.

No Perlin simulation code or Perlin-produced output is used. The unchanged,
pinned independent simulator must first reproduce all 1,800 original checks.
Only then is its sprint impulse replaced by executed BDS instructions.
"""

import argparse
import hashlib
import json
import os
import shlex
import subprocess
from pathlib import Path

import unicorn
from unicorn import x86_const as x86

from generate import HEAP, INPUT_SHA256, Machine

REFERENCE_REVISION = '6a18b0e95b184a9392d13bff9a69d713230a7135'
LEGACY_SHA256 = 'af656d988ce2031a4325a52bd1003d1920e303f5ed8c9a310f173b68ae922129'
SINE_SHA256 = '5834520a8a07b71081b530851803cf051427446a99643b9de13368cc17635628'
SCENARIOS = {'gen:terrain1_1', 'gen:floors1_0', 'gen:ladder1_0', 'gen:effects1_0'}
START, END, SINE_BASE = 0x140EAFD9C, 0x140EAFDFA, 0x1472B2560


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sections(text):
    result = {}
    current = None
    for line in text.splitlines(keepends=True):
        if line.startswith('S '):
            name = line.split()[1]
            current = name if name in SCENARIOS else None
            if current:
                if current in result:
                    raise ValueError('Duplicate scenario')
                result[current] = []
        if current:
            result[current].append(line)
    if set(result) != SCENARIOS:
        raise ValueError('Missing migration scenarios')
    return result


def frames(lines):
    result = {}
    for line in lines:
        fields = line.split()
        if fields[0] != 'E':
            continue
        key = fields[1], int(fields[2])
        if key in result or len(fields) != 13:
            raise ValueError('Invalid independent reference output')
        result[key] = fields[3:]
    if len(result) != 200:
        raise ValueError('Incomplete independent replay')
    return result


def migrate(args):
    reference = args.reference_dir.resolve()
    revision = subprocess.check_output(['git', '-C', str(reference), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != REFERENCE_REVISION:
        raise ValueError('Wrong independent reference revision')
    subprocess.run(['git', '-C', str(reference), 'diff', '--exit-code', 'HEAD', '--'], check=True,
                   stdout=subprocess.DEVNULL)
    legacy_bytes = args.legacy.read_bytes()
    if digest(legacy_bytes) != LEGACY_SHA256:
        raise ValueError('Original compatibility snapshot was modified')
    legacy = sections(legacy_bytes.decode())
    current_bytes = args.fixture.read_bytes()
    current = sections(current_bytes.decode())
    for name in SCENARIOS:
        if [s for s in legacy[name] if not s.startswith('E ')] != [s for s in current[name] if not s.startswith('E ')]:
            raise ValueError('Scenario inputs differ from the original snapshot')

    args.work_dir.mkdir(parents=True, exist_ok=True)
    work = args.work_dir.resolve()
    source = reference / 'src/physics.cpp'
    original = source.read_text()
    old = '''\t\t\tconst float radians = yaw * 0.017453292f;
\t\t\tvelocity.x -= mcSin(radians) * kSprintJumpBoost;
\t\t\tvelocity.z += mcCos(radians) * kSprintJumpBoost;'''
    if original.count(old) != 1:
        raise ValueError('Independent sprint implementation changed')
    patched = work / 'native_sprint_physics.cpp'
    patched.write_text('namespace perlin_bds_reference { void sprint(float, float&, float&); }\n' +
                       original.replace(old, '\t\t\tperlin_bds_reference::sprint(yaw, velocity.x, velocity.z);'))
    adapter = Path(__file__).with_name('legacy_trace_replay.cpp').resolve()
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    common = [*compiler, '-std=c++23', '-O2', '-ffp-contract=off', '-fno-fast-math',
              '-I' + str(reference / 'include'), str(adapter)]
    baseline_exe, native_exe = work / 'baseline', work / 'native'
    for implementation, executable in ((source, baseline_exe), (patched, native_exe)):
        subprocess.run([*common, str(implementation), str(reference / 'src/semantics.cpp'),
                        '-o', str(executable)], check=True)
    baseline = subprocess.run([str(baseline_exe), str(args.legacy.resolve()), 'validate'],
                              capture_output=True, text=True, check=True)
    print(baseline.stderr.strip())
    baseline_frames = frames(baseline.stdout.splitlines())

    data = json.loads(args.export.read_text())
    if data['input_sha256'] != INPUT_SHA256 or data['input_name'] != 'Windows Server 1.26.10':
        raise ValueError('Wrong BDS reference')
    machine = Machine(data['functions'])
    sine = args.sine.read_bytes()
    if digest(sine) != SINE_SHA256:
        raise ValueError('Wrong BDS sine table')
    for page in range(SINE_BASE & ~4095, (SINE_BASE + 0x40000 + 4095) & ~4095, 4096):
        if not any(lo <= page <= hi for lo, hi, _ in machine.cpu.mem_regions()):
            machine.cpu.mem_map(page, 4096)
    machine.cpu.mem_write(SINE_BASE, sine)
    records = ['# BDS sprint impulses observed during independent legacy-trace migration',
               f'# input_sha256 {INPUT_SHA256}', f'# independent_reference artemis-bedrock/bedsim-cpp {revision}',
               f"# function 0x140eafaf0 sha256 {data['functions']['0x140eafaf0']['code_sha256']}",
               '# instruction_range 0x140eafd9c 0x140eafdfa; MXCSR 0x1f80; no runtime hooks',
               '# J yaw inputX inputZ sineIndex cosineIndex outputX outputZ']
    native_lines = []
    process = subprocess.Popen([str(native_exe), str(args.legacy.resolve()), 'replay'], text=True,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=1)
    scenario, step, jumps = '', 0, 0
    try:
        for line in process.stdout:
            if line.startswith('S '):
                scenario, step = line.split()[1], 0
            elif line.startswith('E '):
                native_lines.append(line)
                step += 1
            elif line.startswith('Q '):
                words = [int(value, 16) for value in line.split()[1:]]
                if len(words) != 3:
                    raise ValueError('Invalid native impulse request')
                machine.call(START, [])
                machine.cpu.reg_write(x86.UC_X86_REG_R12, HEAP)
                machine.cpu.reg_write(x86.UC_X86_REG_RDI, HEAP + 0x100)
                machine.put(HEAP + 4, words[:1])
                machine.put(HEAP + 0x118, words[1:2])
                machine.put(HEAP + 0x120, words[2:])
                indices = []

                def lookup(cpu, access, address, size, value, user):
                    if SINE_BASE <= address < SINE_BASE + 0x40000:
                        if size != 4 or (address - SINE_BASE) % 4:
                            raise ValueError('Invalid BDS lookup')
                        indices.append((address - SINE_BASE) // 4)

                hook = machine.cpu.hook_add(unicorn.UC_HOOK_MEM_READ, lookup)
                machine.cpu.emu_start(START, END, count=100)
                machine.cpu.hook_del(hook)
                if machine.cpu.reg_read(x86.UC_X86_REG_RIP) != END or len(indices) != 2:
                    raise ValueError('Incomplete native sprint block')
                result = [*machine.get(HEAP + 0x118, 1), *machine.get(HEAP + 0x120, 1)]
                records.extend([f'# {scenario} step {step}',
                                'J ' + ' '.join(f'{v:08x}' for v in words + indices + result)])
                process.stdin.write(' '.join(f'{value:08x}' for value in result) + '\n')
                process.stdin.flush()
                jumps += 1
            else:
                raise ValueError('Unknown replay output')
        process.wait(timeout=30)
        stderr = process.stderr.read()
        if process.returncode or jumps != 7 or '1800 checks, 152 differences' not in stderr:
            raise RuntimeError(f'Incomplete native replay: {stderr}')
        print(stderr.strip())
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    native_frames = frames(native_lines)
    replacements = {}
    changed_fields = 0
    for name, lines in legacy.items():
        rewritten, index = [], 0
        for line in lines:
            if line.startswith('E '):
                key = name, index
                before, after = baseline_frames[key], native_frames[key]
                if any(before[axis] != after[axis] for axis in (1, 4, 6, 7, 8, 9)):
                    raise ValueError('Migration changed vertical motion or collision decisions')
                fields = line.split()
                changed = False
                for axis in (0, 2, 3, 5):
                    if before[axis] != after[axis]:
                        fields[axis + 1] = after[axis]
                        changed_fields += 1
                        changed = True
                if changed:
                    line = ' '.join(fields) + '\n'
                index += 1
            rewritten.append(line)
        replacements[name] = ''.join(rewritten)
    output = current_bytes.decode()
    for name in SCENARIOS:
        old_section = ''.join(current[name])
        if output.count(old_section) != 1:
            raise ValueError('Ambiguous scenario replacement')
        output = output.replace(old_section, replacements[name], 1)
    args.out.write_text(output)
    args.jump_out.write_text('\n'.join(records) + '\n')
    report = {'reference_revision': revision, 'bds_input_sha256': INPUT_SHA256,
              'legacy_sha256': LEGACY_SHA256, 'baseline_checks': 1800, 'frames': 200,
              'native_impulses': jumps, 'horizontal_fields_migrated': changed_fields,
              'output_sha256': digest(output.encode())}
    (work / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reference-dir', 'export', 'sine', 'legacy', 'fixture', 'out', 'jump-out'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--work-dir', type=Path, default=Path('build/bds-legacy-trace-migration'))
    migrate(parser.parse_args())
