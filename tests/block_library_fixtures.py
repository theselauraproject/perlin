import json, os, random, struct, sys, zlib, shutil

seed = int(sys.argv[1]); out = sys.argv[2]
res = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'resources')
rnd = random.Random(seed)
shutil.rmtree(out, ignore_errors=True)
rp = out + '/vanilla/resource_pack'
os.makedirs(rp + '/textures/blocks'); os.makedirs(out + '/vanilla/metadata/vanilladata_modules')
os.makedirs(out + '/vanilla/behavior_pack/blocks'); os.makedirs(rp + '/models/blocks')
os.makedirs(out + '/perlin')

def png(path, w, h, fn):
    raw = b''.join(b'\0' + b''.join(bytes(fn(x, y)) for x in range(w)) for y in range(h))
    def ch(t, d): c = struct.pack('>I', len(d)) + t + d; return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))

states = json.load(open(res + '/data/blocks/block_states.json'))
props = {}; items = {}
for _, name, st, *_ in states:
    items.setdefault(name, {})
    for k, v in st.items():
        kind, val = v[0], v[1]
        props.setdefault(k, {'t': kind, 'v': set()})['v'].add(val if kind != 'b' else bool(val))
        items[name][k] = 1
bp = [{'name': k, 'type': {'b': 'bool', 'i': 'int'}.get(p['t'], 'string'),
       'values': [{'value': x} for x in sorted(p['v'], key=str)]} for k, p in props.items()]
di = [{'name': n, 'serialization_id': ('tile.' if rnd.random() < .5 else '') + n[10:], 'properties': [{'name': k} for k in ps]}
      for n, ps in items.items()]
json.dump({'block_properties': bp, 'data_items': di}, open(out + '/vanilla/metadata/vanilladata_modules/mojang-blocks.json', 'w'))

texkeys = ['t%d' % i for i in range(70)] + ['grass_top', 'grass_side', 'leaves_oak', 'water_still', 'water_flow', 'kelp_a', 'kelp_b', 'kelp_c', 'kelp_d',
    'fire_0', 'fire_1', 'chain', 'tallgrass', 'cobweb']
files = {}
def mk(name, kind):
    d = rnd.choice([16, 16, 16, 32])
    def fn(x, y):
        if kind == 'hole': return [rnd.randrange(256), 120, 60, 0 if (x + y) % 3 == 0 else 255]
        if kind == 'trans': return [30, 90, 200, 140]
        if kind == 'overlay': return [90, 200, 60, 255 if y < 5 else 0]
        return [(x * 13 + y * 7 + len(name) * 11) % 256, (y * 17) % 256, (x * y) % 256, 255]
    h = d * rnd.choice([1, 1, 1, 4]) if kind == 'plain' else d
    png(rp + '/textures/blocks/%s.png' % name, d, h, lambda x, y: fn(x, y % d) if True else None)
    files[name] = kind
for i, k in enumerate(texkeys + ['extra_%d' % i for i in range(20)]):
    if rnd.random() < .1: continue
    mk(k, rnd.choice(['plain', 'plain', 'plain', 'hole', 'trans', 'overlay']))
mk('stone_opaque', 'plain'); mk('leaves_oak_opaque', 'plain'); mk('flip', 'plain')
avail = list(files)

td = {}
for k in texkeys:
    f = rnd.randrange(7)
    ref = lambda: 'textures/blocks/' + rnd.choice(avail + ['gone'])
    if f == 0: td[k] = {'textures': ref()}
    elif f == 1: td[k] = {'textures': [ref() for _ in range(rnd.randrange(1, 4))]}
    elif f == 2: td[k] = {'textures': {'path': ref(), 'overlay_color': '#79c05a'}}
    elif f == 3: td[k] = {'textures': [{'path': ref(), 'weight': 1, 'overlay_color': '#79c05a'}, ref()], 'tint_color': '#%02x%02x%02x' % (rnd.randrange(256), rnd.randrange(256), rnd.randrange(256))}
    elif f == 4: td[k] = ref()
    elif f == 5: td[k] = {'textures': [], 'tint_color': '#zzzzzz'}
    else: td[k] = {'textures': ref(), 'tint_color': '#4080c0'}
for k in ('kelp_a', 'kelp_b', 'kelp_c', 'kelp_d', 'water_still', 'water_flow', 'grass_top', 'leaves_oak'):
    td[k] = {'textures': 'textures/blocks/' + rnd.choice(avail)}
json.dump({'texture_data': td} if rnd.random() < .7 else td, open(rp + '/textures/terrain_texture.json', 'w'))
json.dump([{'atlas_tile': 'fire_0', 'flipbook_texture': 'textures/blocks/flip'}, {'atlas_tile': 'fire_1', 'flipbook_texture': 'textures/blocks/missing'}, 5], open(rp + '/textures/flipbook_textures.json', 'w'))

allk = texkeys + ['nonlisted', 'extra_3', 'path:textures/blocks/extra_5', 'stone_opaque']
def tex():
    r = rnd.random()
    return rnd.choice(allk)
