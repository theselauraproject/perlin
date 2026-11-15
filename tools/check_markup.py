#!/usr/bin/env python3
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = [ROOT / "src"]
RAW = re.compile(r'R"(\w*)\((.*?)\)\1"', re.S)
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
TAG = re.compile(r"<\s*/?\s*[A-Za-z][\w-]*(?:\s[^<>]*)?/?\s*>")
OPEN_TAG = re.compile(r"<\s*/?\s*(?:rml|html|head|body|style|div|span|img|p|a|br|input|button|select|option|label|table|tr|td|ul|li|form|textarea|handle|mcimg|mcitem|mclayer|mcbtn|link|title|script)\b", re.I)
STYLE = re.compile(r"\b(?:font-family|background-color|text-align|z-index|line-height)\s*:|style=\\?\"")
BANNED_IDENTIFIERS = ("EscapeRml", "FormatCodesToRml", "TextFieldRml", "FormatCodesToRmlLines", "SpriteRml", "TooltipRml", "WithJavaWidgets")
ALLOWED_FILES = {
    "src/client/ui/UiTemplate.cpp",
}
ALLOWED_LITERALS = {"</style>", "{{scene:", "{{include:", "<!--perlin-ui-src:"}


def scan(path, text, failures):
    rel = path.relative_to(ROOT).as_posix()
    for ident in BANNED_IDENTIFIERS:
        for m in re.finditer(r"\b" + ident + r"\b", text):
            failures.append(f"{rel}:{text.count(chr(10), 0, m.start()) + 1}: markup-generation helper '{ident}' must not exist")
    if rel in ALLOWED_FILES:
        return
    bodies = [(m.start(), m.group(1)) for m in LITERAL.finditer(text)] + [(m.start(), m.group(2)) for m in RAW.finditer(text)]
    for start, body in bodies:
        if any(a in body for a in ALLOWED_LITERALS):
            continue
        if OPEN_TAG.search(body) or STYLE.search(body):
            failures.append(f"{rel}:{text.count(chr(10), 0, start) + 1}: string literal holds markup or inline style; structure belongs in resources/ui, behavior in C++ data models")


def main():
    failures = []
    for root in SOURCES:
        for path in sorted(root.rglob("*")):
            if path.suffix in (".cpp", ".hpp", ".h"):
                scan(path, path.read_text(encoding="utf-8", errors="replace"), failures)
    for f in failures:
        print(f)
    if failures:
        print(f"{len(failures)} embedded markup site(s)")
        return 1
    print("markup ok: no markup, inline style or markup-generation helpers in C++")
    return 0


if __name__ == "__main__":
    sys.exit(main())
