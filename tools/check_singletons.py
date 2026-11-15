#!/usr/bin/env python3
"""Ratchet for hidden-global access.

Counts `Type::Instance()` call sites per module and compares with tools/singleton_baseline.json.
The count may only go down: a new singleton use anywhere fails the build, and a decrease must be
recorded by running this script with --update so the improvement is locked in.
"""
import json
import os
import re
import sys

CALL = re.compile(r'\b[A-Z]\w*(?:::\w+)*::Instance\(\)')


def module(rel):
    parts = rel.split("/")
    if len(parts) == 1:
        return "."
    if parts[0] == "client" and len(parts) > 2:
        if parts[1] in ("systems", "services") and len(parts) > 3:
            return "client/" + parts[1] + "/" + parts[2]
        if parts[1] == "screen" and parts[2] == "screens":
            return "client/screen/screens"
        return "client/" + parts[1]
    if parts[0] == "world" and len(parts) > 2:
        return "world/" + parts[1]
    return parts[0]


def count(root):
    counts = {}
    src = os.path.join(root, "src")
    for base, _, files in os.walk(src):
        for name in files:
            if not name.endswith((".cpp", ".hpp", ".inc", ".mm")):
                continue
            path = os.path.join(base, name)
            rel = os.path.relpath(path, src).replace(os.sep, "/")
            with open(path, errors="ignore") as f:
                n = len(CALL.findall(f.read()))
            if n:
                counts[module(rel)] = counts.get(module(rel), 0) + n
    return counts


def main():
    root = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else "."
    baseline_path = os.path.join(root, "tools", "singleton_baseline.json")
    now = count(root)
    if "--update" in sys.argv:
        with open(baseline_path, "w") as f:
            json.dump(dict(sorted(now.items())), f, indent=2)
            f.write("\n")
        print("baseline updated:", sum(now.values()), "uses")
        return 0
    with open(baseline_path) as f:
        base = json.load(f)
    bad = 0
    for mod in sorted(set(now) | set(base)):
        a, b = now.get(mod, 0), base.get(mod, 0)
        if a > b:
            print(f"FAIL {mod}: {a} Instance() uses, baseline {b}. Pass the dependency in instead.")
            bad += 1
        elif a < b:
            print(f"FAIL {mod}: {a} uses, baseline {b}. Improvement - run tools/check_singletons.py --update to lock it in.")
            bad += 1
    if bad:
        return 1
    print("singleton ratchet ok:", sum(now.values()), "uses across", len(now), "modules")
    return 0


if __name__ == "__main__":
    sys.exit(main())