bj = {}
for n in items:
    key = n[10:]
    if rnd.random() < .08: continue
    e = {}
    r = rnd.randrange(6)
    if r == 0: e['textures'] = tex()
    elif r == 1: e['textures'] = {'up': tex(), 'down': tex(), 'side': tex()}
    elif r == 2: e['textures'] = {'top': tex(), 'east': tex(), 'north': tex()}
    elif r == 3: e['textures'] = {'up': tex(), 'side': tex()}
    elif r == 4: e['textures'] = {'down': tex()}
    else: e = rnd.choice([{'sound': 'x'}, {}, {'sound': 'stone'}])
    if rnd.random() < .3: e['carried_textures'] = tex() if rnd.random() < .5 else {'side': tex()}
    if rnd.random() < .15: e['isotropic'] = True
    if rnd.random() < .1: e['ambient_occlusion_exponent'] = 0.5
    bj[key] = e
for k, v in list(bj.items())[:3]: bj['alias_' + k] = v
bj['chain'] = {'textures': 'chain'}
bj['kelp'] = {'textures': 'kelp_a'}
json.dump(bj, open(rp + '/blocks.json', 'w'))

names = list(items)
cb = {'aliases': {}, 'tints': {}, 'blocks': {}}
for _ in range(8): cb['aliases'][rnd.choice(names)[10:]] = rnd.choice(list(bj))
for k in rnd.sample(texkeys, 12): cb['tints'][k] = [round(rnd.random(), 2) for _ in range(3)]
for n in rnd.sample(names, 60):
    e = {}
    r = rnd.randrange(5)
    if r == 0: e['texture'] = tex()
    elif r == 1: e = {'top': tex(), 'side': tex(), 'bottom': tex()}
    elif r == 2: e = {'side': tex()}
    elif r == 3: e = {'tint_faces': rnd.sample(['top', 'side', 'bottom', 'zz'], rnd.randrange(0, 4)), 'cutout': rnd.random() < .5}
    else: e = {'tint_faces': ['top'], 'tint': [0.2, 0.5, 0.9], 'top': tex()}
    if rnd.random() < .4: e['cutout'] = rnd.random() < .5
    cb['blocks'][n] = e
if seed % 5 == 4: cb = {'a': 1}
if seed % 7 == 6: open(out + '/perlin/blocks.json', 'w').write('{broken')
elif seed % 3 != 0: json.dump(cb, open(out + '/perlin/blocks.json', 'w'))
if seed % 4 == 1: json.dump({'flat_contains': ['_slab', 'rail', 3, '']}, open(out + '/perlin/shapes.json', 'w'))
if seed % 6 == 5: open(out + '/perlin/shapes.json', 'w').write('nope')

geo = {'format_version': '1.12.0', 'minecraft:geometry': [{'description': {'identifier': 'geometry.g1', 'texture_width': 16, 'texture_height': 16},
       'bones': [{'name': 'b', 'cubes': [{'origin': [-8, 0, -8], 'size': [16, 8, 16], 'uv': [0, 0]}]}]}]}
json.dump(geo, open(rp + '/models/blocks/g1.geo.json', 'w'))
for i in range(4):
    gn = rnd.choice(names)
    json.dump({'minecraft:block': {'description': {'identifier': 'test:geo%d' % i}, 'components': {'minecraft:geometry': 'geometry.g1',
        'minecraft:material_instances': {'*': {'texture': rnd.choice(texkeys), 'render_method': 'opaque'}}},
        'permutations': [{'condition': "query.block_state('x') == 1", 'components': {'minecraft:geometry': 'geometry.g1'}}]}},
        open(out + '/vanilla/behavior_pack/blocks/g%d.json' % i, 'w'))
if seed % 2:
    extra = out + '/perlin/packs/p1'; os.makedirs(extra + '/models'); os.makedirs(extra + '/blocks'); os.makedirs(extra + '/textures')
    json.dump({'texture_data': {'q': {'textures': 'textures/q'}}}, open(extra + '/textures/terrain_texture.json', 'w'))
    png(extra + '/textures/q.png', 16, 16, lambda x, y: [x * 16, y * 16, 9, 255])
    json.dump(geo, open(extra + '/models/g.geo.json', 'w'))
    json.dump({'minecraft:block': {'description': {'identifier': 'p:b'}, 'components': {'minecraft:geometry': 'geometry.g1', 'minecraft:material_instances': {'*': {'texture': 'q'}}}}}, open(extra + '/blocks/b.json', 'w'))

pk = out + '/packs/Fake/textures/blocks'; os.makedirs(pk)
for k in rnd.sample(avail, min(len(avail), 30)):
    d = 64
    png(pk + '/%s.png' % k, d, d, lambda x, y: [(x * 3) % 256, (y * 5) % 256, 77, 255 if (x + y) % 7 else 0])
for k in ('t1', 't2', 'grass_top'):
    png(pk + '/%s.png' % k, 64, 64, lambda x, y: [200, 30, 30, 255])
