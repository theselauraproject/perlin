import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from perlin_codegen.model import SchemaError
from perlin_codegen.parser import load
from perlin_codegen.backends import c, cpp, docs, host, json_ir

BACKENDS = {'c': c, 'cpp': cpp, 'json': json_ir, 'docs': docs, 'host': host}


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description='Generate the Perlin mod ABI from mods/api/*.papi')
    ap.add_argument('--root', default=here)
    ap.add_argument('--check', action='store_true', help='fail if any generated file is out of date')
    ap.add_argument('--only', action='append', choices=sorted(BACKENDS))
    args = ap.parse_args()
    try:
        api = load(os.path.join(args.root, 'api'))
    except SchemaError as e:
        print('schema error:', e, file=sys.stderr)
        return 2
    files = {}
    for name in args.only or sorted(BACKENDS):
        files.update(BACKENDS[name].generate(api))
    stale = []
    for rel, text in sorted(files.items()):
        path = os.path.join(args.root, rel)
        old = None
        if os.path.exists(path):
            with open(path, encoding='utf-8') as fh:
                old = fh.read()
        if old == text:
            continue
        stale.append(rel)
        if not args.check:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'w', encoding='utf-8', newline='\n') as fh:
                fh.write(text)
    if args.check and stale:
        print('out of date (run mods/codegen/generate.py):', *stale, sep='\n  ', file=sys.stderr)
        return 1
    for rel in stale:
        print('wrote', rel)
    return 0


if __name__ == '__main__':
    sys.exit(main())
