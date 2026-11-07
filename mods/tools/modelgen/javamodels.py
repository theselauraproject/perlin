"""Bakes Java block models (blockstates + models) into oriented quads.

Mirrors net.minecraft.client.resources.model.cuboid.FaceBakery / BlockModelRotation (26.x):
vertex order per face (FaceInfo), element rotation (axis/angle + rescale, or Euler xyz),
blockstate x/y rotation about the block centre, uvlock via the inverse face transformation,
and face winding recalculation for unrotated elements.
"""
import json
import math
import os

import numpy as np

DIRS = ["down", "up", "north", "south", "west", "east"]
DIR_VEC = {
    "down": (0, -1, 0), "up": (0, 1, 0), "north": (0, 0, -1),
    "south": (0, 0, 1), "west": (-1, 0, 0), "east": (1, 0, 0),
}
FACE_VERTS = {
    "down": [(0, 0, 1), (0, 0, 0), (1, 0, 0), (1, 0, 1)],
    "up": [(0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0)],
    "north": [(1, 1, 0), (1, 0, 0), (0, 0, 0), (0, 1, 0)],
    "south": [(0, 1, 1), (0, 0, 1), (1, 0, 1), (1, 1, 1)],
    "west": [(0, 1, 0), (0, 0, 0), (0, 0, 1), (0, 1, 1)],
    "east": [(1, 1, 1), (1, 0, 1), (1, 0, 0), (1, 1, 0)],
}
def _rot(axis, angle):
    c, s = math.cos(angle), math.sin(angle)
    if axis == "x":
        return np.array([[1, 0, 0], [0, c, -s], [0, s, c]], dtype=np.float64)
    if axis == "y":
        return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], dtype=np.float64)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]], dtype=np.float64)

LOCAL_TO_GLOBAL = {
    "south": np.eye(3),
    "east": _rot("y", math.pi / 2),
    "west": _rot("y", -math.pi / 2),
    "north": _rot("y", math.pi),
    "up": _rot("x", -math.pi / 2),
    "down": _rot("x", math.pi / 2),
}
GLOBAL_TO_LOCAL = {k: v.T for k, v in LOCAL_TO_GLOBAL.items()}

def nearest_dir(v):
    best, bd = "up", -1e9
    for d in DIRS:
        dot = float(np.dot(v, DIR_VEC[d]))
        if dot > bd + 1e-9:
            best, bd = d, dot
    return best

def block_rotation(x, y):
    """BlockModelRotation: Ry(-y) * Rx(-x)."""
    return _rot("y", -math.radians(y)) @ _rot("x", -math.radians(x))

