#!/usr/bin/env python3
"""Turns the "settings" of a mod's JSON manifest into a C++ header of typed handles.

    perlin_settings.py waila.json mod_settings.hpp

The mod then writes `settings::visible` instead of looking values up by name, and a choice becomes a real enum whose
enumerators are the choice values in the JSON. build.sh runs this for you when the mod has a manifest next to its source.
"""
import json
import keyword
import re
import sys

CXX_KEYWORDS = set(keyword.kwlist) | {
    "alignas", "alignof", "and", "auto", "bool", "break", "case", "catch", "char", "class", "const", "continue", "default",
    "delete", "do", "double", "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if",
    "inline", "int", "long", "namespace", "new", "not", "operator", "or", "private", "protected", "public", "register",
    "return", "short", "signed", "sizeof", "static", "struct", "switch", "template", "this", "throw", "true", "try", "typedef",
    "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "while", "xor",
}
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
HANDLES = {"toggle": "perlin::Toggle", "integer": "perlin::Integer", "number": "perlin::Number", "button": "perlin::Button"}


def fail(message):
    print(f"perlin_settings: {message}", file=sys.stderr)
    sys.exit(1)


def pascal(text):
    return "".join(part[:1].upper() + part[1:] for part in re.split(r"[_\-. ]+", text) if part)


def cxx_name(setting_id):
    name = re.sub(r"[^A-Za-z0-9_]", "_", setting_id)
    if not name or name[0].isdigit() or name in CXX_KEYWORDS:
        name = "_" + name
    return name


def main():
    if len(sys.argv) != 3:
        fail("usage: perlin_settings.py <manifest.json> <output.hpp>")
    manifest_path, out_path = sys.argv[1], sys.argv[2]
    with open(manifest_path, encoding="utf-8") as f:
        manifest = json.load(f)

    enums, handles, seen = [], [], set()
    for entry in manifest.get("settings", []):
        kind = entry.get("type")
        if kind in ("label", "header"):
            continue
        setting_id = entry.get("id")
        if not setting_id:
            fail(f"a {kind} setting has no id")
        name = cxx_name(setting_id)
        if name in seen:
            fail(f"two settings are called {name}")
        seen.add(name)
        if kind == "choice":
            enum_name = entry.get("enum") or pascal(setting_id)
            values = []
            for choice in entry.get("choices", []):
                values.append(choice["value"] if isinstance(choice, dict) else choice)
            if not values:
                fail(f"choice {setting_id} has no choices")
            for value in values:
                if not IDENT.match(value) or value in CXX_KEYWORDS:
                    fail(f"choice value '{value}' of {setting_id} is not a C++ identifier (use names like KilometersPerHour)")
            enums.append((enum_name, values))
            handles.append(f'    inline constexpr perlin::Choice<{enum_name}> {name}{{"{setting_id}"}};')
        elif kind in HANDLES:
            handles.append(f'    inline constexpr {HANDLES[kind]} {name}{{"{setting_id}"}};')
        else:
            fail(f"unknown setting type '{kind}' for {setting_id}")

    lines = [
        f"// Generated from {manifest_path.replace(chr(92), '/').split('/')[-1]} by perlin_settings.py. Do not edit.",
        "#pragma once",
        "#include <perlin/settings.hpp>",
        "",
        "namespace settings {",
    ]
    for enum_name, values in enums:
        lines.append(f"    enum class {enum_name} {{ " + ", ".join(values) + " };")
    if enums:
        lines.append("")
    lines += handles
    lines.append("}")
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
