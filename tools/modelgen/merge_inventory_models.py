#!/usr/bin/env python3
"""Append Java inventory meshes to a shipped PBM1 without modifying world-state meshes.

Usage: python merge_inventory_models.py original.bin generated-with-IMOD.bin output.bin
The generated file is produced by modelgen.py with JAVA_ASSETS from a client JAR.
The original world-state hashes, variants and entries are preserved exactly.
"""
import struct
import sys
from pathlib import Path

QUAD_SIZE = struct.calcsize('<BBbbBBH') + 4 * 3 * 4 + 4 * 2 * 4

class Reader:
    def __init__(self, data):
        self.data, self.offset = data, 0
    def bytes(self, n):
        end = self.offset + n
        if end > len(self.data):
            raise ValueError('truncated PBM1 file')
        value = self.data[self.offset:end]
        self.offset = end
        return value
    def number(self, fmt):
        return struct.unpack('<' + fmt, self.bytes(struct.calcsize('<' + fmt)))[0]


def unpack(data):
    r = Reader(data)
    if r.bytes(4) != b'PBM1':
        raise ValueError('invalid model magic')
    textures = [r.bytes(r.number('H')) for _ in range(r.number('I'))]
    variants = [r.bytes(r.number('H') * QUAD_SIZE) for _ in range(r.number('I'))]
    entries = []
    for _ in range(r.number('I')):
        flags = r.number('B')
        groups = [[(r.number('I'), r.number('B')) for _ in range(r.number('B'))]
                  for _ in range(r.number('B'))]
        entries.append((flags, groups))
    states = r.bytes(8 * r.number('I'))
    items = {}
    if r.offset < len(data):
        if r.bytes(4) != b'IMOD':
            raise ValueError('unexpected PBM1 footer')
        for _ in range(r.number('I')):
            name = r.bytes(r.number('H')).decode('utf-8')
            items[name] = r.number('I')
    if r.offset != len(data):
        raise ValueError('extra model data')
    return textures, variants, entries, states, items


def pack(textures, variants, entries, states, items):
    out = bytearray(b'PBM1')
    out += struct.pack('<I', len(textures))
    for path in textures:
        out += struct.pack('<H', len(path)) + path
    out += struct.pack('<I', len(variants))
    for variant in variants:
        if len(variant) % QUAD_SIZE:
            raise ValueError('invalid quad data')
        out += struct.pack('<H', len(variant) // QUAD_SIZE) + variant
    out += struct.pack('<I', len(entries))
    for flags, groups in entries:
        out += struct.pack('<BB', flags, len(groups))
        for group in groups:
            out += struct.pack('<B', len(group))
            for variant_id, weight in group:
                out += struct.pack('<IB', variant_id, weight)
    out += struct.pack('<I', len(states) // 8) + states
    out += b'IMOD' + struct.pack('<I', len(items))
    for name, entry in sorted(items.items()):
        key = name.encode('utf-8')
        out += struct.pack('<H', len(key)) + key + struct.pack('<I', entry)
    return bytes(out)


def merge(original, generated):
    tex, var, ent, states, old_items = unpack(original)
    new_tex, new_var, new_ent, _, new_items = unpack(generated)
    tex_index = {path: i for i, path in enumerate(tex)}
    remapped_variants = {}
    remapped_entries = {}

    def convert_variant(index):
        if index in remapped_variants:
            return remapped_variants[index]
        raw = bytearray(new_var[index])
        for at in range(0, len(raw), QUAD_SIZE):
            source = struct.unpack_from('<H', raw, at + 6)[0]
            if source == 0xffff:
                continue
            name = new_tex[source]
            if name not in tex_index:
                tex_index[name] = len(tex)
                tex.append(name)
            struct.pack_into('<H', raw, at + 6, tex_index[name])
        remapped_variants[index] = len(var)
        var.append(bytes(raw))
        return remapped_variants[index]

    def convert_entry(index):
        if index in remapped_entries:
            return remapped_entries[index]
        flags, groups = new_ent[index]
        new_groups = [[(convert_variant(v), weight) for v, weight in group] for group in groups]
        remapped_entries[index] = len(ent)
        ent.append((flags, new_groups))
        return remapped_entries[index]

    items = dict(old_items)
    for name, index in sorted(new_items.items()):
        items[name] = convert_entry(index)
    merged = pack(tex, var, ent, states, items)
    # Byte-for-byte preservation of the original state table is critical:
    # rendering changes must not alter server block identity or physics.
    _, _, _, after_states, after_items = unpack(merged)
    if states != after_states or len(after_items) != len(items):
        raise AssertionError('world model state table changed')
    return merged, len(items)


if __name__ == '__main__':
    original, generated, output = map(Path, sys.argv[1:4])
    data, count = merge(original.read_bytes(), generated.read_bytes())
    output.write_bytes(data)
    print(f'wrote {output}: {len(data)} bytes, {count} Java inventory models')