class Models:
    def __init__(self, root):
        self.root = root
        self.raw = {}
        self.resolved = {}

    def _load(self, mid):
        mid = mid.replace("minecraft:", "")
        if mid not in self.raw:
            p = os.path.join(self.root, "models", mid + ".json")
            self.raw[mid] = json.load(open(p)) if os.path.exists(p) else None
        return self.raw[mid]

    def resolve(self, mid):
        mid = mid.replace("minecraft:", "")
        if mid in self.resolved:
            return self.resolved[mid]
        d = self._load(mid)
        if d is None:
            self.resolved[mid] = None
            return None
        parent = self.resolve(d["parent"]) if "parent" in d else None
        out = {"textures": {}, "elements": None, "ao": True}
        if parent:
            out["textures"].update(parent["textures"])
            out["elements"] = parent["elements"]
            out["ao"] = parent["ao"]
        out["textures"].update(d.get("textures", {}))
        if "elements" in d:
            out["elements"] = d["elements"]
        if "ambientocclusion" in d:
            out["ao"] = d["ambientocclusion"]
        self.resolved[mid] = out
        return out

    @staticmethod
    def tex(model, ref):
        seen = 0
        while seen < 16:
            if isinstance(ref, dict):
                ref = ref.get("sprite", "missingno")
            if isinstance(ref, str) and ref.startswith("#"):
                ref = model["textures"].get(ref[1:], "missingno")
                seen += 1
                continue
            break
        return ref.replace("minecraft:", "")

    def bake(self, mid, rx=0, ry=0, uvlock=False):
        """Returns a list of quad dicts for model `mid` with blockstate rotation (rx, ry)."""
        model = self.resolve(mid)
        if not model or not model["elements"]:
            return []
        rot = block_rotation(rx, ry)
        has_rot = (rx % 360) != 0 or (ry % 360) != 0
        quads = []
        for el in model["elements"]:
            fr = np.array(el["from"], dtype=np.float64)
            to = np.array(el["to"], dtype=np.float64)
            erot = el.get("rotation")
            emat = None
            eorigin = None
            if erot:
                eorigin = np.array(erot["origin"], dtype=np.float64) / 16.0
                if "axis" in erot:
                    ang = math.radians(erot["angle"])
                    if erot["angle"] == 0:
                        emat = np.eye(3)
                    else:
                        emat = _rot(erot["axis"], ang)
                else:
                    emat = (_rot("z", math.radians(erot.get("z", 0))) @ _rot("y", math.radians(erot.get("y", 0)))
                            @ _rot("x", math.radians(erot.get("x", 0))))
                if erot.get("rescale") and not np.allclose(emat, np.eye(3)):
                    sc = []
                    for a in range(3):
                        e = np.zeros(3)
                        e[a] = 1
                        t = np.abs(emat @ e)
                        sc.append(1.0 / t.max())
                    emat = emat @ np.diag(sc)
            shade_override = el.get("shade_direction_override")
            for dname, face in el["faces"].items():
                tex = self.tex(model, face["texture"])
                uvs = face.get("uv")
                if uvs is None:
                    uvs = default_uv(fr, to, dname)
                fr_rot = (face.get("rotation", 0) // 90) % 4
                uvt = None
                if uvlock and has_rot:
                    uvt = inverse_face_transform(rot, dname)
                pos = []
                uv = []
                for i in range(4):
                    vm = FACE_VERTS[dname][i]
                    v = np.array([to[a] if vm[a] else fr[a] for a in range(3)]) / 16.0
                    if emat is not None:
                        v = eorigin + emat @ (v - eorigin)
                    if has_rot:
                        v = 0.5 + rot @ (v - 0.5)
                    ri = (i + fr_rot) % 4
                    ru = (uvs[2] if ri not in (0, 1) else uvs[0]) / 16.0
                    rv = (uvs[3] if ri not in (0, 3) else uvs[1]) / 16.0
                    if uvt is not None:
                        t = uvt @ np.array([ru - 0.5, rv - 0.5, 0.0])
                        ru, rv = t[0] + 0.5, t[1] + 0.5
                    pos.append(v)
                    uv.append((ru * 16.0, rv * 16.0))
                nrm = np.cross(pos[1] - pos[0], pos[2] - pos[0])
                n = float(np.linalg.norm(nrm))
                final = nearest_dir(nrm / n) if n > 1e-9 else "up"
                if erot is None:
                    pos, uv = recalc_winding(pos, uv, final)
                shade_dir = final
                if shade_override:
                    shade_dir = shade_override
                cull = face.get("cullface")
                if cull and has_rot:
                    cull = nearest_dir(rot @ np.array(DIR_VEC[cull], dtype=np.float64))
                quads.append({
                    "dir": final, "cull": cull, "tex": tex, "tint": face.get("tintindex", -1),
                    "shade": el.get("shade", True), "shade_dir": shade_dir,
                    "pos": [[float(c) for c in p] for p in pos], "uv": [[float(a), float(b)] for a, b in uv],
                    "light": el.get("light_emission", 0),
                })
        return quads

def default_uv(fr, to, d):
    if d == "down":
        return [fr[0], 16 - to[2], to[0], 16 - fr[2]]
    if d == "up":
        return [fr[0], fr[2], to[0], to[2]]
    if d == "north":
        return [16 - to[0], 16 - to[1], 16 - fr[0], 16 - fr[1]]
    if d == "south":
        return [fr[0], 16 - to[1], to[0], 16 - fr[1]]
    if d == "west":
        return [fr[2], 16 - to[1], to[2], 16 - fr[1]]
    return [16 - to[2], 16 - to[1], 16 - fr[2], 16 - fr[1]]

def inverse_face_transform(rot, d):
    """BlockMath.getFaceTransformation(...).invert, for a pure rotation matrix `rot`."""
    action = rot @ LOCAL_TO_GLOBAL[d]
    normal = action @ np.array([0.0, 0.0, 1.0])
    new_side = nearest_dir(normal)
    face_t = GLOBAL_TO_LOCAL[new_side] @ action
    return face_t.T

def recalc_winding(pos, uv, final):
    mn = np.min(pos, axis=0)
    mx = np.max(pos, axis=0)
    pos = [p.copy() for p in pos]
    uv = list(uv)
    for vtx in range(4):
        vm = FACE_VERTS[final][vtx]
        target = np.array([mx[a] if vm[a] else mn[a] for a in range(3)])
        swap = -1
        for j in range(vtx, 4):
            if np.all(pos[j] == target):
                swap = j
                break
        if swap == -1:
            for j in range(vtx, 4):
                if np.allclose(pos[j], target, atol=1e-6):
                    swap = j
                    break
        if swap == -1:
            return pos, uv
        if swap != vtx:
            pos[swap], pos[vtx] = pos[vtx], pos[swap]
            uv[swap], uv[vtx] = uv[vtx], uv[swap]
    return pos, uv
