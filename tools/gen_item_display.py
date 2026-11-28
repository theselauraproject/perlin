#!/usr/bin/env python3
"""Reads the Java client jar and writes resources/data/items/item_display.json.

For every item it records, per display context group (hand, ground, gui), the model the Java client picks,
its kind (flat sprite / block model / special renderer), the display transforms of that model and, for
special renderers, the item definition's local "transformation" and the renderer's arguments. Reference
data only: the transforms are numbers, no assets are copied.

The transforms are resolved exactly the way the Java client does it:
  * a model JSON's display block is read on its own: a missing left-hand transform takes the right-hand
    transform *of the same file* (ItemTransforms.Deserializer);
  * a context's transform is then the first one found walking from the model up its parent chain
    (ResolvedModel.findTopTransform); the transform is taken whole, fields are never merged across files;
  * a missing field defaults to rotation 0, translation 0, scale 1; translation is in sixteenths and
    clamped to +-5 blocks, scale to +-4 (ItemTransform.Deserializer).

usage: gen_item_display.py client.jar out.json
"""
import json
import math
import sys
import zipfile

jar = zipfile.ZipFile(sys.argv[1])
names = set(jar.namelist())

CONTEXTS = (
    "firstperson_righthand",
    "firstperson_lefthand",
    "thirdperson_righthand",
    "thirdperson_lefthand",
    "ground",
    "head",
    "fixed",
    "gui",
)
LEFT_OF = {"firstperson_lefthand": "firstperson_righthand", "thirdperson_lefthand": "thirdperson_righthand"}


def load(path):
    try:
        return json.loads(jar.read(path))
    except KeyError:
        return None


def bare(ref):
    return ref.split(":", 1)[-1]


def model_path(ref):
    return f"assets/minecraft/models/{bare(ref)}.json"


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def read_transform(t):
    """ItemTransform.Deserializer: defaults, then translation/scale clamps (translation stays in 1/16)."""
    rot = [float(x) for x in t.get("rotation", [0, 0, 0])]
    tr = [clamp(float(x) / 16.0, -5.0, 5.0) * 16.0 for x in t.get("translation", [0, 0, 0])]
    sc = [clamp(float(x), -4.0, 4.0) for x in t.get("scale", [1, 1, 1])]
    return {"rotation": [round(x, 5) for x in rot], "translation": [round(x, 5) for x in tr],
            "scale": [round(x, 5) for x in sc]}


def own_transforms(data):
    """The transforms one model file defines, with the left-hand fallback to the same file's right hand."""
    disp = (data or {}).get("display") or {}
    out = {}
    for ctx in CONTEXTS:
        if ctx in disp:
            out[ctx] = read_transform(disp[ctx])
        elif ctx in LEFT_OF and LEFT_OF[ctx] in disp:
            out[ctx] = read_transform(disp[LEFT_OF[ctx]])
    return out


_cache = {}


def resolve_model(ref):
    """(transforms, parent chain root-first, gui_light) of a model reference."""
    key = bare(ref)
    if key in _cache:
        return _cache[key]
    data = load(model_path(ref))
    if data is None:
        _cache[key] = ({}, [key], None)
        return _cache[key]
    parent = data.get("parent")
    disp, chain, light = resolve_model(parent) if parent else ({}, [], None)
    disp = dict(disp)
    disp.update(own_transforms(data))  # the child's transform for a context replaces the parent's whole
    light = data.get("gui_light", light)
    _cache[key] = (disp, chain + [key], light)
    return _cache[key]


def pick(node, ctx, outer=None, using=False):
    """Chooses the leaf of an items/*.json tree for a display context group, in the item's idle state.

    Any node may carry a "transformation"; they compose from the root down to the leaf, so the leaf is
    returned together with the product of every transformation on its path."""
    if not node:
        return None
    here = node.get("transformation")
    chain = (outer or []) + ([here] if here else [])
    t = node.get("type", "").replace("minecraft:", "")
    if t in ("model", "special"):
        return node, chain
    if t == "select":
        prop = node.get("property", "").replace("minecraft:", "")
        if prop == "display_context":
            want = {"ground": "ground", "gui": "gui"}.get(ctx, "firstperson_righthand")
            for case in node.get("cases", []):
                when = case["when"] if isinstance(case["when"], list) else [case["when"]]
                if want in when:
                    return pick(case["model"], ctx, chain, using)
            return pick(node.get("fallback"), ctx, chain, using)
        return pick(node.get("fallback") or (node["cases"][0]["model"] if node.get("cases") else None), ctx, chain,
                    using)
    if t == "condition":
        prop = node.get("property", "").replace("minecraft:", "")
        branch = "on_true" if using and prop == "using_item" else "on_false"
        return pick(node.get(branch), ctx, chain, using)
    if t == "range_dispatch":
        return pick(node.get("fallback") or (node["entries"][0]["model"] if node.get("entries") else None), ctx,
                    chain, using)
    if t == "composite":
        for model in node.get("models", []):
            r = pick(model, ctx, chain, using)
            if r:
                return r
    return None


