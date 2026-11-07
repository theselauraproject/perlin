"""Bake mob definitions into a c++ source:
gen_mobs.py <bedrock-samples dir> <out.cpp>

From behavior_pack/entities: size, health, movement speed, attack damage, families, the behaviours that pick an ai, tempt items;
from behavior_pack/loot_tables/entities: the item drops (first matching entries of each pool).
"""
import json
import os
import re
import sys

samples, out = sys.argv[1:3]
bp = os.path.join(samples, 'behavior_pack')

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jsonc import load

def find(entity, key):
    comps = entity.get('components', {})
    if key in comps:
        return comps[key]
    for group in entity.get('component_groups', {}).values():
        if key in group:
            return group[key]
    return None

def has(entity, key):
    return find(entity, key) is not None

def loot(path):
    if not path:
        return []
    full = os.path.join(bp, path)
    if not os.path.exists(full):
        return []
    drops = []
    allowed = {'random_chance', 'random_chance_with_looting', 'killed_by_player', 'killed_by_player_or_pets',
               'random_regional_difficulty_chance', 'random_difficulty_chance'}
    for pool in load(full).get('pools', []):
        if any(c.get('condition') not in allowed for c in pool.get('conditions', [])):
            continue
        for e in pool.get('entries', []):
            if e.get('type') != 'item':
                continue
            if any(c.get('condition') not in allowed for c in e.get('conditions', [])):
                continue
            lo, hi = 1, 1
            for f in e.get('functions', []):
                if f.get('function') == 'set_count':
                    c = f.get('count', 1)
                    if isinstance(c, dict):
                        lo, hi = int(c.get('min', 1)), int(c.get('max', 1))
                    else:
                        lo = hi = int(c)
            chance = 1.0
            for c in pool.get('conditions', []) + e.get('conditions', []):
                if c.get('condition') == 'random_chance':
                    chance = min(chance, float(c.get('chance', 1)))
                if c.get('condition') == 'random_chance_with_looting':
                    chance = min(chance, float(c.get('chance', 1)))
            drops.append('%s:%d:%d:%g' % (e['name'].replace('minecraft:', ''), lo, hi, chance))
            break
    return drops

FLAGS = ['hostile', 'panic', 'fly', 'aquatic', 'burns', 'explodes', 'ranged', 'neutral', 'baby_ok', 'spider']
rows = []
for f in sorted(os.listdir(os.path.join(bp, 'entities'))):
    try:
        doc = load(os.path.join(bp, 'entities', f))['minecraft:entity']
    except Exception:
        continue
    ident = doc['description']['identifier']
    families = (find(doc, 'minecraft:type_family') or {}).get('family', [])
    if 'mob' not in families and 'monster' not in families and 'animal' not in families:
        continue
    box = find(doc, 'minecraft:collision_box') or {'width': 0.6, 'height': 1.8}
    health = find(doc, 'minecraft:health') or {}
    hv = health.get('max', health.get('value', 10))
    if isinstance(hv, dict):
        hv = hv.get('range_max', hv.get('max', 10))
    move = find(doc, 'minecraft:movement') or {}
    speed = move.get('value', 0.25)
    if isinstance(speed, dict):
        speed = speed.get('range_max', 0.25)
    attack = find(doc, 'minecraft:attack') or {}
    damage = attack.get('damage', 0)
    if isinstance(damage, list):
        damage = max(damage)
    if isinstance(damage, dict):
        damage = damage.get('range_max', 2)
    flags = set()
    if 'monster' in families or has(doc, 'minecraft:behavior.nearest_attackable_target') and \
            not has(doc, 'minecraft:behavior.panic'):
        flags.add('hostile')
    if has(doc, 'minecraft:behavior.panic'):
        flags.add('panic')
    if has(doc, 'minecraft:can_fly') or has(doc, 'minecraft:navigation.fly') or has(doc, 'minecraft:navigation.hover'):
        flags.add('fly')
    if has(doc, 'minecraft:navigation.swim') or has(doc, 'minecraft:navigation.generic') and has(doc, 'minecraft:underwater_movement') \
            and not has(doc, 'minecraft:navigation.walk'):
        flags.add('aquatic')
    if 'aquatic' in families or 'fish' in families:
        flags.add('aquatic')
    if has(doc, 'minecraft:burns_in_daylight'):
        flags.add('burns')
    if has(doc, 'minecraft:explode') or ident == 'minecraft:creeper':
        flags.add('explodes')
    if has(doc, 'minecraft:behavior.ranged_attack') or has(doc, 'minecraft:shooter'):
        flags.add('ranged')
    if ('hostile' not in flags and (has(doc, 'minecraft:behavior.hurt_by_target') or
                                    has(doc, 'minecraft:behavior.melee_attack') or has(doc, 'minecraft:behavior.melee_box_attack'))):
        flags.add('neutral')
    if 'spider' in families or ident.endswith('spider'):
        flags.add('spider')
    tempt = find(doc, 'minecraft:behavior.tempt') or {}
    items = []
    for i in tempt.get('items', [])[:8]:
        if isinstance(i, dict):
            i = i.get('item', '')
        if isinstance(i, str):
            items.append(i.replace('minecraft:', ''))
    lt = (find(doc, 'minecraft:loot') or {}).get('table', '')
    mask = sum(1 << FLAGS.index(x) for x in flags)
    rows.append('        {"%s", %sf, %sf, %sf, %sf, %sf, %du, "%s", "%s", "%s"},' % (
        ident, *[repr(float(v)) for v in (box.get('width', 0.6), box.get('height', 1.8), hv, speed, damage)], mask,
        ','.join(items), ';'.join(loot(lt)), ','.join(families)))

lines = ['#include "world/MobData.hpp"', '', 'namespace perlin::server {', '    extern const MobDef kMobDefs[] = {']
lines += rows
lines += ['    };', f'    extern const int kMobDefCount = {len(rows)};', '}', '']
open(out, 'w', newline='\n').write('\n'.join(lines))
sys.stderr.write(f'{len(rows)} mobs\n')
