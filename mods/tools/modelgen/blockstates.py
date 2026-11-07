"""Java blockstate definitions: property domains and variant / multipart selection."""
import json
import os

def _as_list(x):
    return x if isinstance(x, list) else [x]

def _parse_key(key):
    if key == "":
        return {}
    return dict(kv.split("=", 1) for kv in key.split(","))

def _match_when(when, props):
    if "OR" in when:
        return any(_match_when(w, props) for w in when["OR"])
    if "AND" in when:
        return all(_match_when(w, props) for w in when["AND"])
    for k, v in when.items():
        want = str(v).lower() if isinstance(v, bool) else str(v)
        have = props.get(k)
        if have is None or have not in want.split("|"):
            return False
    return True

class JavaBlock:
    def __init__(self, name, data):
        self.name = name
        self.data = data
        self.values = {}
        if "variants" in data:
            self.kind = "variants"
            self.variants = [(_parse_key(k), _as_list(v)) for k, v in data["variants"].items()]
            for k, _ in self.variants:
                for p, v in k.items():
                    self.values.setdefault(p, [])
                    if v not in self.values[p]:
                        self.values[p].append(v)
        else:
            self.kind = "multipart"
            self.parts = data["multipart"]
            for part in self.parts:
                w = part.get("when")
                if not w:
                    continue
                stack = [w]
                while stack:
                    c = stack.pop()
                    if "OR" in c:
                        stack.extend(c["OR"])
                    elif "AND" in c:
                        stack.extend(c["AND"])
                    else:
                        for p, v in c.items():
                            self.values.setdefault(p, [])
                            for x in str(v).lower().split("|") if isinstance(v, bool) else str(v).split("|"):
                                if x not in self.values[p]:
                                    self.values[p].append(x)

    def select(self, props):
        """Returns a list of groups; each group is a list of weighted model refs
        (model, x, y, uvlock, weight).  Unknown properties fall back to defaults."""
        full = dict(props)
        for p, vals in self.values.items():
            if p not in full or (self.kind == "variants" and full[p] not in vals):
                full[p] = default_value(p, vals)
        if self.kind == "variants":
            for k, group in self.variants:
                if all(full.get(p) == v for p, v in k.items()):
                    return [self._group(group)]
            return None
        out = []
        for part in self.parts:
            w = part.get("when")
            if w is None or _match_when(w, full):
                out.append(self._group(_as_list(part["apply"])))
        return out

    @staticmethod
    def _group(variants):
        return [(v["model"].replace("minecraft:", ""), v.get("x", 0), v.get("y", 0), v.get("uvlock", False),
                 v.get("weight", 1)) for v in variants]

    def all_groups(self):
        """Every distinct model ref in the definition (for classification)."""
        refs = set()
        if self.kind == "variants":
            for _, g in self.variants:
                for v in g:
                    refs.add((v["model"].replace("minecraft:", ""), v.get("x", 0), v.get("y", 0)))
        else:
            for part in self.parts:
                for v in _as_list(part["apply"]):
                    refs.add((v["model"].replace("minecraft:", ""), v.get("x", 0), v.get("y", 0)))
        return refs

def default_value(prop, values):
    for pref in ("false", "none", "bottom", "floor", "lower", "0", "straight", "north", "normal", "x"):
        if pref in values:
            return pref
    return sorted(values)[0] if values else ""

def load_all(root):
    d = os.path.join(root, "blockstates")
    out = {}
    for f in os.listdir(d):
        if f.endswith(".json"):
            out[f[:-5]] = JavaBlock(f[:-5], json.load(open(os.path.join(d, f))))
    return out
