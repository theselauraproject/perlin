#!/usr/bin/env python3
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
RES = ROOT / "resources"
LAYOUT = {
    "ui": {"screens", "overlays", "components", "styles", "defaults"},
    "data": {"blocks", "items", "lighting", "geometry", "fonts"},
    "branding": set(),
    "control": set(),
    "localization": set(),
}
BUILD_ARTIFACTS = {"perlin_shaders.pack"}
KNOWN_MISSING = {"spyglass_model.png", "spyglass_scope.png"}
SEARCH = ["src", "server", "tools", "tests", "cmake", "CMakeLists.txt", "resources/ui"]
SUFFIXES = {".cpp", ".hpp", ".h", ".py", ".cmake", ".txt", ".rml", ".rcss"}
LITERAL = re.compile(r'"((?:ui/(?:screens|overlays|components|styles|defaults)|data|branding|control|localization)/[\w./-]+\.\w+)"')
READ = re.compile(r'(?:resources::(?:Read|ReadText|ReadRequiredText)|ui::ReadMarkup)\(\s*"([^"]*)"')
EMBED = re.compile(r'\{"([^"]+)",\s*perlin_res_\d+')


def sources():
    for entry in SEARCH:
        p = ROOT / entry
        if p.is_file():
            yield p
        elif p.is_dir():
            for f in p.rglob("*"):
                if f.is_file() and f.suffix in SUFFIXES and "build" not in f.parts:
                    yield f


def main():
    failures = []
    files = sorted(p for p in RES.rglob("*") if p.is_file())
    rels = {p.relative_to(RES).as_posix() for p in files}

    for p in RES.iterdir():
        if p.is_file():
            failures.append(f"resources/{p.name}: loose file; every resource belongs in a domain directory")
        elif p.name not in LAYOUT:
            failures.append(f"resources/{p.name}/: not a known resource domain ({', '.join(sorted(LAYOUT))})")
    for domain, subs in LAYOUT.items():
        base = RES / domain
        if not base.is_dir():
            failures.append(f"resources/{domain}/ is missing")
            continue
        for child in base.iterdir():
            if child.is_dir() and child.name not in subs:
                failures.append(f"resources/{domain}/{child.name}/: not a known subdirectory")
            if child.is_file() and subs:
                failures.append(f"resources/{domain}/{child.name}: belongs in one of {sorted(subs)}")
    for d in sorted(p for p in RES.rglob("*") if p.is_dir()):
        if not any(d.rglob("*")) or not any(c.is_file() for c in d.rglob("*")):
            failures.append(f"{d.relative_to(ROOT)}: empty directory")

    texts = {}
    for f in sources():
        texts[f] = f.read_text(encoding="utf-8", errors="replace")
    blob = "\n".join(texts.values())
    for rel in sorted(rels):
        if rel not in blob:
            failures.append(f"resources/{rel}: no reference anywhere in src, server, tools, tests or CMake")

    for f, text in texts.items():
        shown = f.relative_to(ROOT)
        for m in LITERAL.finditer(text):
            if m.group(1) not in rels and (RES / m.group(1)).is_file() is False:
                line = text.count("\n", 0, m.start()) + 1
                failures.append(f"{shown}:{line}: resource path {m.group(1)} does not exist")
        if f.suffix in (".cpp", ".hpp"):
            for m in READ.finditer(text):
                if "/" not in m.group(1) and m.group(1) not in BUILD_ARTIFACTS | KNOWN_MISSING:
                    line = text.count("\n", 0, m.start()) + 1
                    failures.append(f"{shown}:{line}: flat resource name {m.group(1)}; use the path under resources/")

    if len(sys.argv) > 1:
        table = pathlib.Path(sys.argv[1])
        if not table.is_file():
            failures.append(f"{table}: embedded resource table not generated")
        else:
            embedded = set(EMBED.findall(table.read_text()))
            embedded -= BUILD_ARTIFACTS
            for rel in sorted(rels - embedded):
                failures.append(f"{rel}: not in the embedded resource table")
            for rel in sorted(embedded - rels):
                failures.append(f"{rel}: embedded but not under resources/")

    for line in failures:
        print(line)
    if failures:
        print(f"{len(failures)} resource layout problem(s)")
        return 1
    print(f"resources ok: {len(rels)} files in {len(LAYOUT)} domains")
    return 0


if __name__ == "__main__":
    sys.exit(main())
