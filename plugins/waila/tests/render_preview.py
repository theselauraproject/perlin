#!/usr/bin/env python3
"""Renders the draw commands written by `preview` to a PNG, using the Java client's font and textures.

    render_preview.py preview.txt out.png [--jar /path/to/extracted/jar] [--scale 4]
"""
import sys, os, re, math
from PIL import Image, ImageDraw

args = sys.argv[1:]
jar = '/home/claude/jar'
scale = 4
files = []
i = 0
while i < len(args):
    if args[i] == '--jar': jar = args[i + 1]; i += 2
    elif args[i] == '--scale': scale = int(args[i + 1]); i += 2
    else: files.append(args[i]); i += 1
src, dst = files
W, H = 320, 180
tex = lambda p: os.path.join(jar, 'assets/minecraft/textures', p)

font = Image.open(tex('font/ascii.png')).convert('RGBA')
GW = font.size[0] // 16
def glyph(c):
    o = ord(c)
    if o >= 256: return None, 6
    cx, cy = (o % 16) * GW, (o // 16) * GW
    g = font.crop((cx, cy, cx + GW, cy + GW))
    w = 0
    for x in range(GW):
        if any(g.getpixel((x, y))[3] > 0 for y in range(GW)): w = x + 1
    return g, (w + 1 if w else 4)

COLORS = {'0':0x000000,'1':0x0000AA,'2':0x00AA00,'3':0x00AAAA,'4':0xAA0000,'5':0xAA00AA,'6':0xFFAA00,'7':0xAAAAAA,'8':0x555555,
          '9':0x5555FF,'a':0x55FF55,'b':0x55FFFF,'c':0xFF5555,'d':0xFF55FF,'e':0xFFFF55,'f':0xFFFFFF}

def unpack(c):
    return ((c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, c & 255)

img = Image.new('RGBA', (W * scale, H * scale), (0, 0, 0, 255))
import random
random.seed(3)
bd = ImageDraw.Draw(img)
for gy in range(0, H * scale, 16 * scale // 4):
    for gx in range(0, W * scale, 16 * scale // 4):
        v = random.randint(34, 60)
        bd.rectangle([gx, gy, gx + 16 * scale // 4, gy + 16 * scale // 4], fill=(v, v, v, 255))

def blend(layer):
    global img
    img = Image.alpha_composite(img, layer)

def new_layer():
    return Image.new('RGBA', img.size, (0, 0, 0, 0))

def draw_text(x, y, color, size, text):
    layer = new_layer()
    cx = 0.0
    r, g, b, a = unpack(color)
    italic = False; col = (r, g, b)
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == '§' and i + 1 < len(text):
            code = text[i + 1].lower(); i += 2
            if code in COLORS: v = COLORS[code]; col = ((v >> 16) & 255, (v >> 8) & 255, v & 255); italic = False
            elif code == 'o': italic = True
            elif code == 'r': col = (r, g, b); italic = False
            continue
        gl, adv = glyph(ch)
        if gl is not None:
            gw = gl.copy()
            px = gw.load()
            for yy in range(GW):
                for xx in range(GW):
                    p = px[xx, yy]
                    if p[3] > 0: px[xx, yy] = (col[0], col[1], col[2], p[3] * a // 255)
            sh = gw.copy(); spx = sh.load()
            for yy in range(GW):
                for xx in range(GW):
                    p = spx[xx, yy]
                    if p[3] > 0: spx[xx, yy] = (col[0] // 4, col[1] // 4, col[2] // 4, p[3])
            for im2, off in ((sh, 1), (gw, 0)):
                big = im2.resize((int(GW * size * scale), int(GW * size * scale)), Image.NEAREST)
                if italic:
                    big = big.transform(big.size, Image.AFFINE, (1, 0.2, -0.1 * big.size[1], 0, 1, 0), Image.NEAREST)
                layer.alpha_composite(big, (int(round((x + cx * size + off) * scale)), int(round((y + off) * scale))))
        cx += adv + (1 if italic else 0) * 0
        i += 1
    blend(layer)

def load_icon(name):
    n = name.split(':')[-1]
    for sub in ('item', 'block'):
        p = tex(f'{sub}/{n}.png')
        if os.path.exists(p): return sub, Image.open(p).convert('RGBA')
    for suffix in ('_side', '_front', '_top'):
        p = tex(f'block/{n}{suffix}.png')
        if os.path.exists(p): return 'block', Image.open(p).convert('RGBA')
    return None, None

def draw_item(x, y, size, name):
    kind, t = load_icon(name)
    layer = new_layer()
    if t is None:
        ImageDraw.Draw(layer).rectangle([x * scale, y * scale, (x + size) * scale, (y + size) * scale], outline=(255, 0, 255, 255))
        blend(layer); return
    t = t.crop((0, 0, t.size[0], t.size[0]))
    if kind == 'item':
        big = t.resize((int(size * scale), int(size * scale)), Image.NEAREST)
        layer.alpha_composite(big, (int(x * scale), int(y * scale)))
    else:
        s = int(size * scale)
        top = t.resize((s // 2, s // 2), Image.NEAREST)
        side = t.resize((s // 2, s // 2), Image.NEAREST)
        cube = Image.new('RGBA', (s, s), (0, 0, 0, 0))
        topq = top.transform((s, s // 2), Image.AFFINE, (0.5, 1.0, -s / 4 * 0 , -0.5, 1.0, s / 4), Image.NEAREST) if False else None
        tt = t.resize((s // 2, s // 2), Image.NEAREST).rotate(45, expand=True, resample=Image.NEAREST).resize((s, s // 2), Image.NEAREST)
        cube.alpha_composite(tt, (0, 0))
        def shade(im, f):
            im = im.copy(); px = im.load()
            for yy in range(im.size[1]):
                for xx in range(im.size[0]):
                    p = px[xx, yy]; px[xx, yy] = (int(p[0] * f), int(p[1] * f), int(p[2] * f), p[3])
            return im
        left = shade(t.resize((s // 2, int(s * 0.5)), Image.NEAREST), 0.8)
        right = shade(t.resize((s // 2, int(s * 0.5)), Image.NEAREST), 0.6)
        lq = left.transform((s // 2, int(s * 0.62)), Image.AFFINE, (1, 0, 0, 0.5, 1, -s // 4 * 0 + 0), Image.NEAREST)
        cube.alpha_composite(left.resize((s // 2, int(s * 0.5))), (0, int(s * 0.28)))
        cube.alpha_composite(right.resize((s // 2, int(s * 0.5))), (s // 2, int(s * 0.28)))
        layer.alpha_composite(cube, (int(x * scale), int(y * scale)))
    blend(layer)

HEART = {'textures/ui/heart_background': 'gui/sprites/hud/heart/container.png', 'textures/ui/heart': 'gui/sprites/hud/heart/full.png',
         'textures/ui/heart_half': 'gui/sprites/hud/heart/half.png', 'textures/ui/armor_full': 'gui/sprites/hud/armor_full.png',
         'textures/ui/armor_half': 'gui/sprites/hud/armor_half.png', 'textures/ui/armor_empty': 'gui/sprites/hud/armor_empty.png'}
def draw_image(x, y, w, h, path):
    p = HEART.get(path)
    if p and os.path.exists(tex(p)):
        t = Image.open(tex(p)).convert('RGBA').resize((int(w * scale), int(h * scale)), Image.NEAREST)
        layer = new_layer(); layer.alpha_composite(t, (int(x * scale), int(y * scale))); blend(layer)

for line in open(src, encoding='utf-8'):
    parts = line.rstrip('\n').split(' ')
    k = parts[0]
    if k == 'rect':
        x, y, w, h = map(float, parts[1:5]); c = unpack(int(parts[5]))
        layer = new_layer(); ImageDraw.Draw(layer).rectangle([round(x * scale), round(y * scale), round((x + w) * scale) - 1, round((y + h) * scale) - 1], fill=c); blend(layer)
    elif k == 'gradient':
        x, y, w, h = map(float, parts[1:5]); a = unpack(int(parts[5])); b = unpack(int(parts[6]))
        layer = new_layer(); d = ImageDraw.Draw(layer)
        rows = max(1, round(h * scale))
        for r in range(rows):
            t = r / max(1, rows - 1)
            col = tuple(int(a[j] + (b[j] - a[j]) * t) for j in range(4))
            d.rectangle([round(x * scale), round(y * scale) + r, round((x + w) * scale) - 1, round(y * scale) + r], fill=col)
        blend(layer)
    elif k == 'line':
        x0, y0, x1, y1 = map(float, parts[1:5]); c = unpack(int(parts[5])); t = float(parts[6])
        layer = new_layer(); ImageDraw.Draw(layer).line([x0 * scale, y0 * scale, x1 * scale, y1 * scale], fill=c, width=max(1, int(t * scale)))
        blend(layer)
    elif k == 'text':
        x, y = float(parts[1]), float(parts[2]); c = int(parts[3]); s = float(parts[4]); draw_text(x, y, c, s, ' '.join(parts[5:]))
    elif k == 'item':
        draw_item(float(parts[1]), float(parts[2]), float(parts[3]), parts[4])
    elif k == 'image':
        draw_image(*map(float, parts[1:5]), parts[5])

xs = []
for line in open(src, encoding='utf-8'):
    p = line.split(' ')
    if p[0] in ('rect', 'gradient'): xs.append((float(p[1]), float(p[2]), float(p[1]) + float(p[3]), float(p[2]) + float(p[4])))
if xs:
    x0 = max(0, min(a[0] for a in xs) - 12); y0 = max(0, min(a[1] for a in xs) - 8)
    x1 = max(a[2] for a in xs) + 12; y1 = max(a[3] for a in xs) + 8
    img = img.crop((int(x0 * scale), int(y0 * scale), int(x1 * scale), int(y1 * scale)))
img.convert('RGB').save(dst)
print('wrote', dst, img.size)
