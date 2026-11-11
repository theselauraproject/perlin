"""Unpack and merge the feature data of the dedicated server's behavior packs:
extract_bds_features.py <bedrock server dir> <out dir>

writes <out>/features and <out>/feature_rules. the server keeps each pack version's data in __brarchive/*.brarchive files (header: 8 byte magic, u32 count,
u32 version; then count 256 byte entries: u8 name length, name, u32 offset at +248, u32 size at +252; data follows the table, offsets relative to it).
packs are applied oldest first, 'vanilla' (the base) before the versioned ones, so newer versions override older files of the same name
"""
import os
import re
import struct
import sys

server, out = sys.argv[1:3]

def read(path):
    d = open(path, 'rb').read()
    if len(d) < 16:
        return {}
    n = struct.unpack('<I', d[8:12])[0]
    base = 16 + n * 256
    files = {}
    for i in range(n):
        o = 16 + i * 256
        name = d[o + 1:o + 1 + d[o]].decode()
        off, size = struct.unpack('<II', d[o + 248:o + 256])
        files[name] = d[base + off:base + off + size]
    return files

packs = os.path.join(server, 'behavior_packs')
def version(name):
    m = re.fullmatch(r'vanilla(?:_(\d+(?:\.\d+)*))?', name)
    if not m:
        return None
    return tuple(int(p) for p in m.group(1).split('.')) if m.group(1) else (0,)

order = sorted((n for n in os.listdir(packs) if version(n) is not None), key=version)
for kind in ('features', 'feature_rules'):
    dest = os.path.join(out, kind)
    os.makedirs(dest, exist_ok=True)
    count = 0
    for pack in order:
        arc = os.path.join(packs, pack, '__brarchive', kind + '.brarchive')
        if not os.path.exists(arc):
            continue
        for name, data in read(arc).items():
            open(os.path.join(dest, os.path.basename(name)), 'wb').write(data)
            count += 1
    print(kind, count, 'files written')
