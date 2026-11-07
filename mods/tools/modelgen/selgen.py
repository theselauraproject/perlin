import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "resources", "block_selection.bin")
SRC = sys.argv[1]
S = 1e8
data = open(SRC, "rb").read()
body = data[:-32]
assert body[:8] == b"PREG1002"
count = struct.unpack_from("<I", body, 12)[0]
p = 48

def u8():
    global p
    v = body[p]
    p += 1
    return v

def u32():
    global p
    v = struct.unpack_from("<I", body, p)[0]
    p += 4
    return v

def box():
    global p
    v = struct.unpack_from("<6d", body, p)
    p += 48
    return v

records = []
for i in range(count):
    seq = u32()
    h = u32()
    nbox = u8()
    flags = u8()
    surface = u8()
    sem = u8()
    p += 16
    p += nbox * 24
    p += 32
    facing = u8()
    if u8():
        box()
    nsel = u8()
    pick = u8()
    shape_flags = struct.unpack_from("<H", body, p)[0]
    p += 2
    door = u8()
    p += 1
    if door != 255:
        p += 32
    p += 4
    sel = [box() for _ in range(nsel)]
    sel_ctx = u8()
    ncont = u8()
    for _ in range(ncont):
        box()
    out_ctx = u8()
    outline = box() if u8() else None
    if u8():
        box()
    ntags = u8()
    for _ in range(ntags):
        n = u8()
        p += n
    records.append((h, pick, shape_flags, sel, outline))
assert p == len(body), (p, len(body))

shapes = {}
table = []
for h, pick, sf, sel, outline in records:
    if pick == 1 and not outline and len(sel) == 1 and sel[0] == (0.0, 0.0, 0.0, 1.0, 1.0, 1.0):
        continue
    key = (pick, tuple(sel), outline)
    if key not in shapes:
        shapes[key] = len(shapes)
    table.append((h, shapes[key]))
out = bytearray(b"SEL2")
out += struct.pack("<II", len(shapes), len(table))
for (pick, sel, outline), idx in sorted(shapes.items(), key=lambda kv: kv[1]):
    out += struct.pack("<BB", pick, len(sel))
    for b in sel:
        out += struct.pack("<6f", *b)
    out += struct.pack("<B", 1 if outline else 0)
    if outline:
        out += struct.pack("<6f", *outline)
for h, idx in table:
    out += struct.pack("<IH", h, idx)
open(OUT, "wb").write(out)
print("wrote", OUT, len(out), "bytes,", len(shapes), "shapes,", len(table), "of", count)
