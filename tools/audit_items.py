#!/usr/bin/env python3
"""Audits how every Java item is drawn as an icon / held item.

  python3 tools/audit_items.py --jar minecraft-<ver>-client.jar [--sheets OUTDIR]

Checks, against resources/data:
  * every Bedrock id (block_states.json + items/extra_items.txt) maps to a Java item
    (itemdisplay::JavaItemName, parsed from ItemDisplay.cpp);
  * every "block" item in item_display.json has an inventory mesh in block_models.bin (IMOD);
  * every texture those meshes use was matched to a Bedrock texture exactly (tools/modelgen/texmap.json,
    texalias.json): a fuzzy match means the icon shows the wrong texture;
  * with --sheets, renders every block item's icon the way UiAssets::RasterIcon does (Java gui transform,
    Java GUI lights) with the jar's textures, 192 per PNG, for a visual pass.
"""
import argparse, io, json, math, os, re, struct, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_models(path):
    d = open(path, 'rb').read()
    p = 4

    def g(fmt):
        nonlocal p
        v = struct.unpack_from('<' + fmt, d, p)
        p += struct.calcsize('<' + fmt)
        return v
    n, = g('I')
    tex = []
    for _ in range(n):
        l, = g('H')
        tex.append(d[p:p + l].decode())
        p += l
    nv, = g('I')
    var = []
    for _ in range(nv):
        nq, = g('H')
        qs = []
        for _ in range(nq):
            dr, sd, cull, tint, shade, light, t = g('BBbbBBH')
            pos = [g('3f') for _ in range(4)]
            uv = [g('2f') for _ in range(4)]
            qs.append((dr, tint, t, pos, uv))
        var.append(qs)
    ne, = g('I')
    ent = []
    for _ in range(ne):
        fl, ng = g('BB')
        gs = []
        for _ in range(ng):
            nc, = g('B')
            gs.append([g('IB') for _ in range(nc)])
        ent.append(gs)
    nb, = g('I')
    p += 8 * nb
    items = {}
    if p < len(d) and d[p:p + 4] == b'IMOD':
        p += 4
        ni, = g('I')
        for _ in range(ni):
            l, = g('H')
            nm = d[p:p + l].decode()
            p += l
            e, = g('I')
            items[nm] = e
    return tex, var, ent, items


def java_name_fn():
    src = open(os.path.join(ROOT, 'src/client/systems/sprites/ItemDisplay.cpp')).read()
    body = src[src.index('kRenamed = {'):src.index('const auto it = kRenamed.find')]
    ren = dict(re.findall(r'\{"([^"]+)", "([^"]+)"\}', body))

    def jname(n):
        n = n.removeprefix('minecraft:')
        if n == 'skull':
            return 'skeleton_skull'
        if n.startswith('light_block'):
            return 'light'
        return ren.get(n, n)
    return jname


def model_textures(jar, model):
    chain, n = [], model
    while n:
        n = n.removeprefix('minecraft:')
        try:
            m = json.loads(jar.read(f'assets/minecraft/models/{n}.json'))
        except KeyError:
            break
        chain.append(m)
        n = m.get('parent')
    t = {}
    for m in reversed(chain):
        t.update({a: (b['sprite'] if isinstance(b, dict) else b) for a, b in m.get('textures', {}).items()})
    els = next((m['elements'] for m in chain if 'elements' in m), [])
    used = set()
    for e in els:
        for f in e.get('faces', {}).values():
            r = f['texture']
            for _ in range(10):
                if r.startswith('#'):
                    r = t.get(r[1:], r)
            used.add(r.removeprefix('minecraft:'))
    return used


