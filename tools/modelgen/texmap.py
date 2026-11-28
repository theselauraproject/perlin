"""Maps Java block texture ids to Bedrock resource pack texture paths by pixel content."""
import json, os
import numpy as np
from PIL import Image

JAVA = os.environ['JAVA_TEXTURES']
BEDROCK = os.environ['BEDROCK_PACK']

def load(path):
    try:
        im = Image.open(path).convert('RGBA')
    except Exception:
        return None
    a = np.asarray(im, dtype=np.float32)
    s = min(a.shape[0], a.shape[1])
    a = a[:s, :s]
    if s != 16:
        idx = (np.arange(16) * s // 16)
        a = a[idx][:, idx]
    return a

def sig(a):
    p = a.copy()
    p[..., :3] *= (p[..., 3:4] / 255.0)
    return p

def used_texels(models_dir):
    """16x16 masks of the texels each Java block texture is sampled at by the block models."""
    if not os.path.isdir(models_dir):
        return {}
    raw = {}
    for f in os.listdir(models_dir):
        if f.endswith('.json'):
            try:
                raw['block/' + f[:-5]] = json.load(open(os.path.join(models_dir, f)))
            except Exception:
                pass

    def chain(name):
        out = []
        while name:
            name = name.removeprefix('minecraft:')
            m = raw.get(name)
            if m is None:
                break
            out.append(m)
            name = m.get('parent')
        return out
    masks = {}
    for name in raw:
        c = chain(name)
        tex = {}
        for m in reversed(c):
            for k, v in m.get('textures', {}).items():
                tex[k] = v['sprite'] if isinstance(v, dict) else v
        els = next((m['elements'] for m in c if 'elements' in m), [])
        for e in els:
            fr, to = e['from'], e['to']
            for face, fd in e.get('faces', {}).items():
                ref = fd['texture']
                for _ in range(8):
                    if ref.startswith('#'):
                        ref = tex.get(ref[1:], ref)
                if ref.startswith('#'):
                    continue
                ref = ref.removeprefix('minecraft:')
                uv = fd.get('uv')
                if uv is None:
                    x0, y0, x1, y1 = {'up': (fr[0], fr[2], to[0], to[2]), 'down': (fr[0], 16 - to[2], to[0], 16 - fr[2]),
                                      'north': (16 - to[0], 16 - to[1], 16 - fr[0], 16 - fr[1]),
                                      'south': (fr[0], 16 - to[1], to[0], 16 - fr[1]),
                                      'west': (fr[2], 16 - to[1], to[2], 16 - fr[1]),
                                      'east': (16 - to[2], 16 - to[1], 16 - fr[2], 16 - fr[1])}[face]
                else:
                    x0, y0, x1, y1 = uv
                xa, xb = sorted((x0, x1))
                ya, yb = sorted((y0, y1))
                m = masks.setdefault(ref, np.zeros((16, 16), np.float32))
                m[int(max(0, ya)):int(min(16, np.ceil(yb))), int(max(0, xa)):int(min(16, np.ceil(xb)))] = 1
    return {k: v for k, v in masks.items() if v.sum() > 0}

def main():
    bed = {}
    bdir = os.path.join(BEDROCK, 'textures', 'blocks')
    files = []
    for dp, dn, fn in os.walk(bdir):
        for f in fn:
            files.append(os.path.relpath(os.path.join(dp, f), bdir).replace(os.sep, '/'))
    stems = set(files)
    for f in sorted(files):
        stem, ext = os.path.splitext(f)
        if ext not in ('.png', '.tga'):
            continue
        a = load(os.path.join(bdir, f))
        if a is None:
            continue
        # PBR companions of a colour texture (x_mers, x_normal, ...) are skipped; a colour texture that only
        # happens to end that way (sandstone_normal, red_sandstone_normal) is kept.
        if stem.endswith(('_mers', '_heightmap', '_mer')):
            continue
        companion = next((sfx for sfx in ('_normal', '_n') if stem.endswith(sfx)), None)
        if companion and any(stem[:-len(companion)] + e in stems for e in ('.png', '.tga')):
            continue
        key = 'textures/blocks/' + stem
        if key in bed and ext == '.png':
            continue
        bed[key] = sig(a)
    keys = list(bed)
    stack = np.stack([bed[k] for k in keys])
    out = {}
    used = used_texels(os.path.join(JAVA, '..', 'models', 'block'))
    jdir = os.path.join(JAVA, 'block')
    unmatched = []
    for f in sorted(os.listdir(jdir)):
        if not f.endswith('.png'):
            continue
        jid = 'block/' + f[:-4]
        a = load(os.path.join(jdir, f))
        if a is None:
            continue
        s = sig(a)
        d = np.abs(stack - s[None]).mean(axis=(1, 2, 3))
        order = np.argsort(d)
        best = order[0]
        same = 'textures/blocks/' + f[:-4]
        if same in bed and d[keys.index(same)] <= d[best] + 1.0:
            best = keys.index(same)
        if d[best] > 3.5 and jid in used:
            # Models often sample only part of a Java texture (the dried ghast's 10x10 faces) while the
            # Bedrock texture leaves the rest empty: compare only the texels the Java models use.
            m = used[jid][None, :, :, None]
            dm = (np.abs(stack - s[None]) * m).sum(axis=(1, 2, 3)) / (m.sum() * 4)
            mb = int(np.argmin(dm))
            if dm[mb] < 1.0:
                best, d = mb, dm
        out[jid] = {'path': keys[best], 'diff': float(d[best])}
        if d[best] > 5:
            unmatched.append((jid, keys[best], float(d[best])))
    json.dump(out, open(os.path.join(os.path.dirname(__file__), 'texmap.json'), 'w'), indent=1, sort_keys=True)
    print(len(out), 'mapped;', len(unmatched), 'poor')
    for u in unmatched:
        print(u)

main()
