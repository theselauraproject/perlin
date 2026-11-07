#!/usr/bin/env python3
"""Builds resources/block_models.bin: per-Bedrock-block-state baked Java models.

Inputs (not shipped in the repo; see README):
  JAVA_ASSETS   extracted assets/minecraft of the Java client jar (models/block, blockstates)
  BEDROCK_STATES  bedrock_states.json  (name, state) per record, in network order
  PHYSICS       physics.json            (seq, hash, flags, ..., boxes) from the Bedrock block registry
  texmap.json   Java texture -> Bedrock texture path (texmap.py)
"""
import itertools
import json
import os
import struct
import sys
from collections import Counter, OrderedDict

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from javamodels import Models, DIRS, block_rotation
from blockstates import load_all
from translate import translate

JAVA_ASSETS = os.environ.get("JAVA_ASSETS", "/tmp/claude-0/jm/assets/minecraft")
OUT = os.environ.get("OUT", os.path.join(HERE, "..", "..", "resources", "block_models.bin"))

KEEP_LEGACY = {
    "grass_block", "mycelium", "podzol", "water", "lava", "bubble_column", "fire", "soul_fire", "redstone_wire",
    "air", "cave_air", "void_air", "light", "barrier", "structure_void", "moving_piston", "piston_head",
}

OFFSET_XZ = {
    "poppy", "dandelion", "blue_orchid", "allium", "azure_bluet", "red_tulip", "orange_tulip", "white_tulip",
    "pink_tulip", "oxeye_daisy", "cornflower", "lily_of_the_valley", "torchflower", "wither_rose", "sunflower",
    "lilac", "rose_bush", "peony", "tall_grass", "large_fern", "bamboo", "bamboo_sapling", "dead_bush",
    "seagrass", "tall_seagrass", "small_dripleaf", "hanging_roots", "crimson_roots", "warped_roots",
    "nether_sprouts", "golden_dandelion", "open_eyeblossom", "closed_eyeblossom", "short_dry_grass",
    "tall_dry_grass", "firefly_bush", "bush",
}
OFFSET_XYZ = {"short_grass", "fern"}
OFFSET_DRIPSTONE = {"pointed_dripstone", "sulfur_spike"}

DIR_INDEX = {d: i for i, d in enumerate(DIRS)}

def load_records():
    states = json.load(open(os.path.join(HERE, "bedrock_states.json")))
    phys = json.load(open(os.path.join(HERE, "physics.json")))
    assert len(states) == len(phys), (len(states), len(phys))
    recs = []
    for (name, st), p in zip(states, phys):
        seq, h, flags, surf, fr, hs, vs, fh, boxes = p
        recs.append({"name": name, "state": st, "hash": h, "flags": flags,
                     "boxes": [[c / 1e8 for c in b] for b in boxes]})
    return recs

def is_full_cube(model):
    els = model["elements"] or []
    if len(els) != 1:
        return False
    e = els[0]
    return (e["from"] == [0, 0, 0] and e["to"] == [16, 16, 16] and not e.get("rotation") and len(e["faces"]) == 6)

