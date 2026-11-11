"""Turn a bgfx-dialect vertex/fragment pair into one slang program file.

The bodies keep their glsl/hlsl-flavoured spelling (see src/shaders/slang/compat.slang); this tool only
rewrites the parts that differ in slang: the `$input`/`$output` interface, `uniform` and SAMPLER declarations
(hoisted into one cbuffer and one set of texture bindings shared by both stages) and the entry points.
Each stage lives in its own namespace so the two `main` functions and their helpers never collide.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ATTRIBUTE_LOCATIONS = {
    'POSITION': 0, 'NORMAL': 1, 'COLOR0': 2, 'COLOR1': 3,
    'TEXCOORD0': 4, 'TEXCOORD1': 5, 'TEXCOORD2': 6, 'TEXCOORD3': 7,
}

BUILTIN_UNIFORMS = {
    'u_viewRect':       'vec4',
    'u_viewTexel':      'vec4',
    'u_modelViewProj':  'mat4',
    'u_viewProj':       'mat4',
    'u_view':           'mat4',
    'u_proj':           'mat4',
}


SLOTS = None


def slot_of(name):
    """texture slots are global by name, so every program binds a texture to the same place"""
    global SLOTS
    slots_path: str = 'src/shaders/slang/texture_slots.json'
    
    if not SLOTS:
        SLOTS = json.loads((Path(__file__).resolve().parent.parent / slots_path).read_text())

    if name not in SLOTS:
        raise KeyError(f"texture {name} has no entry in texture_slots.json")

    return SLOTS[name][0]


def read_varying(text):
    """name -> (type, semantic), in definition order."""
    result = {}
    for line in text.splitlines():
        line = line.split('//')[0].strip()
        match = re.match(r'(\w+)\s+(\w+)\s*:\s*(\w+)', line)
        if match:
            result[match.group(2)] = (match.group(1), match.group(3))
    return result


def expand(text, dirs, seen, inline=True):
    """Inline quoted/angled includes once each; bgfx_shader.sh is replaced by the compat prelude.

    With inline=False the includes stay as `#include "name.slang"` lines for headers already ported."""
    out = []
    for line in text.splitlines():
        match = re.match(r'\s*#include\s*[<"]([^>"]+)[>"]', line)
        if not match:
            out.append(line)
            continue
        name = match.group(1)
        if name == 'bgfx_shader.sh':
            continue
        if not inline:
            out.append('#include "' + re.sub(r'\.sh$', '.slang', name) + '"')
            continue
        if name in seen:
            continue
        seen.add(name)
        for directory in dirs:
            path = Path(directory) / name
            if path.exists():
                out.append(expand(path.read_text(encoding='utf-8'), dirs, seen, inline))
                break
        else:
            raise FileNotFoundError(f'include not found: {name}')
    return '\n'.join(out)


class Stage:
    def __init__(self, text, dirs, inline=True):
        self.inputs = []
        self.outputs = []
        lines = text.splitlines()
        body = []
        for line in lines:
            if line.startswith('$input'):
                self.inputs += [s.strip() for s in line[6:].split(',') if s.strip()]
            elif line.startswith('$output'):
                self.outputs += [s.strip() for s in line[7:].split(',') if s.strip()]
            else:
                body.append(line)
        self.text = expand('\n'.join(body), dirs, set(), inline)
        # kept includes land at file scope: their helpers are shared by both stages, not namespaced
        self.includes = re.findall(r'^#include "[^"]+"$', self.text, re.M)
        self.text = re.sub(r'^#include "[^"]+"\n?', '', self.text, flags=re.M)


UNIFORM = re.compile(r'^[ \t]*uniform\s+(\w+)\s+(\w+)\s*(?:\[\s*(\w+)\s*\])?\s*;[ \t]*$', re.M)
SAMPLER = re.compile(r'^[ \t]*(SAMPLER2DARRAY|SAMPLER2D)\(\s*(\w+)\s*,\s*(\d+)\s*\)\s*;[ \t]*$', re.M)


def hoist(stage, uniforms, samplers):
    def take_uniform(match):
        typ, name, count = match.groups()
        previous = uniforms.get(name)
        if previous and previous != (typ, count):
            raise ValueError(f'uniform {name} declared twice with different types')
        uniforms[name] = (typ, count)
        return ''

    def take_sampler(match):
        kind, name, slot = match.groups()
        samplers[name] = (kind, slot_of(name, int(slot)))
        return ''

    stage.text = UNIFORM.sub(take_uniform, stage.text)
    stage.text = SAMPLER.sub(take_sampler, stage.text)


def add_builtins(stages, uniforms):
    joined = '\n'.join(s.text for s in stages)
    for name, typ in BUILTIN_UNIFORMS.items():
        if typ and re.search(r'\b' + name + r'\b', joined) and name not in uniforms:
            uniforms[name] = (typ, None)
    models = [int(m) for m in re.findall(r'\bu_model\s*\[\s*(\d+)\s*\]', joined)]
    if models:
        uniforms['u_model'] = ('mat4', str(max(models) + 1))


def resource_block(uniforms, samplers):
    out = ['UNIFORMS_BEGIN']
    for name, (typ, count) in uniforms.items():
        out.append(f'    {typ} {name}' + (f'[{count}]' if count else '') + ';')
    out.append('UNIFORMS_END')
    out.append('')
    for name, (kind, slot) in sorted(samplers.items(), key=lambda kv: kv[1][1]):
        out.append(f'{kind}({name}, {slot * 2}, {slot * 2 + 1});')
    return out


def interface_struct(name, items, varying, attribute):
    varying_names = [n for n in varying if not n.startswith('a_')]
    lines = [f'struct {name}', '{']
    for item in items:
        typ, semantic = varying[item]
        if attribute:
            location = ATTRIBUTE_LOCATIONS[semantic]
        else:
            location = varying_names.index(item)
        lines.append(f'    [[vk::location({location})]] {typ} {item} : TEXCOORD{location};')
    lines.append('};')
    return '\n'.join(lines)


def convert(vs_text, fs_text, varying_text, dirs, header='', resources=None, inline=True, shared=None):
    varying = read_varying(varying_text)
    varying_names = [n for n in varying if not n.startswith('a_')]
    vs = Stage(vs_text, dirs, inline)
    fs = Stage(fs_text, dirs, inline)
    uniforms, samplers = {}, {}
    hoist(vs, uniforms, samplers)
    hoist(fs, uniforms, samplers)
    if resources is None:
        add_builtins([vs, fs], uniforms)

    for stage in (vs, fs):
        stage.text = re.sub(r'\bvoid\s+main\s*\(\s*\)', 'void stageMain()', stage.text)

    out = [header + '#include "compat.slang"', '']
    if resources:
        out.append(f'#include "{resources}"')
        # the shared header owns the uniform block; a program may only add textures of its own
        for name in uniforms:
            if name not in (shared or ()):
                raise ValueError(f'uniform {name} is not declared in {resources}')
        for name, (kind, slot) in sorted(samplers.items(), key=lambda kv: kv[1][1]):
            if name not in (shared or ()):
                out.append(f'{kind}({name}, {slot * 2}, {slot * 2 + 1});')
    elif resources is None:
        out += resource_block(uniforms, samplers)
    shared_includes = []
    for line in vs.includes + fs.includes:
        if line not in shared_includes and (not resources or resources not in line):
            shared_includes.append(line)
    out += shared_includes
    out.append('')

    out.append(interface_struct('VsIn', vs.inputs, varying, True))
    out.append('struct VsOut')
    out.append('{')
    out.append('    float4 clipPosition : SV_Position;')
    for item in vs.outputs:
        typ, _ = varying[item]
        location = varying_names.index(item)
        out.append(f'    [[vk::location({location})]] {typ} {item} : TEXCOORD{location};')
    out.append('};')
    out.append('struct FsIn')
    out.append('{')
    out.append('    float4 fragCoord : SV_Position;')
    for item in fs.inputs:
        typ, _ = varying[item]
        location = varying_names.index(item)
        out.append(f'    [[vk::location({location})]] {typ} {item} : TEXCOORD{location};')
    out.append('};')
    out.append('')

    out.append('namespace vs_stage')
    out.append('{')
    out.append('static vec4 gl_Position;')
    for item in vs.inputs + vs.outputs:
        out.append(f'static {varying[item][0]} {item};')
    out.append(vs.text)
    out.append('}')
    out.append('')
    out.append('namespace fs_stage')
    out.append('{')
    out.append('static vec4 gl_FragColor;')
    out.append('static vec4 gl_FragCoord;')
    for item in fs.inputs:
        out.append(f'static {varying[item][0]} {item};')
    out.append(fs.text)
    out.append('}')
    out.append('')

    out.append('[shader("vertex")]')
    out.append('VsOut vsMain(VsIn input)')
    out.append('{')
    for item in vs.inputs:
        out.append(f'    vs_stage::{item} = input.{item};')
    out.append('    vs_stage::stageMain();')
    out.append('    VsOut output;')
    out.append('    output.clipPosition = vs_stage::gl_Position;')
    for item in vs.outputs:
        out.append(f'    output.{item} = vs_stage::{item};')
    out.append('    return output;')
    out.append('}')
    out.append('')
    out.append('[shader("fragment")]')
    out.append('float4 fsMain(FsIn input) : SV_Target')
    out.append('{')
    out.append('    fs_stage::gl_FragCoord = input.fragCoord;')
    for item in fs.inputs:
        out.append(f'    fs_stage::{item} = input.{item};')
    out.append('    fs_stage::stageMain();')
    out.append('    return fs_stage::gl_FragColor;')
    out.append('}')
    return re.sub(r'\n{3,}', '\n\n', '\n'.join(out)) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vs', type=Path, required=True)
    parser.add_argument('--fs', type=Path, required=True)
    parser.add_argument('--varying', type=Path, required=True)
    parser.add_argument('-I', dest='dirs', action='append', default=[])
    parser.add_argument('-o', dest='output', type=Path, required=True)
    args = parser.parse_args()
    dirs = args.dirs + [str(args.vs.parent), str(args.fs.parent)]
    text = convert(args.vs.read_text(encoding='utf-8'), args.fs.read_text(encoding='utf-8'),
                   args.varying.read_text(encoding='utf-8'), dirs)
    args.output.write_text(text, encoding='utf-8')


if __name__ == '__main__':
    sys.exit(main())
