"""Bake the Bedrock ore features and their placement rules into a c++ source:
gen_features.py <bds features dir> <bds feature_rules dir> <bedrock-samples dir> <BiomeData.cpp> <out.cpp>

Sources, in the order the data rules ask for:
  - which biome has which tags: bedrock-samples' behavior_pack/biomes/*.biome.json (minecraft:tags)
  - the features and rules themselves: the dedicated server's versioned behavior packs (bedrock-samples ships none of them). The packs' brarchive files are
    unpacked and merged beforehand with tools/extract_bds_features.py, newer versions overriding older ones
Only ore_feature rules are baked; everything else in the packs (trees, flowers, structures, ...) is native code in Bedrock and not data.
Biome filters are evaluated here, once, for each biome the server can generate, so the server only tests a bit.
The output is deterministic and committed; nothing reads the sources at run time.
"""
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jsonc import load as load_jsonc

features_dir, rules_dir, samples, biome_cpp, out = sys.argv[1:6]

# biome identifiers in the server's biome order, from the generated table
src = open(biome_cpp).read()
start = src.index('kBiomes[] = {')
biome_ids = re.findall(r'\{"[a-z_]+", "([a-z_]+)", "minecraft:', src[start:])
if len(biome_ids) != 56:
    sys.exit('expected 56 biomes in %s, found %d' % (biome_cpp, len(biome_ids)))

tags = {}
for f in glob.glob(os.path.join(samples, 'behavior_pack', 'biomes', '*.biome.json')):
    j = load_jsonc(f)['minecraft:biome']
    ident = j['description']['identifier'].replace('minecraft:', '')
    tags[ident] = set(j['components'].get('minecraft:tags', {}).get('tags', []))
for b in biome_ids:
    if b not in tags:
        sys.exit('no tags for biome ' + b)

def test(node, biome):
    """one biome filter node: a test, or any_of / all_of of nodes; a list is an implicit all_of"""
    if isinstance(node, list):
        return all(test(n, biome) for n in node)
    if 'any_of' in node:
        return any(test(n, biome) for n in node['any_of'])
    if 'all_of' in node:
        return all(test(n, biome) for n in node['all_of'])
    if node.get('test') != 'has_biome_tag':
        sys.exit('unsupported biome test: %r' % (node,))
    has = node['value'] in tags[biome]
    op = node.get('operator', '==')
    if op in ('==', 'equals'):
        return has
    if op in ('!=', 'not'):
        return not has
    sys.exit('unsupported operator: ' + op)

STONES = {'granite': 'minecraft:granite', 'diorite': 'minecraft:diorite', 'andesite': 'minecraft:andesite'}
def place_name(pb):
    if isinstance(pb, str):
        return pb
    states = pb.get('states', {})
    if not states:
        return pb['name']
    if pb['name'] == 'minecraft:stone' and list(states) == ['stone_type'] and states['stone_type'] in STONES:
        return STONES[states['stone_type']]
    sys.exit('unsupported block states on ' + pb['name'])

ores = {}
for f in sorted(glob.glob(os.path.join(features_dir, '*.json'))):
    j = load_jsonc(f)
    v = j.get('minecraft:ore_feature')
    if not v:
        continue
    reps = []
    for r in v['replace_rules']:
        may = [m['name'] for m in r['may_replace']]
        if len(may) > 2:
            sys.exit('more than two replaceable blocks in ' + f)
        reps.append((place_name(r['places_block']), may))
    ores[v['description']['identifier']] = (v['count'], v.get('discard_chance_on_air_exposure', 0.0), reps)

PASSES = {'before_underground_pass': 0, 'underground_pass': 1, 'after_surface_pass': 2}
DISTS = {'uniform': 0, 'triangle': 1}
def dist(d):
    if isinstance(d, (int, float)):
        return (2, int(d), int(d))
    if isinstance(d, dict) and d.get('distribution') in DISTS:
        lo, hi = d['extent']
        return (DISTS[d['distribution']], int(lo), int(hi))
    sys.exit('unsupported distribution: %r' % (d,))

rules = []
for f in sorted(glob.glob(os.path.join(rules_dir, '*.json'))):
    j = load_jsonc(f)['minecraft:feature_rules']
    placed = j['description']['places_feature']
    if placed not in ores:
        continue
    cond = j['conditions']
    d = j['distribution']
    it = d.get('iterations', 1)
    if not isinstance(it, int):
        sys.exit('ore rule with a computed iteration count: ' + f)
    if it == 0:
        continue
    pass_name = cond.get('placement_pass')
    if pass_name not in PASSES:
        sys.exit('ore rule in an unknown pass: %s (%s)' % (pass_name, f))
    flt = cond.get('minecraft:biome_filter')
    mask = [0, 0]
    for i, b in enumerate(biome_ids):
        if flt is None or test(flt, b):
            mask[i // 64] |= 1 << (i % 64)
    sc = d.get('scatter_chance')
    num, den = (sc['numerator'], sc['denominator']) if isinstance(sc, dict) else (1, 1)
    order = d.get('coordinate_eval_order', 'xyz')
    rules.append((PASSES[pass_name], j['description']['identifier'].replace('minecraft:', ''), placed, it, dist(d['x']), dist(d['y']), dist(d['z']), order,
                  num, den, mask))
rules.sort(key=lambda r: (r[0], r[1]))

names = sorted(ores)
index = {n: i for i, n in enumerate(names)}
def s(v):
    return '"%s"' % v

lines = ['// generated by tools/gen_features.py from the dedicated server\'s behavior packs and bedrock-samples biome tags; do not edit',
         '#include "world/vanilla/FeatureData.hpp"', '', 'namespace perlin::server::vanilla {', '']
lines.append('    extern const int kOreFeatureCount = %d;' % len(names))
lines.append('    extern const OreFeatureDef kOreFeatures[] = {')
for n in names:
    count, discard, reps = ores[n]
    cells = []
    for place, may in reps:
        cells.append('{%s, %s, %s}' % (s(place), s(may[0]), s(may[1]) if len(may) > 1 else 'nullptr'))
    lines.append('        {%s, %d, %sf, %d, {%s}},' % (s(n.replace('minecraft:', '')), count, repr(float(discard)), len(reps), ', '.join(cells)))
lines.append('    };')
lines.append('')
lines.append('    extern const int kOreRuleCount = %d;' % len(rules))
lines.append('    extern const OreRuleDef kOreRules[] = {')
for pas, name, placed, it, x, y, z, order, num, den, mask in rules:
    lines.append('        {%s, %d, %d, %d, {%d, %d, %d}, {%d, %d, %d}, {%d, %d, %d}, %s, %d, %d, {0x%016Xull, 0x%016Xull}},' % (
        s(name), index[placed], pas, it, *x, *y, *z, s(order), num, den, mask[0], mask[1]))
lines.append('    };')
lines.append('}')
open(out, 'w').write('\n'.join(lines) + '\n')
print('%d ore features, %d rules' % (len(names), len(rules)))
