#!/usr/bin/env python3
import os
import re
import sys

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')

LAYERS = [
    ("foundation", ["util", "render", "network", "world/level", "world/movement", "client/screen",
                    "client/systems/bindings", "client/systems/camera", "client/systems/diagnostics"]),
    ("services", ["client/systems/config", "client/services/account", "client/services/control",
                  "client/services/discord", "client/systems/model", "client/systems/mods",
                  "client/systems/replay", "client/systems/resources", "client/services/update",
                  "client/systems/i18n"]),
    ("content", ["client/systems/blocks", "client/systems/text", "client/systems/gfx", "client/systems/audio",
                 "client/systems/preview", "client/systems/sprites", "client/systems/attachable",
                 "client/systems/javashaders"]),
    ("simulation", ["client/systems/multiplayer", "client/systems/input", "client/systems/particle",
                    "client/systems/hand", "client/systems/entity", "client/systems/player"]),
    ("presentation", ["client/ui", "client/systems/renderer"]),
    ("screens", ["client/screen/screens"]),
    ("features", ["client/features/mods", "client/features/audio", "client/features/replay",
                 "client/features/servers", "client/features/packs", "client/features/debug", "client/features/interaction",
                 "client/features/singleplayer"]),
    ("host", ["client/platform", "client/features/worldview"]),
    ("application", ["client/application", "client/selftest", "."]),
]
LAYER_OF = {m: i for i, (_, ms) in enumerate(LAYERS) for m in ms}

FORBIDDEN = [
    ("util", "util must not depend on anything else", lambda s, t: s == "util" and t != "util"),
    ("network", "network may depend only on util", lambda s, t: s == "network" and t not in ("network", "util")),
    ("world", "world must not depend on client or network",
     lambda s, t: s.startswith("world/") and (t.startswith("client/") or t == "network")),
    ("screen", "client/screen (manager and interfaces) must be self-contained",
     lambda s, t: s == "client/screen" and t != "client/screen"),
    ("ui", "client/ui holds reusable components and must not include navigable screens",
     lambda s, t: s == "client/ui" and t in ("client/screen", "client/screen/screens")),
    ("screens", "screens compose ui and systems; they must not include services or the application",
     lambda s, t: s == "client/screen/screens" and (t.startswith("client/services") or t == "client/application")),
    ("services", "services must not depend on screens, ui or the application",
     lambda s, t: s.startswith("client/services/") and
     (t in ("client/application", "client/ui", "client/screen", "client/screen/screens") or t == "client/platform")),
    ("systems", "systems must not depend on services, screens or the application",
     lambda s, t: s.startswith("client/systems/") and
     (t.startswith("client/services/") or t in ("client/application", "client/screen/screens"))),
    ("features", "features compose systems and ui; they must not include services, screens or the application",
     lambda s, t: s.startswith("client/features/") and not s.startswith("client/features/worldview") and
     (t.startswith("client/services/") or t in ("client/application", "client/screen/screens", "client/platform"))),
    ("app", "only client/application and main.cpp may include client/application",
     lambda s, t: t == "client/application" and s not in ("client/application", ".")),
]


def module(rel):
    parts = rel.split("/")
    if len(parts) == 1:
        return "."
    if parts[0] == "client" and len(parts) > 2:
        if parts[1] in ("systems", "services", "features") and len(parts) > 3:
            return "client/" + parts[1] + "/" + parts[2]
        if parts[1] == "screen" and parts[2] == "screens":
            return "client/screen/screens"
        return "client/" + parts[1]
    if parts[0] == "world" and len(parts) > 2:
        return "world/" + parts[1]
    return parts[0]


def load(src):
    files = {}
    for directory, _, names in os.walk(src):
        for name in names:
            if name.endswith((".cpp", ".hpp", ".h", ".inc")):
                full = os.path.join(directory, name)
                files[os.path.relpath(full, src).replace(os.sep, "/")] = full
    edges = {}
    for rel, full in files.items():
        with open(full, errors="ignore") as handle:
            for line in handle:
                match = INCLUDE.match(line)
                if not match:
                    continue
                target = match.group(1)
                local = os.path.normpath(os.path.join(os.path.dirname(rel), target)).replace(os.sep, "/")
                resolved = target if target in files else local if local in files else None
                if resolved:
                    edges.setdefault((module(rel), module(resolved)), []).append((rel, resolved))
    return edges


def components(edges):
    graph = {}
    for (a, b) in edges:
        if a != b:
            graph.setdefault(a, set()).add(b)
            graph.setdefault(b, set())
    index, low, stack, on, out = {}, {}, [], set(), []
    counter = [0]
    sys.setrecursionlimit(10000)

    def visit(v):
        index[v] = low[v] = counter[0]
        counter[0] += 1
        stack.append(v)
        on.add(v)
        for w in graph[v]:
            if w not in index:
                visit(w)
                low[v] = min(low[v], low[w])
            elif w in on:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            comp = set()
            while True:
                w = stack.pop()
                on.discard(w)
                comp.add(w)
                if w == v:
                    break
            if len(comp) > 1:
                out.append(comp)

    for v in list(graph):
        if v not in index:
            visit(v)
    return out


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    edges = load(os.path.join(root, "src"))
    problems = []
    for name, why, bad in FORBIDDEN:
        for (a, b), uses in edges.items():
            if bad(a, b):
                for rel, target in uses:
                    problems.append(f"[{name}] {why}: {rel} -> {target}")
    for comp in components(edges):
        problems.append("[cycle] dependency cycle among " + ", ".join(sorted(comp)))
    for (a, b), uses in edges.items():
        if a == b:
            continue
        for module_name in (a, b):
            if module_name not in LAYER_OF:
                problems.append(f"[layer] module {module_name} has no declared layer")
        if a in LAYER_OF and b in LAYER_OF and LAYER_OF[b] > LAYER_OF[a]:
            for rel, target in uses:
                problems.append(f"[layer] {LAYERS[LAYER_OF[a]][0]} module {a} includes "
                                f"{LAYERS[LAYER_OF[b]][0]} module {b}: {rel} -> {target}")
    for line in problems:
        print(line)
    if problems:
        print(f"{len(problems)} layering violation(s)")
        return 1
    print("layering ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
