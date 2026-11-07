import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, 'packaging', 'icon', 'perlin.png')
APP_ID = 'io.perlin.client'


def load(path):
    image = Image.open(path).convert('RGBA')
    side = max(image.size)
    square = Image.new('RGBA', (side, side), (0, 0, 0, 0))
    square.paste(image, ((side - image.width) // 2, (side - image.height) // 2))
    return square


def resized(image, size):
    return image.resize((size, size), Image.LANCZOS)


def save(image, *parts):
    path = os.path.join(ROOT, *parts)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    image.save(path)
    return path


def full_bleed(image):
    side = image.width
    panel = image.getpixel((round(side * 0.031), side // 2))
    fill = (panel[0], panel[1], panel[2], 255) if panel[3] == 255 and max(panel[:3]) < 60 else (20, 20, 21, 255)
    out = Image.new('RGBA', image.size, fill)
    pixels = image.load()
    target = out.load()
    for y in range(side):
        for x in range(side):
            r, g, b, a = pixels[x, y]
            if a == 255 and b - r <= 5:
                target[x, y] = (r, g, b, 255)
    inset = round(side * 0.0154)
    return out.crop((inset, inset, side - inset, side - inset)), fill


def padded(image, content):
    side = image.width
    canvas = Image.new('RGBA', (side, side), (0, 0, 0, 0))
    inner = resized(image, round(side * content))
    offset = (side - inner.width) // 2
    canvas.paste(inner, (offset, offset), inner)
    return canvas


def circle(image):
    mask = Image.new('L', image.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, image.width - 1, image.height - 1), fill=255)
    out = Image.new('RGBA', image.size, (0, 0, 0, 0))
    out.paste(image, (0, 0), mask)
    return out


def main():
    master = resized(load(sys.argv[1] if len(sys.argv) > 1 else SOURCE), 1024)
    if len(sys.argv) > 1:
        save(master, 'packaging', 'icon', 'perlin.png')
    bleed, fill = full_bleed(master)
    bleed = resized(bleed, 1024)
    mono = Image.new('RGBA', bleed.size, (255, 255, 255, 0))
    mono.putalpha(bleed.convert('L').point(lambda v: 0 if v < 64 else min(255, (v - 64) * 255 // 191)))

    save(resized(master, 256), 'resources', 'perlin_icon.png')

    os.makedirs(os.path.join(ROOT, 'packaging', 'windows'), exist_ok=True)
    master.save(os.path.join(ROOT, 'packaging', 'windows', 'perlin.ico'),
                sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (96, 96), (128, 128), (256, 256)])

    mac = padded(master, 824 / 1024)
    os.makedirs(os.path.join(ROOT, 'packaging', 'macos'), exist_ok=True)
    mac.save(os.path.join(ROOT, 'packaging', 'macos', 'Perlin.icns'), sizes=[(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)])

    for size in (16, 24, 32, 48, 64, 128, 256, 512):
        save(resized(master, size), 'packaging', 'linux', 'icons', f'{size}x{size}', 'apps', f'{APP_ID}.png')

    res = ('android', 'app', 'src', 'main', 'res')
    for density, scale in (('mdpi', 1.0), ('hdpi', 1.5), ('xhdpi', 2.0), ('xxhdpi', 3.0), ('xxxhdpi', 4.0)):
        save(resized(master, round(48 * scale)), *res, f'mipmap-{density}', 'ic_launcher.png')
        save(circle(resized(bleed, round(48 * scale))), *res, f'mipmap-{density}', 'ic_launcher_round.png')
        save(padded(resized(bleed, round(108 * scale)), 0.82), *res, f'mipmap-{density}', 'ic_launcher_foreground.png')
        save(padded(resized(mono, round(108 * scale)), 0.82), *res, f'mipmap-{density}', 'ic_launcher_monochrome.png')

    adaptive = ('<?xml version="1.0" encoding="utf-8"?>\n'
                '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
                '    <background android:drawable="@color/ic_launcher_background" />\n'
                '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
                '    <monochrome android:drawable="@mipmap/ic_launcher_monochrome" />\n'
                '</adaptive-icon>\n')
    anydpi = os.path.join(ROOT, *res, 'mipmap-anydpi-v26')
    os.makedirs(anydpi, exist_ok=True)
    for name in ('ic_launcher.xml', 'ic_launcher_round.xml'):
        with open(os.path.join(anydpi, name), 'w', encoding='utf-8', newline='\n') as f:
            f.write(adaptive)
    with open(os.path.join(ROOT, *res, 'values', 'ic_launcher_background.xml'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
                f'    <color name="ic_launcher_background">#{fill[0]:02X}{fill[1]:02X}{fill[2]:02X}</color>\n</resources>\n')


if __name__ == '__main__':
    main()
