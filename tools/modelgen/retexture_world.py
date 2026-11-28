#!/usr/bin/env python3
"""Carry texture fixes from a fresh modelgen run into a shipped PBM1's world-state meshes.

Usage: python retexture_world.py shipped.bin generated.bin output.bin

The shipped world meshes are kept (they include hand fixes the generator does not reproduce, e.g. the
multi-face blocks); only states whose generated mesh has identical geometry, quad for quad, take the
generated texture of each quad. The state table is preserved byte for byte. Run merge_inventory_models.py
afterwards for the IMOD footer.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from merge_inventory_models import unpack, pack, QUAD_SIZE


def states_of(blob):
    return dict(struct.unpack_from('<iI', blob, 8 * i) for i in range(len(blob) // 8))


def main(shipped, generated, output):
    tex, var, ent, states, items = unpack(Path(shipped).read_bytes())
    gtex, gvar, gent, gstates, _ = unpack(Path(generated).read_bytes())
    tex, var = list(tex), list(var)
    index = {t: i for i, t in enumerate(tex)}
    a_map, b_map = states_of(states), states_of(gstates)
    changed_vars, changed_quads = set(), 0
    matched, protected = [], set()
    for h, e in a_map.items():
        ga = ent[e][1]
        gb = gent[b_map[h]][1] if h in b_map else None
        ok = gb is not None and len(ga) == len(gb) and all(len(x) == len(y) for x, y in zip(ga, gb))
        pairs = [(va, vb) for x, y in zip(ga, gb) for (va, _), (vb, _) in zip(x, y)] if ok else []
        # geometry (everything but the 2-byte texture index) must match exactly
        ok = ok and not any(
            len(var[va]) != len(gvar[vb]) or
            any(var[va][o:o + 6] != gvar[vb][o:o + 6] or var[va][o + 8:o + QUAD_SIZE] != gvar[vb][o + 8:o + QUAD_SIZE]
                for o in range(0, len(var[va]), QUAD_SIZE)) for va, vb in pairs)
        if ok:
            matched.append(pairs)
        else:
            # a variant shared with a state the generator builds differently keeps its textures
            protected.update(va for group in ga for va, _ in group)
    for pairs in matched:
        if any(va in protected for va, _ in pairs):
            continue
        for va, vb in pairs:
            if va in changed_vars:
                continue
            buf = bytearray(var[va])
            for o in range(0, len(buf), QUAD_SIZE):
                name = gtex[struct.unpack_from('<H', gvar[vb], o + 6)[0]]
                if name not in index:
                    index[name] = len(tex)
                    tex.append(name)
                new = index[name]
                if struct.unpack_from('<H', buf, o + 6)[0] != new:
                    struct.pack_into('<H', buf, o + 6, new)
                    changed_quads += 1
            var[va] = bytes(buf)
            changed_vars.add(va)
    out = pack(tex, var, ent, states, items)
    if unpack(out)[3] != states:
        raise AssertionError('state table changed')
    Path(output).write_bytes(out)
    print(f'wrote {output}: {changed_quads} quads retextured')


if __name__ == '__main__':
    main(*sys.argv[1:4])
