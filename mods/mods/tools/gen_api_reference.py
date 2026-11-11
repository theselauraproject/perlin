#!/usr/bin/env python3
"""Regenerates the reference sections at the end of README.md from the SDK headers and the host's operation table."""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
sdk = root / "sdk" / "perlin"
host = root.parent / "src" / "client"
BEGIN = "<!-- API-REFERENCE-BEGIN -->"
END = "<!-- API-REFERENCE-END -->"


def comment_above(lines, i):
    out = []
    j = i - 1
    while j >= 0 and lines[j].strip().startswith("//"):
        out.append(lines[j].strip()[2:].strip())
        j -= 1
    return " ".join(reversed(out))


def functions(path, ns):
    lines = path.read_text().splitlines()
    rows = []
    for i, line in enumerate(lines):
        m = re.match(r"^    inline (?!constexpr)(.+)$", line)
        if not m:
            continue
        sig = m.group(1).rstrip().rstrip(";").rstrip()
        sig = re.sub(r"\s\{(?!\}).*$", "", sig).rstrip()
        if "(" not in sig:
            continue
        doc = re.sub(r"-{3,}.*?-{3,}\s*", "", comment_above(lines, i)).strip()
        rows.append((sig, doc))
    return rows


def enums(path):
    text = path.read_text()
    out = []
    for m in re.finditer(r"enum class (\w+)(?:\s*:\s*\w+)?\s*\{(.*?)\};", text, re.S):
        body = re.sub(r"//.*", "", m.group(2))
        names = [re.split(r"\s*=", n.strip())[0] for n in body.replace("\n", " ").split(",") if n.strip()]
        out.append((m.group(1), names))
    return out


def structs(path):
    text = path.read_text()
    out = []
    for m in re.finditer(r"^    struct (\w+)(?: : [\w:<>, ]+)? \{\n(.*?)^    \};", text, re.S | re.M):
        fields = []
        for line in m.group(2).splitlines():
            code = line.split("//")[0].strip()
            if not code or "(" in code or code.startswith(("using", "static", "template", "return", "if", "}")):
                continue
            if code.endswith(";"):
                note = line.split("//", 1)[1].strip() if "//" in line else ""
                fields.append((code[:-1], note))
        if fields:
            out.append((m.group(1), fields))
    return out


def ops():
    abi = (sdk / "abi.hpp").read_text()
    m = re.search(r"enum class Op : \w+ \{(.*?)\};", abi, re.S)
    table = {}
    group = ""
    for line in m.group(1).splitlines():
        s = line.strip()
        if s.startswith("//"):
            group = s[2:].strip()
            continue
        mm = re.match(r"(\w+) = (0x[0-9A-Fa-f]+),?\s*(?://\s*(.*))?", s)
        if mm:
            table[mm.group(1)] = (mm.group(2), mm.group(3) or "", group)
    perms = {}
    for f in host.glob("PerlinMods*.cpp"):
        t = f.read_text()
        for r in re.finditer(r"mm\.Register\(Op::(\w+),", t):
            end = t.find("\n        }", r.end())
            tail = t[end:end + 200]
            p = re.match(r"\n        \}, ([^;]+?)\);", tail)
            perms[r.group(1)] = p.group(1).strip() if p else ""
    return table, perms


def main():
    parts = [BEGIN, ""]
    parts.append("## Reference\n")
    parts.append("Generated from the SDK headers by `mods/tools/gen_api_reference.py`; run it after changing the API.\n")

    parts.append("### Permissions per operation\n")
    table, perms = ops()
    perm_names = {"network": "network", "packets": "packets", "control": "control", "files": "files", "native": "native"}
    parts.append("| Op | Id | Needs |\n| --- | --- | --- |")
    for name, (num, note, group) in table.items():
        need = perms.get(name, "")
        shown = ", ".join(sorted({p for p in perm_names if re.search(r"\b" + p + r"\b", need, re.I)})) or "always allowed"
        parts.append(f"| `{name}` | `{num}` | {shown} |")
    parts.append("")

    parts.append("### Functions\n")
    for header, ns in (("game.hpp", "game"), ("adv.hpp", "game"), ("act.hpp", "act"), ("io.hpp", "fs, Config, net"), ("ui.hpp", "ui"), ("settings.hpp", "settings"), ("types.hpp", "types"), ("runtime.hpp", "runtime")):
        rows = functions(sdk / header, ns)
        if not rows:
            continue
        parts.append(f"#### `perlin/{header}`\n")
        parts.append("| Signature | Notes |\n| --- | --- |")
        for sig, doc in rows:
            parts.append("| `" + sig.replace("|", "\\|") + "` | " + doc.replace("|", "\\|") + " |")
        parts.append("")

    parts.append("### Enumerations\n")
    for name, vals in enums(sdk / "abi.hpp"):
        if name in ("Op", "Event"):
            continue
        parts.append(f"* `{name}`: " + ", ".join(f"`{v}`" for v in vals if v))
    parts.append("")

    parts.append("### Data types\n")
    for header in ("abi.hpp", "types.hpp"):
        for name, fields in structs(sdk / header):
            if len(fields) > 60:
                continue
            parts.append(f"#### `{name}`\n")
            parts.append("| Field | Notes |\n| --- | --- |")
            for decl, note in fields:
                parts.append(f"| `{decl}` | {note} |")
            parts.append("")
    parts.append(END)
    block = "\n".join(parts) + "\n"

    readme = root / "README.md"
    text = readme.read_text()
    if BEGIN in text:
        text = text[: text.index(BEGIN)] + block
    else:
        text = text.rstrip() + "\n\n" + block
    readme.write_text(text)
    print(len(block.splitlines()), "lines")


main()
