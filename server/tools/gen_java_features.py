"""Bake Java's vegetation and surface features into a c++ source:
gen_java_features.py <extracted client jar dir> <minecraft-data blocksJ2B.json> <out.cpp>

The jar's data/minecraft/worldgen (biome feature lists, placed and configured features, block state providers), the block tags they name and every biome's
temperature and mob spawns are converted to Bedrock block names with minecraft-data's Java to Bedrock state table, minified, deflated and embedded.
"""
import json
import os
import sys
import zlib

jar, j2b_path, out = sys.argv[1:4]
root = os.path.join(jar, 'data', 'minecraft')
wg = os.path.join(root, 'worldgen')
j2b = json.load(open(j2b_path))

by_block = {}
for key, value in j2b.items():
    name, _, rest = key.partition('[')
    props = {}
    for part in rest.rstrip(']').split(','):
        if '=' in part:
            k, v = part.split('=', 1)
            props[k] = v
    by_block.setdefault(name, []).append((props, value))

PREFER = {'waterlogged': 'false', 'half': 'lower', 'axis': 'y', 'facing': 'north', 'age': '0', 'persistent': 'false',
          'distance': '7', 'snowy': 'false', 'lit': 'false', 'powered': 'false', 'open': 'false', 'segment_amount': '1',
          'flower_amount': '1', 'stage': '0', 'hanging': 'false', 'tip': 'false', 'bottom': 'false', 'up': 'true',
          'down': 'true', 'north': 'true', 'south': 'true', 'east': 'true', 'west': 'true', 'type': 'bottom'}
missing = set()

def bedrock_state(name, props=None):
    if not name.startswith('minecraft:'):
        name = 'minecraft:' + name
    props = props or {}
    options = by_block.get(name)
    if not options:
        missing.add(name)
        return name + '[]'
    best = None
    best_score = -1
    for p, value in options:
        if any(p.get(k) != v for k, v in props.items() if k in p):
            continue
        score = sum(1 for k, v in p.items() if k not in props and PREFER.get(k) == v)
        if score > best_score:
            best, best_score = value, score
    if best is None:
        best = options[0][1]
    bname, _, rest = best.partition('[')
    parts = sorted(x for x in rest.rstrip(']').split(',') if x)
    return bname + '[' + ','.join(parts) + ']'

def bedrock_name(name):
    return bedrock_state(name).partition('[')[0]

def is_state(o):
    return isinstance(o, dict) and isinstance(o.get('id'), str) and set(o.keys()) <= {'id', 'properties'}

BLOCK_LIST_KEYS = {'blocks'}
STATE_STRING_KEYS = {'state', 'to_place', 'trunk_provider', 'foliage_provider', 'below_trunk_provider', 'dirt_provider',
                     'cap_provider', 'stem_provider', 'provider', 'block_provider', 'block', 'ground_state', 'source',
                     'default_state', 'fallback', 'then', 'data', 'root_provider', 'state_provider', 'contents',
                     'hanging_root_state_provider', 'muddy_roots_provider', 'leaves_provider', 'mangrove_roots_provider'}

def convert(o, key=None):
    if is_state(o):
        return {'b': bedrock_state(o['id'], o.get('properties'))}
    if isinstance(o, dict):
        return {k: convert(v, k) for k, v in o.items()}
    if isinstance(o, list):
        if key in BLOCK_LIST_KEYS:
            return [bedrock_name(x) if isinstance(x, str) and not x.startswith('#') else x for x in o]
        return [convert(x, key) for x in o]
    if isinstance(o, str) and key in BLOCK_LIST_KEYS and not o.startswith('#'):
        return bedrock_name(o)
    if isinstance(o, str) and key in STATE_STRING_KEYS and o.startswith('minecraft:') and o[10:] and \
            o in by_block:
        return {'b': bedrock_state(o)}
    return o

def walk_files(base):
    out = {}
    for r, _, files in os.walk(base):
        for f in files:
            if f.endswith('.json'):
                rel = os.path.relpath(os.path.join(r, f), base)[:-5].replace(os.sep, '/')
                out['minecraft:' + rel] = json.load(open(os.path.join(r, f)))
    return out

nether = {'nether_wastes', 'soul_sand_valley', 'crimson_forest', 'warped_forest', 'basalt_deltas', 'the_end',
          'end_highlands', 'end_midlands', 'small_end_islands', 'end_barrens', 'the_void'}

biomes = {}
for f in sorted(os.listdir(os.path.join(wg, 'biome'))):
    name = f[:-5]
    if name in nether:
        continue
    b = json.load(open(os.path.join(wg, 'biome', f)))
    spawns = b.get('attributes', {}).get('minecraft:gameplay/natural_mob_spawns', {}).get('argument', {}).get(
        'spawns_by_category', {})
    biomes[name] = {'features': b['features'], 'temperature': b.get('temperature', 0.5),
                    'frozen': b.get('temperature_modifier') == 'frozen', 'precipitation': b.get('has_precipitation', True),
                    'spawns': spawns}

