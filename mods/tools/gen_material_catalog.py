"""Generate a source catalog from a pinned mcbe-shader-codebase checkout."""
import argparse
import json
from pathlib import Path


def generate(source, output):
    rows = []
    for fragment in sorted((source / 'materials').glob('*/*.Fragment.glsl')):
        family = fragment.parent.name
        render_pass = fragment.name.removesuffix('.Fragment.glsl')
        vertex = fragment.with_name(render_pass + '.Vertex.glsl')
        if not vertex.is_file():
            continue
        rows.append('    {' + ', '.join(json.dumps(s) for s in (
            family, render_pass, vertex.relative_to(source).as_posix(),
            fragment.relative_to(source).as_posix())) + '},')
    if not rows:
        raise ValueError(f'No paired RenderDragon shader passes in {source}')
    output.parent.mkdir(parents=True, exist_ok=True)
    content = ('// Generated from mcbe-shader-codebase; do not edit.\n'
                      '#pragma once\nnamespace perlin::material {\n'
                      'struct SourcePass { const char* family; const char* pass; '
                      'const char* vertex; const char* fragment; };\n'
                      'inline constexpr SourcePass kSourcePasses[] = {\n' +
                      '\n'.join(rows) + '\n};\n}\n')
    if not output.exists() or output.read_text(encoding='utf-8') != content:
        output.write_text(content, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    generate(args.source, args.output)