def render_sheets(jar, out, tex, var, ent, items, disp, texmap, alias):
    import numpy as np
    from PIL import Image, ImageDraw
    inv = {}
    for k, v in sorted(texmap.items(), key=lambda kv: -kv[1]['diff']):
        inv[v['path']] = k
    for k, v in alias.items():
        inv[v] = k
    cache = {}

    def tload(t):
        if t not in cache:
            img = None
            if t in inv:
                try:
                    img = np.array(Image.open(io.BytesIO(jar.read(f'assets/minecraft/textures/{inv[t]}.png'))).convert('RGBA'))
                except KeyError:
                    pass
            if img is None:
                img = np.zeros((16, 16, 4), np.uint8)
                img[..., 0] = img[..., 2] = img[..., 3] = 255
            cache[t] = img[:img.shape[1]]
        return cache[t]

    def rot(ax, deg):
        c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
        m = np.eye(4)
        i, j = [(1, 2), (2, 0), (0, 1)][ax]
        m[i, i] = c; m[i, j] = -s; m[j, i] = s; m[j, j] = c
        return m

    def T(v):
        m = np.eye(4); m[:3, 3] = v; return m

    def S(v):
        m = np.eye(4); m[0, 0], m[1, 1], m[2, 2] = v; return m
    L0 = np.array([-0.933439, 0.262695, -0.244300])
    L1 = np.array([-0.103571, 0.976607, 0.188446])
    size = 64
    ys, xs = np.mgrid[0:size, 0:size]
    px, py = xs + .5, ys + .5

    def render(name):
        row = disp['items'][name]['gui']
        g = disp['styles'][row[1]].get('gui') or {'rotation': [30, 225, 0], 'scale': [.625] * 3, 'translation': [0, 0, 0]}
        P = T(np.array(g['translation']) / 16) @ rot(0, g['rotation'][0]) @ rot(1, g['rotation'][1]) @ rot(2, g['rotation'][2]) @ S(g['scale']) @ T([-.5] * 3)
        col = np.zeros((size, size, 4), np.uint8)
        dep = np.full((size, size), -1e9)
        for _, ti, t, pos, uv in var[ent[items[name]][0][0][0]]:
            img = tload(tex[t]); ts = img.shape[0]
            v = [P @ np.array([*p, 1]) for p in pos]
            sx = [size / 2 + a[0] * size for a in v]; sy = [size / 2 - a[1] * size for a in v]; sz = [a[2] for a in v]
            n = np.linalg.inv(P[:3, :3]).T @ np.cross(np.array(pos[1]) - pos[0], np.array(pos[3]) - pos[0])
            if np.linalg.norm(n) < 1e-9:
                continue
            n /= np.linalg.norm(n)
            n = -n if n[2] < 0 else n
            sh = min(1, (max(0, n @ L0) + max(0, n @ L1)) * .6 + .4)
            for a, b, c in ((0, 1, 2), (0, 2, 3)):
                det = (sx[b] - sx[a]) * (sy[c] - sy[a]) - (sx[c] - sx[a]) * (sy[b] - sy[a])
                if abs(det) < 1e-6:
                    continue
                w1 = ((px - sx[a]) * (sy[c] - sy[a]) - (sx[c] - sx[a]) * (py - sy[a])) / det
                w2 = ((sx[b] - sx[a]) * (py - sy[a]) - (px - sx[a]) * (sy[b] - sy[a])) / det
                w0 = 1 - w1 - w2
                z = w0 * sz[a] + w1 * sz[b] + w2 * sz[c]
                m = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4) & (z >= dep)
                u = w0 * uv[a][0] + w1 * uv[b][0] + w2 * uv[c][0]
                vv = w0 * uv[a][1] + w1 * uv[b][1] + w2 * uv[c][1]
                cc = img[np.clip((vv / 16 * ts).astype(int), 0, ts - 1), np.clip((u / 16 * ts).astype(int), 0, ts - 1)]
                m &= cc[..., 3] >= 8
                dep[m] = z[m]
                col[m] = np.concatenate([np.clip(cc[..., :3] * sh, 0, 255), cc[..., 3:]], -1).astype(np.uint8)[m]
        return Image.fromarray(col)
    names = sorted(k for k, v in disp['items'].items() if v['gui'][0] == 'block' and k in items)
    os.makedirs(out, exist_ok=True)
    per, C, cell = 192, 16, 72
    for s in range(0, len(names), per):
        chunk = names[s:s + per]
        sheet = Image.new('RGBA', (C * cell, ((len(chunk) + C - 1) // C) * (cell + 10)), (139, 139, 139, 255))
        dr = ImageDraw.Draw(sheet)
        for i, n in enumerate(chunk):
            x, y = (i % C) * cell, (i // C) * (cell + 10)
            sheet.alpha_composite(render(n), (x + 4, y + 2))
            dr.text((x + 1, y + cell - 2), n[:12], fill=(0, 0, 0, 255))
        sheet.save(os.path.join(out, f'block_items_{s // per}.png'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--jar', required=True)
    ap.add_argument('--sheets')
    a = ap.parse_args()
    jar = zipfile.ZipFile(a.jar)
    data = os.path.join(ROOT, 'resources/data')
    disp = json.load(open(os.path.join(data, 'items/item_display.json')))
    tex, var, ent, items = load_models(os.path.join(data, 'blocks/block_models.bin'))
    texmap = json.load(open(os.path.join(ROOT, 'tools/modelgen/texmap.json')))
    alias = json.load(open(os.path.join(ROOT, 'tools/modelgen/texalias.json')))
    jname = java_name_fn()
    blocks = {s[1].removeprefix('minecraft:') for s in json.load(open(os.path.join(data, 'blocks/block_states.json')))}
    extra = {l.strip() for l in open(os.path.join(data, 'items/extra_items.txt')) if l.strip() and not l.startswith('#')}
    rows = disp['items']
    unmapped = sorted(b for b in blocks | extra if jname(b) not in rows)
    print(f'{len(blocks | extra)} Bedrock ids, {len(unmapped)} without a Java item (kept on the Bedrock path):')
    print('  ' + ' '.join(unmapped))
    no_mesh = sorted(k for k, v in rows.items() if v['gui'][0] == 'block' and k not in items)
    print(f'block items without an inventory mesh: {no_mesh}')
    print('block items whose textures were matched fuzzily (wrong texture likely):')
    for k, v in sorted(rows.items()):
        if v['gui'][0] != 'block':
            continue
        for t in sorted(model_textures(jar, v['gui'][2])):
            if t in alias:
                continue
            e = texmap.get(t)
            if e is None or e['diff'] > 3.5:
                print(f'  {k}: {t} -> {e["path"] if e else "unmapped"} (diff {e["diff"] if e else "-"})')
    if a.sheets:
        render_sheets(jar, a.sheets, tex, var, ent, items, disp, texmap, alias)
        print('sheets written to', a.sheets)


if __name__ == '__main__':
    main()