def quat_matrix(q):
    x, y, z, w = (float(v) for v in q)
    return [
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0],
        [0, 0, 0, 1],
    ]


def matmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def one_matrix(t):
    """com.mojang.math.Transformation: translation * left_rotation * scale * right_rotation."""
    if isinstance(t, list):  # a raw 4x4, row-major in the codec
        return [[float(v) for v in t[i * 4:i * 4 + 4]] for i in range(4)]
    tr = t.get("translation", [0, 0, 0])
    sc = t.get("scale", [1, 1, 1])
    m = [[1, 0, 0, tr[0]], [0, 1, 0, tr[1]], [0, 0, 1, tr[2]], [0, 0, 0, 1]]
    m = matmul(m, quat_matrix(t.get("left_rotation", [0, 0, 0, 1])))
    m = matmul(m, [[sc[0], 0, 0, 0], [0, sc[1], 0, 0], [0, 0, sc[2], 0], [0, 0, 0, 1]])
    return matmul(m, quat_matrix(t.get("right_rotation", [0, 0, 0, 1])))


def local_matrix(chain):
    """The composed transformations of a model path, column-major, or None for the identity."""
    if not chain:
        return None
    rows = [[1 if i == j else 0 for j in range(4)] for i in range(4)]
    for t in chain:
        rows = matmul(rows, one_matrix(t))
    identity = all(abs(rows[i][j] - (1 if i == j else 0)) < 1e-7 for i in range(4) for j in range(4))
    if identity:
        return None
    return [round(rows[r][c] + 0.0, 6) for c in range(4) for r in range(4)]


def describe(picked):
    if not picked:
        return None
    leaf, transforms = picked
    t = leaf["type"].replace("minecraft:", "")
    if t == "special":
        ref = leaf.get("base", "")
        disp, chain, light = resolve_model(ref) if ref else ({}, [], None)
        special = leaf["model"]
        args = {k: v for k, v in special.items() if k != "type"}
        extra = {"light": light or "side"}
        local = local_matrix(transforms)
        if local:
            extra["local"] = local
        if args:
            extra["args"] = args
        return {"kind": "special:" + special["type"].replace("minecraft:", ""), "model": bare(ref),
                "chain": chain, "display": disp, "extra": extra}
    ref = leaf["model"]
    disp, chain, light = resolve_model(ref)
    flat_roots = ("item/generated", "item/handheld", "item/handheld_rod", "item/handheld_mace",
                  "item/template_spawn_egg", "builtin/generated")
    kind = "flat" if any(rule in chain for rule in flat_roots) else "block"
    if chain and chain[0] == "builtin/entity":
        kind = "special:entity"
    extra = {"light": light or ("front" if kind == "flat" else "side")}
    local = local_matrix(transforms)
    if local:
        extra["local"] = local
    return {"kind": kind, "model": bare(ref), "chain": chain, "display": disp, "extra": extra}


DYES = ("white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan", "purple",
        "blue", "brown", "green", "red", "black")
BEDS = {dye + "_bed" for dye in DYES}

styles, style_index = [], {}


def style_of(display):
    disp = {c: t for c, t in display.items() if c in CONTEXTS}
    key = json.dumps(disp, sort_keys=True)
    if key not in style_index:
        style_index[key] = len(styles)
        styles.append(disp)
    return style_index[key]


items = {}
for n in sorted(names):
    if not n.startswith("assets/minecraft/items/") or not n.endswith(".json"):
        continue
    item = n.rsplit("/", 1)[-1][:-5]
    tree = load(n).get("model")
    row = {}
    # "hand_use" is the hand model while the item is in use (minecraft:using_item), e.g. shield_blocking;
    # it is only written when it differs from "hand".
    for ctx, using in (("hand", False), ("ground", False), ("gui", False), ("hand_use", True)):
        d = describe(pick(tree, "hand" if ctx == "hand_use" else ctx, None, using))
        if not d:
            continue
        if d["kind"] == "block" and item in BEDS and tree.get("type", "").endswith("composite"):
            # A bed item is a composite of its head and foot models (the foot one block south). Perlin
            # draws it as one special model from the same elements, textured from the bed sheet the
            # client extracts from the jar, since the Java bed face textures have no Bedrock counterpart.
            d = dict(d, kind="special:bed", extra=dict(d["extra"], args={"color": item[:-4]}))
        entry = [d["kind"], style_of(d["display"]), d["model"]]
        extra = {k: v for k, v in d["extra"].items() if not (k == "light" and v == "side")}
        if extra:
            entry.append(extra)
        if ctx == "hand_use" and entry == row.get("hand"):
            continue
        row[ctx] = entry
    if row:
        items[item] = row

with open(sys.argv[2], "w") as out:
    json.dump({"styles": styles, "items": items}, out, separators=(",", ":"), sort_keys=True)
print(len(items), "items,", len(styles), "styles")
