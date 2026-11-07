"""Bake Bedrock's crafting and smelting recipes into a c++ source:
gen_recipes.py <bedrock-samples dir> <out.cpp>

One line per recipe:
  S|id|block|w|h|ingredient,...|output:count:data     shaped (ingredient "" is an empty cell, "#tag" a tag)
  L|id|block|ingredient*count,...|output:count:data   shapeless
  F|tags|input|output:count                            furnace, blast_furnace, smoker, campfire
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jsonc import load

samples, out = sys.argv[1:3]
folder = os.path.join(samples, 'behavior_pack', 'recipes')

def name(v):
    if isinstance(v, str):
        return v.replace('minecraft:', ''), 0
    if 'tag' in v:
        return '#' + v['tag'].replace('minecraft:', ''), 0
    item = v.get('item', '')
    if isinstance(item, dict):
        item = item.get('name', '')
    return item.replace('minecraft:', ''), int(v.get('data', 0) or 0)

def output(v):
    if isinstance(v, list):
        v = v[0]
    n, d = name(v)
    return '%s:%d:%d' % (n, int(v.get('count', 1)) if isinstance(v, dict) else 1, d)

lines = []
for f in sorted(os.listdir(folder)):
    try:
        doc = load(os.path.join(folder, f))
    except Exception:
        continue
    for kind, body in doc.items():
        if not kind.startswith('minecraft:recipe'):
            continue
        ident = body.get('description', {}).get('identifier', f[:-5]).replace('minecraft:', '')
        tags = body.get('tags', [])
        block = tags[0] if tags else 'crafting_table'
        if kind == 'minecraft:recipe_shaped':
            if 'crafting_table' not in tags:
                continue
            pattern = body['pattern']
            h = len(pattern)
            w = max(len(r) for r in pattern)
            cells = []
            for row in pattern:
                for c in row.ljust(w):
                    if c == ' ':
                        cells.append('')
                    else:
                        cells.append(name(body['key'][c])[0])
            lines.append('S|%s|%s|%d|%d|%s|%s' % (ident, 'crafting_table', w, h, ','.join(cells), output(body['result'])))
        elif kind == 'minecraft:recipe_shapeless':
            if 'crafting_table' not in tags:
                continue
            ings = []
            for i in body.get('ingredients', []):
                n, _ = name(i)
                ings.append('%s*%d' % (n, int(i.get('count', 1)) if isinstance(i, dict) else 1))
            lines.append('L|%s|%s|%s|%s' % (ident, 'crafting_table', ','.join(ings), output(body['result'])))
        elif kind == 'minecraft:recipe_furnace':
            inp, _ = name(body['input'])
            res = body['output']
            if isinstance(res, str):
                res = {'item': res}
            lines.append('F|%s|%s|%s' % (','.join(tags), inp, output(res)))

text = '\n'.join(lines)
chunks = [text[i:i + 2000] for i in range(0, len(text), 2000)]
src = ['namespace perlin::server {', '    extern const char* const kRecipeText[] = {']
for c in chunks:
    src.append('        R"PERLINRECIPES(' + c + ')PERLINRECIPES",')
src += ['        nullptr};', '}', '']
open(out, 'w', newline='\n').write('\n'.join(src))
sys.stderr.write('%d recipes\n' % len(lines))
