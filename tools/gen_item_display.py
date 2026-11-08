#!/usr/bin/env python3
"""Reads the Java client jar and writes resources/item_display.json: for every item the display transforms (first/third person
hands, ground, fixed, head, gui) of the model it shows in the hand and on the ground, plus the model kind
(flat sprite / block model / special renderer). Reference data only - the transforms are numbers, no assets are copied.

usage: gen_item_display.py client.jar out.json
"""
import json, sys, zipfile

jar = zipfile.ZipFile(sys.argv[1])
names = set(jar.namelist())

def load(path):
    try:
        return json.loads(jar.read(path))
    except KeyError:
        return None

def model_path(ref):
    ref = ref.split(":", 1)[-1]
    return f"assets/minecraft/models/{ref}.json"

_cache = {}
def resolve_model(ref):
    """Merged display dict and root parent chain of a model reference."""
    if ref in _cache:
        return _cache[ref]
    data = load(model_path(ref))
    if data is None:
        _cache[ref] = ({}, [ref.split(":", 1)[-1]], None)
        return _cache[ref]
    disp, chain, _ = ({}, [], None)
    parent = data.get("parent")
    if parent:
        disp, chain, _ = resolve_model(parent)
        disp = {k: dict(v) for k, v in disp.items()}
    chain = chain + [ref.split(":", 1)[-1]]
    for ctx, t in (data.get("display") or {}).items():
        merged = dict(disp.get(ctx, {}))
        merged.update(t)
        disp[ctx] = merged
    _cache[ref] = (disp, chain, data)
    return _cache[ref]

HAND_CONTEXTS = {"firstperson_righthand", "firstperson_lefthand", "thirdperson_righthand", "thirdperson_lefthand"}

def pick(node, ctx):
    """Chooses the leaf model of an items/*.json tree for a display context ('hand' or 'ground'), idle state."""
    if not node:
        return None

    t = node.get("type", "").replace("minecraft:", "")

    match(t):
        case "model":
            return node
        case "special":
            return node
        case "select":
            prop = node.get("property", "").replace("minecraft:", "")
            if prop == "display_context":
                want: str = "ground" if ctx == "ground" else "firstperson_righthand"
                for case in node.get("cases", []):
                    when: list = case["when"] if isinstance(case["when"], list) else [case["when"]]
                    if want in when:
                        return pick(case["model"], ctx)
                return pick(node.get("fallback"), ctx)
            return pick(
                node.get("fallback") or (
                    node["cases"][0]["model"] if node.get("cases") else None
                ), ctx)
        case "condition":
            return pick(node.get("on_false"), ctx)
        case "range_dispatch":
            return pick(node.get("fallback") or (
                node["entries"][0]["model"] if node.get("entries") else None
            ), ctx)
        case "composite":
            for model in node.get("models", []):
                r = pick(model, ctx)
                if r:
                    return r
        case "empty":
            return None

    return None

def describe(leaf):
    if not leaf:
        return None

    t = leaf["type"].replace("minecraft:", "")

    if t == "special":
        ref = leaf.get("base", "")
        disp, chain, _ = resolve_model(ref) if ref else ({}, [], None)
        return {
            "kind": "special:" + leaf["model"]["type"].replace("minecraft:", ""),
            "model": ref.split(":", 1)[-1],
            "chain": chain,
            "display": disp
        }

    ref = leaf["model"]

    disp, chain, data = resolve_model(ref)

    root = chain[0] if chain else ""

    exclude = [
        "item/generated",
        "item/handheld",
        "item/handheld_rod",
        "item/template_spawn_egg",
    ]

    kind: str = "flat" if any(rule in chain for rule in exclude) else "block"

    if root == "builtin/entity":
        kind = "special:entity"

    return {
        "kind": kind,
        "model": ref.split(":", 1)[-1],
        "chain": chain,
        "display": disp
    }

out = {}
for n in sorted(names):
    if not n.startswith("assets/minecraft/items/") or not n.endswith(".json"):
        continue

    item = n.rsplit("/", 1)[-1][:-5]
    tree = load(n).get("model")
    entry = {}

    for ctx in ("hand", "ground"):
        d = describe(pick(tree, ctx))
        if d:
            entry[ctx] = d

    if entry:
        out[item] = entry

def compact(d):
    keep = {}
    for ctx, t in d["display"].items():
        keep[ctx] = {k: t[k] for k in ("rotation", "translation", "scale") if k in t}

    return {
        "kind": d["kind"],
        "model": d["model"],
        "root": d["chain"][0] if d["chain"] else "",
        "display": keep
    }

KEEP = (
    "firstperson_righthand",
    "firstperson_lefthand",
    "thirdperson_righthand",
    "thirdperson_lefthand",
    "ground",
    "head",
    "fixed",
)

styles, style_index = [], {}

def style_of(d):
    disp = {c: t for c, t in d["display"].items() if c in KEEP}
    key = json.dumps(disp, sort_keys=True)

    if key not in style_index:
        style_index[key] = len(styles)
        styles.append(disp)

    return style_index[key]

items = {}
for item, e in out.items():
    row = {}
    for ctx, d in e.items():
        c = compact(d)
        row[ctx] = [c["kind"], style_of(c), c["model"]]
    items[item] = row

json.dump({"styles": styles, "items": items}, open(sys.argv[2], "w"), separators=(",", ":"), sort_keys=True)
print(len(items), "items,", len(styles), "styles")