class Gen:
    def __init__(self):
        self.models = Models(JAVA_ASSETS)
        self.jb = load_all(JAVA_ASSETS)
        self.texmap = json.load(open(os.path.join(HERE, "texmap.json")))
        manual = os.path.join(HERE, "texalias.json")
        self.alias = json.load(open(manual)) if os.path.exists(manual) else {}
        self.tex_ids = OrderedDict()
        self.variant_ids = OrderedDict()
        self.variants = []
        self.entry_ids = OrderedDict()
        self.entries = []
        self.unresolved_tex = Counter()
        self.include_cache = {}
        self.vox_cache = {}
        self.register_special_models()
        self.register_sign_models()

    CHEST_SETS = {
        "chest": ("chest_front", "chest_side", "chest_top"),
        "trapped_chest": ("trapped_chest_front", "chest_side", "chest_top"),
        "ender_chest": ("ender_chest_front", "ender_chest_side", "ender_chest_top"),
        "copper_chest": ("copper_chest_inventory_front", "copper_chest_inventory_side", "copper_chest_inventory_top"),
        "exposed_copper_chest": ("exposed_copper_chest_inventory_front", "exposed_copper_chest_inventory_side",
                                 "exposed_copper_chest_inventory_top"),
        "weathered_copper_chest": ("weathered_copper_chest_inventory_front", "weathered_copper_chest_inventory_side",
                                   "weathered_copper_chest_inventory_top"),
        "oxidized_copper_chest": ("oxidized_copper_chest_inventory_front", "oxidized_copper_chest_inventory_side",
                                  "oxidized_copper_chest_inventory_top"),
    }

    def register_special_models(self):
        def face(tex, uv=(0, 0, 16, 16)):
            return {"texture": "#" + tex, "uv": list(uv)}
        for base, (front, side, top) in self.CHEST_SETS.items():
            for t, f in (("front", front), ("side", side), ("top", top)):
                self.alias["special/%s_%s" % (base, t)] = "textures/blocks/" + f
            self.models.raw["special/" + base] = {"textures": {
                "front": "special/%s_front" % base, "side": "special/%s_side" % base, "top": "special/%s_top" % base},
                "elements": [{"from": [1, 0, 1], "to": [15, 14, 15], "faces": {
                    "north": face("front"), "south": face("side"), "east": face("side"), "west": face("side"),
                    "up": face("top"), "down": face("top")}}]}
        for t, f in (("side", "bell_side"), ("top", "bell_top"), ("bottom", "bell_bottom")):
            self.alias["special/bell_" + t] = "textures/blocks/" + f
        bell_tex = {"side": "special/bell_side", "top": "special/bell_top", "bottom": "special/bell_bottom"}
        self.models.raw["special/bell_body"] = {"textures": bell_tex, "elements": [
            {"from": [5, 6, 5], "to": [11, 13, 11], "faces": {
                "north": face("side"), "south": face("side"), "east": face("side"), "west": face("side"),
                "up": face("top")}},
            {"from": [4, 4, 4], "to": [12, 6, 12], "faces": {
                "north": face("side"), "south": face("side"), "east": face("side"), "west": face("side"),
                "up": face("top"), "down": face("bottom")}}]}

    PLANKS = {"oak": "planks_oak", "spruce": "planks_spruce", "birch": "planks_birch", "jungle": "planks_jungle",
              "acacia": "planks_acacia", "darkoak": "planks_big_oak", "dark_oak": "planks_big_oak",
              "mangrove": "mangrove_planks", "cherry": "cherry_planks", "bamboo": "bamboo_planks",
              "crimson": "crimson_planks", "warped": "warped_planks", "pale_oak": "pale_oak_planks",
              "poplar": "poplar_planks"}

    def register_sign_models(self):
        def f(tex, uv=(0, 0, 16, 16), rot=0):
            d = {"texture": "#" + tex, "uv": list(uv)}
            if rot:
                d["rotation"] = rot
            return d

        def box(frm, to, tex="w", rot=None):
            faces = {k: f(tex) for k in ("north", "south", "east", "west", "up", "down")}
            el = {"from": frm, "to": to, "faces": faces}
            if rot:
                el["rotation"] = {"origin": [8, 8, 8], "axis": "y", "angle": rot}
            return el

        for wood, plank in self.PLANKS.items():
            self.alias["special/sign_" + wood] = "textures/blocks/" + plank
            tex = {"w": "special/sign_" + wood}
            for r in (-2, -1, 0, 1, 2):
                a = r * 22.5
                self.models.raw["special/standing_sign_%s_%d" % (wood, r)] = {"textures": tex, "elements": [
                    box([7.33, 0, 7.33], [8.67, 9, 8.67], "w", a if a else None),
                    box([0, 9, 7.33], [16, 17, 8.67], "w", a if a else None)]}
                self.models.raw["special/hanging_sign_%s_%d" % (wood, r)] = {"textures": tex, "elements": [
                    box([2, 12, 7.5], [3, 16, 8.5], "w", a if a else None),
                    box([13, 12, 7.5], [14, 16, 8.5], "w", a if a else None),
                    box([1, 2, 7], [15, 12, 9], "w", a if a else None)]}
            self.models.raw["special/wall_sign_" + wood] = {"textures": tex, "elements": [
                box([0, 4.5, 0], [16, 12.5, 1.5], "w")]}
            self.models.raw["special/arm_sign_" + wood] = {"textures": tex, "elements": [
                box([0, 14, 6.5], [16, 16, 9.5], "w"),
                box([1, 2, 7], [15, 12, 9], "w")]}
        self.alias["special/banner_cloth"] = "textures/blocks/wool_colored_white"
        self.alias["special/banner_pole"] = "textures/blocks/planks_oak"
        self.models.raw["special/standing_banner"] = {"textures": {"c": "special/banner_cloth", "p": "special/banner_pole"}, "elements": [
            box([7.5, 0, 7.5], [8.5, 16, 8.5], "p"), box([1, 0, 7.5], [15, 16, 8.5], "c")]}
        self.models.raw["special/wall_banner"] = {"textures": {"c": "special/banner_cloth", "p": "special/banner_pole"}, "elements": [
            box([0, 14, 0], [16, 16, 2], "p"), box([1, 0, 0.5], [15, 14, 1.5], "c")]}
        for t, f_ in (("head_top", "bed_head_top"), ("head_side", "bed_head_side"), ("head_end", "bed_head_end"),
                      ("feet_top", "bed_feet_top"), ("feet_side", "bed_feet_side"), ("feet_end", "bed_feet_end")):
            self.alias["special/bed_" + t] = "textures/blocks/" + f_
        self.alias["special/bed_leg"] = "textures/blocks/planks_oak"
        bt = {k: "special/bed_" + k for k in ("head_top", "head_side", "head_end", "feet_top", "feet_side", "feet_end")}
        bt["leg"] = "special/bed_leg"

        def leg(x, z):
            return box([x, 0, z], [x + 3, 3, z + 3], "leg")
        self.models.raw["special/bed_head"] = {"textures": bt, "elements": [
            {"from": [0, 3, 0], "to": [16, 9, 16], "faces": {
                "up": f("head_top", rot=90), "south": f("head_end"), "west": f("head_side"),
                "east": f("head_side", uv=(16, 0, 0, 16)), "down": f("head_top")}},
            leg(0, 13), leg(13, 13)]}
        self.models.raw["special/bed_foot"] = {"textures": bt, "elements": [
            {"from": [0, 3, 0], "to": [16, 9, 16], "faces": {
                "up": f("feet_top", rot=90), "north": f("feet_end"), "west": f("feet_side"),
                "east": f("feet_side", uv=(16, 0, 0, 16)), "down": f("feet_top")}},
            leg(0, 0), leg(13, 0)]}

    def entry_of_variants(self, vids):
        key = (0, tuple(((v, 1),) for v in vids))
        if key not in self.entry_ids:
            self.entry_ids[key] = len(self.entries)
            self.entries.append(key)
        return self.entry_ids[key]

    def special_sign_entry(self, rec):
        n = rec["name"].replace("minecraft:", "")
        st = rec["state"]
        for suffix, kind in (("_standing_sign", "standing"), ("_wall_sign", "wall"), ("_hanging_sign", "hanging"),
                             ("standing_banner", "standing_banner"), ("wall_banner", "wall_banner")):
            if n.endswith(suffix) or n == suffix.lstrip("_"):
                break
        else:
            if n == "bed":
                head = int(st.get("head_piece_bit", 0))
                d = {0: 0, 1: 270, 2: 180, 3: 90}[int(st.get("direction", 0))]
                vid = self.bake_variant("special/bed_head" if head else "special/bed_foot", 0, d, False)
                return self.entry_of_variants([vid])
            return None
        wood = n[: -len(suffix)] if n != suffix.lstrip("_") else "oak"
        if kind in ("standing_banner", "wall_banner"):
            wood = "oak"
        wood = wood.rstrip("_") or "oak"
        if wood not in self.PLANKS:
            wood = "oak"
        FD_RY = {2: 180, 3: 0, 4: 90, 5: 270}
        if kind == "standing" or (kind == "hanging" and int(st.get("hanging", 0))):
            if kind == "hanging" and not int(st.get("attached_bit", 0)):
                k = {2: 8, 3: 0, 4: 4, 5: 12}.get(int(st.get("facing_direction", 2)), 0)
            else:
                k = int(st.get("ground_sign_direction", 0)) % 16
            m = round(k / 4)
            r = k - 4 * m
            ry = (m * 90) % 360
            model = ("special/standing_sign_%s_%d" if kind == "standing" else "special/hanging_sign_%s_%d") % (wood, r)
            return self.entry_of_variants([self.bake_variant(model, 0, ry, False)])
        if kind == "hanging":
            ry = FD_RY.get(int(st.get("facing_direction", 2)), 0)
            return self.entry_of_variants([self.bake_variant("special/arm_sign_" + wood, 0, ry, False)])
        if kind == "wall":
            ry = FD_RY.get(int(st.get("facing_direction", 2)), 0)
            return self.entry_of_variants([self.bake_variant("special/wall_sign_" + wood, 0, ry, False)])
        if kind == "standing_banner":
            k = int(st.get("ground_sign_direction", 0)) % 16
            ry = (round(k / 4) * 90) % 360
            return self.entry_of_variants([self.bake_variant("special/standing_banner", 0, ry, False)])
        ry = FD_RY.get(int(st.get("facing_direction", 2)), 0)
        return self.entry_of_variants([self.bake_variant("special/wall_banner", 0, ry, False)])

    def special_entry(self, rec):
        n = rec["name"].replace("minecraft:", "")
        base = n[6:] if n.startswith("waxed_") else n
        if n in ("frame", "glow_frame"):
            return self.frame_entry(rec, n)
        if base not in self.CHEST_SETS:
            return self.special_sign_entry(rec)
        facing = rec["state"].get("minecraft:cardinal_direction", "north")
        ry = {"north": 0, "east": 90, "south": 180, "west": 270}.get(facing, 0)
        vid = self.bake_variant("special/" + base, 0, ry, False)
        key = (0, (((vid, 1),),))
        if key not in self.entry_ids:
            self.entry_ids[key] = len(self.entries)
            self.entries.append(key)
        return self.entry_ids[key]

    FRAME_ROT = {0: (90, 0), 1: (270, 0), 2: (0, 0), 3: (0, 180), 4: (0, 270), 5: (0, 90)}

    def frame_entry(self, rec, n):
        st = rec["state"]
        model = "minecraft:block/" + ("glow_item_frame" if n == "glow_frame" else "item_frame")
        if int(st.get("item_frame_map_bit", 0)):
            model += "_map"
        rx, ry = self.FRAME_ROT.get(int(st.get("facing_direction", 2)), (0, 0))
        vid = self.bake_variant(model, rx, ry, False)
        key = (0, (((vid, 1),),))
        if key not in self.entry_ids:
            self.entry_ids[key] = len(self.entries)
            self.entries.append(key)
        return self.entry_ids[key]

    def include(self, jname):
        """True when this Java block must be rendered through the model path."""
        if jname in self.include_cache:
            return self.include_cache[jname]
        res = 0
        jb = self.jb.get(jname)
        if jb and jname not in KEEP_LEGACY:
            refs = jb.all_groups()
            visible = False
            noncube = False
            for mid, x, y in refs:
                m = self.models.resolve(mid)
                if m and m["elements"]:
                    visible = True
                    if not is_full_cube(m):
                        noncube = True
            distinct = len({mid for mid, x, y in refs})
            if visible and noncube:
                res = 1
            elif visible and distinct > 1:
                res = 2
        self.include_cache[jname] = res
        return res

    def tex_index(self, jtex):
        if jtex in ("missingno",):
            return 0xFFFF
        path = None
        if jtex in self.alias:
            path = self.alias[jtex]
        elif jtex in self.texmap and (self.texmap[jtex]["diff"] <= 14
                                      or os.path.basename(self.texmap[jtex]["path"]) == os.path.basename(jtex)):
            path = self.texmap[jtex]["path"]
        elif jtex in self.texmap:
            path = self.texmap[jtex]["path"]
            self.unresolved_tex[jtex + " -> " + path] += 1
        else:
            self.unresolved_tex[jtex] += 1
            return 0xFFFF
        if path not in self.tex_ids:
            self.tex_ids[path] = len(self.tex_ids)
        return self.tex_ids[path]

    def bake_variant(self, mid, x, y, uvlock):
        key = (mid, x % 360, y % 360, bool(uvlock))
        if key in self.variant_ids:
            return self.variant_ids[key]
        quads = self.models.bake(mid, x, y, uvlock)
        out = []
        for q in quads:
            t = self.tex_index(q["tex"])
            if t == 0xFFFF:
                continue
            out.append((DIR_INDEX[q["dir"]], DIR_INDEX[q["shade_dir"]], DIR_INDEX[q["cull"]] if q["cull"] else -1,
                        q["tint"], 1 if q["shade"] else 0, min(15, int(q.get("light", 0))), t, q["pos"], q["uv"]))
        vid = len(self.variants)
        self.variants.append(out)
        self.variant_ids[key] = vid
        return vid

    def boxes_of(self, mid, x, y):
        key = (mid, x % 360, y % 360)
        if key in self.vox_cache:
            return self.vox_cache[key]
        m = self.models.resolve(mid)
        rot = block_rotation(x, y)
        boxes = []
        if m and m["elements"]:
            for e in m["elements"]:
                fr = np.array(e["from"], dtype=float) / 16
                to = np.array(e["to"], dtype=float) / 16
                corners = np.array([[c[0], c[1], c[2]] for c in itertools.product(*zip(fr, to))])
                r = e.get("rotation")
                if r:
                    pass
                corners = 0.5 + (rot @ (corners - 0.5).T).T
                boxes.append((corners.min(axis=0), corners.max(axis=0)))
        self.vox_cache[key] = boxes
        return boxes

    @staticmethod
    def voxelize(boxes):
        g = (np.arange(16) + 0.5) / 16
        X, Y, Z = np.meshgrid(g, g, g, indexing="ij")
        v = np.zeros((16, 16, 16), bool)
        for lo, hi in boxes:
            v |= ((X > lo[0] - 1e-6) & (X < hi[0] + 1e-6) & (Y > lo[1] - 1e-6) & (Y < hi[1] + 1e-6)
                  & (Z > lo[2] - 1e-6) & (Z < hi[2] + 1e-6))
        return v

    def candidate_boxes(self, jb, props):
        groups = jb.select(props)
        boxes = []
        for g in groups or []:
            mid, x, y, uvl, w = g[0]
            boxes.extend(self.boxes_of(mid, x, y))
        return boxes

    def fit(self, rec, jb, props, search):
        keys = list(search)
        combos = list(itertools.product(*[search[k] for k in keys]))
        if len(combos) == 1:
            p = dict(props)
            p.update(dict(zip(keys, combos[0])))
            return p, None
        target = self.voxelize([(np.array(b[:3]), np.minimum(np.array(b[3:]), 1.0)) for b in rec["boxes"]]) if rec["boxes"] else None
        best, bestp, bests = -1, None, None
        for c in combos:
            p = dict(props)
            p.update(dict(zip(keys, c)))
            if target is None:
                return p, None
            cand = self.voxelize(self.candidate_boxes(jb, p))
            inter = np.logical_and(cand, target).sum()
            union = np.logical_or(cand, target).sum()
            s = inter / union if union else 0.0
            if s > best + 1e-9:
                best, bestp, bests = s, p, s
        return bestp, bests

    def entry_for(self, jname, props, offset, extra=()):
        jb = self.jb[jname]
        groups = jb.select(props)
        if groups is None:
            return None
        groups = list(groups) + [[(mid, x, y, False, 1)] for (mid, x, y) in extra]
        gl = []
        total = 0
        for g in groups:
            choices = []
            for (mid, x, y, uvl, w) in g:
                vid = self.bake_variant(mid, x, y, uvl)
                choices.append((vid, min(255, w)))
                total += len(self.variants[vid])
            gl.append(tuple(choices))
        if total == 0:
            return None
        key = (offset, tuple(gl))
        if key not in self.entry_ids:
            self.entry_ids[key] = len(self.entries)
            self.entries.append(key)
        return self.entry_ids[key]

    def run(self):
        recs = load_records()
        blocks = []
        qmax = {}
        badval = Counter()
        stats = Counter()
        fitlog = Counter()
        poor = []
        missing_props = Counter()
        for rec in recs:
            sid = self.special_entry(rec)
            if sid is not None:
                blocks.append((rec["hash"], sid))
                stats["special"] += 1
                continue
            tr = translate(rec["name"], rec["state"], self.jb)
            if tr is None:
                stats["no-translation"] += 1
                continue
            jname, props, search = tr
            if jname not in self.jb:
                stats["no-java-block:" + jname] += 1
                continue
            if not self.include(jname):
                stats["legacy-path"] += 1
                continue
            jb = self.jb[jname]
            for p in list(props):
                if p not in jb.values:
                    missing_props[(jname, p)] += 1
                    del props[p]
            for p, v in props.items():
                if v not in jb.values[p] and v not in ('false', 'none'):
                    badval[(jname, p, v)] += 1
            for p in list(search):
                if p not in jb.values:
                    del search[p]
            if search:
                props, score = self.fit(rec, jb, props, search)
                if score is not None:
                    fitlog[round(score, 1)] += 1
                    if score < 0.6:
                        poor.append((rec["name"], rec["state"], props, round(score, 2)))
            offset = 0
            if jname in OFFSET_XZ:
                offset = 1
            elif jname in OFFSET_XYZ:
                offset = 2
            elif jname in OFFSET_DRIPSTONE:
                offset = 3
            extra = [("special/bell_body", 0, 0)] if jname == "bell" else ()
            eid = self.entry_for(jname, props, offset | (0x80 if self.include(jname) == 2 else 0), extra)
            if eid is None:
                stats["empty:" + jname] += 1
                continue
            blocks.append((rec["hash"], eid))
            qn = sum(len(self.variants[g[0][0]]) for g in self.entries[eid][1])
            qmax[rec["name"]] = max(qmax.get(rec["name"], 0), qn)
            stats["emitted"] += 1
        print('values outside java domain:', dict(badval.most_common(60)))
        self.write(blocks)
        print('heaviest blocks (quads):', sorted(qmax.items(), key=lambda kv: -kv[1])[:40])
        print("emitted", len(blocks), "block states;", len(self.variants), "variants;", len(self.entries),
              "entries;", len(self.tex_ids), "textures")
        for k, v in sorted(stats.items(), key=lambda kv: -kv[1])[:60]:
            print("  ", k, v)
        print("fit score histogram:", sorted(fitlog.items()))
        print("poor fits:", len(poor))
        for p in poor[:40]:
            print("   ", p)
        print("props not on java block:", dict(missing_props.most_common(40)))
        print("unresolved/poor textures:", dict(self.unresolved_tex.most_common(80)))

    def write(self, blocks):
        out = bytearray()
        out += b"PBM1"
        out += struct.pack("<I", len(self.tex_ids))
        for path in self.tex_ids:
            bts = path.encode()
            out += struct.pack("<H", len(bts)) + bts
        out += struct.pack("<I", len(self.variants))
        for quads in self.variants:
            out += struct.pack("<H", len(quads))
            for (d, sd, cull, tint, shade, light, tex, pos, uv) in quads:
                out += struct.pack("<BBbbBBH", d, sd, cull, tint, shade, light, tex)
                for p in pos:
                    out += struct.pack("<3f", *p)
                for uvp in uv:
                    out += struct.pack("<2f", *uvp)
        out += struct.pack("<I", len(self.entries))
        for (offset, groups) in self.entries:
            out += struct.pack("<BB", offset, len(groups))
            for g in groups:
                out += struct.pack("<B", len(g))
                for vid, w in g:
                    out += struct.pack("<IB", vid, w)
        out += struct.pack("<I", len(blocks))
        for h, eid in blocks:
            out += struct.pack("<iI", h if h < 2 ** 31 else h - 2 ** 32, eid)
        os.makedirs(os.path.dirname(os.path.abspath(OUT)), exist_ok=True)
        open(OUT, "wb").write(out)
        print("wrote", OUT, len(out), "bytes")

if __name__ == "__main__":
    Gen().run()