placed = walk_files(os.path.join(wg, 'placed_feature'))
configured = walk_files(os.path.join(wg, 'feature'))
providers = walk_files(os.path.join(wg, 'block_state_provider'))

used_placed = set()
used_configured = set()

def visit_feature_ref(v):
    if isinstance(v, str):
        if v in placed:
            visit_placed(v)
        elif v in configured:
            visit_configured(v)
    elif isinstance(v, dict):
        if 'placement' in v and 'feature' in v:
            visit_feature_ref(v['feature'])
            visit_any(v['placement'])
        elif 'type' in v:
            visit_any(v)

def visit_placed(name):
    if name in used_placed:
        return
    used_placed.add(name)
    p = placed[name]
    f = p['feature']
    if isinstance(f, str):
        if f in configured:
            visit_configured(f)
    else:
        visit_any(f)
    visit_any(p.get('placement', []))

def visit_configured(name):
    if name in used_configured:
        return
    used_configured.add(name)
    visit_any(configured[name])

def visit_any(o):
    if isinstance(o, dict):
        for k, v in o.items():
            if k in ('feature', 'default', 'vegetation_feature', 'feature_true', 'feature_false'):
                visit_feature_ref(v)
            elif k == 'features' and isinstance(v, list):
                for e in v:
                    if isinstance(e, dict) and 'feature' in e and 'chance' in e:
                        visit_feature_ref(e['feature'])
                    elif isinstance(e, dict) and 'data' in e:
                        visit_feature_ref(e['data'])
                    else:
                        visit_feature_ref(e)
            else:
                visit_any(v)
    elif isinstance(o, list):
        for e in o:
            visit_any(e)

for b in biomes.values():
    for step, names in enumerate(b['features']):
        for n in names:
            visit_placed(n)

order = []
for step in range(11):
    edges = {}
    seen = []
    for b in biomes.values():
        names = b['features'][step] if step < len(b['features']) else []
        for i, n in enumerate(names):
            if n not in edges:
                edges[n] = set()
                seen.append(n)
            if i > 0:
                edges[n].add(names[i - 1])
    done = []
    state = {}

    def dfs(n):
        if state.get(n) == 2:
            return
        if state.get(n) == 1:
            return
        state[n] = 1
        for m in sorted(edges[n]):
            dfs(m)
        state[n] = 2
        done.append(n)

    for n in seen:
        dfs(n)
    order.append(done)

tags = {}
tag_dir = os.path.join(root, 'tags', 'block')
raw_tags = {}
for r, _, files in os.walk(tag_dir):
    for f in files:
        rel = os.path.relpath(os.path.join(r, f), tag_dir)[:-5].replace(os.sep, '/')
        raw_tags['minecraft:' + rel] = json.load(open(os.path.join(r, f)))['values']

def expand(tag, stack=()):
    if tag in tags:
        return tags[tag]
    result = set()
    for v in raw_tags.get(tag, []):
        if isinstance(v, dict):
            v = v.get('id', '')
        if v.startswith('#'):
            if v[1:] not in stack:
                result |= expand(v[1:], stack + (tag,))
        else:
            result.add(bedrock_name(v))
    tags[tag] = result
    return result

for t in raw_tags:
    expand(t)

doc = {
    'biomes': {k: {'temperature': v['temperature'], 'frozen': v['frozen'], 'precipitation': v['precipitation'],
                   'features': v['features'],
                   'spawns': v['spawns']} for k, v in biomes.items()},
    'order': order,
    'placed': {k: convert(placed[k]) for k in sorted(used_placed)},
    'configured': {k: convert(configured[k]) for k in sorted(used_configured)},
    'providers': {k: convert(v) for k, v in providers.items()},
    'tags': {k: sorted(v) for k, v in sorted(tags.items())},
}
text = json.dumps(doc, separators=(',', ':'), sort_keys=True).encode()
packed = zlib.compress(text, 9)
lines = ['#include <cstddef>', '#include <cstdint>', '', 'namespace perlin::server::vanilla {',
         f'    extern const size_t kJavaFeatureJsonSize = {len(text)};',
         f'    extern const size_t kJavaFeatureDataSize = {len(packed)};',
         '    extern const uint8_t kJavaFeatureData[] = {']
for i in range(0, len(packed), 24):
    lines.append('        ' + ','.join(str(b) for b in packed[i:i + 24]) + ',')
lines += ['    };', '}', '']
open(out, 'w', newline='\n').write('\n'.join(lines))
sys.stderr.write(f'{len(used_placed)} placed, {len(used_configured)} configured, {len(tags)} tags, {len(text)} bytes json, '
                 f'{len(packed)} deflated, {len(missing)} unmapped blocks: {sorted(missing)[:20]}\n')
