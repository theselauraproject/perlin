#!/usr/bin/env python3
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
UI = ROOT / "resources" / "ui"
INDENT = "    "
WIDTH = 110
VOID = {"mcimg", "mcitem", "img", "br", "input"}
PLACEHOLDER = re.compile(r"\{\{[^{}]*\}\}")


def mask(text):
    holders = []

    def keep(m):
        holders.append(m.group(0))
        return "\x00%d\x00" % (len(holders) - 1)

    return PLACEHOLDER.sub(keep, text), holders


def unmask(text, holders):
    return re.sub(r"\x00(\d+)\x00", lambda m: holders[int(m.group(1))], text)


def split_top(text, sep):
    parts, depth, quote, cur = [], 0, "", []
    for ch in text:
        if quote:
            cur.append(ch)
            if ch == quote:
                quote = ""
            continue
        if ch in "\"'":
            quote = ch
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append("".join(cur))
    return parts


def parse_css(text):
    items, i, n = [], 0, len(text)
    while i < n:
        while i < n and text[i].isspace():
            i += 1
        if i >= n:
            break
        if text.startswith("/*", i):
            end = text.find("*/", i)
            i = n if end < 0 else end + 2
            continue
        j, depth, quote = i, 0, ""
        while j < n:
            c = text[j]
            if quote:
                if c == quote:
                    quote = ""
            elif c in "\"'":
                quote = c
            elif c == "{":
                break
            elif c == ";" and depth == 0:
                break
            j += 1
        head = text[i:j].strip()
        if j >= n or text[j] == ";":
            if head:
                items.append(("decl", head, None))
            i = j + 1
            continue
        depth, k = 1, j + 1
        while k < n and depth:
            if text[k] == "{":
                depth += 1
            elif text[k] == "}":
                depth -= 1
            k += 1
        body = text[j + 1:k - 1]
        items.append(("rule", head, parse_css(body)))
        i = k
    return items


def declarations(body):
    out = []
    for part in split_top(body, ";"):
        part = part.strip()
        if not part:
            continue
        if ":" not in part:
            out.append(part)
            continue
        name, _, value = part.partition(":")
        value = re.sub(r"\s+", " ", value.strip())
        out.append("%s: %s;" % (name.strip(), value))
    return out


def emit_css(items, level):
    pad = INDENT * level
    blocks = []
    for kind, head, body in items:
        if kind == "decl":
            blocks.append([pad + d for d in declarations(head)])
            continue
        if head.startswith("@"):
            opener = pad + re.sub(r"\s+", " ", head) + " {"
        else:
            selectors = [re.sub(r"\s+", " ", s.strip()) for s in split_top(head, ",")]
            opener = (",\n").join(pad + s for s in selectors) + " {"
        if any(k == "rule" for k, _, _ in body):
            inner = emit_css(body, level + 1)
        else:
            inner = [pad + INDENT + d for k, h, _ in body for d in declarations(h)]
        blocks.append(opener.split("\n") + inner + [pad + "}"])
    lines = []
    for index, block in enumerate(blocks):
        if index:
            lines.append("")
        lines.extend(block)
    return lines


def format_css(text, level=0, masked=False):
    holders = []
    if not masked:
        text, holders = mask(text)
    out = "\n".join(emit_css(parse_css(text), level))
    return out if masked else unmask(out, holders)


TAG = re.compile(r"<(/?)([A-Za-z][\w:-]*)((?:\"[^\"]*\"|'[^']*'|[^'\">])*?)(/?)>|<!--.*?-->", re.S)
ATTR = re.compile(r"([\w:.-]+)(?:\s*=\s*(\"[^\"]*\"|'[^']*'|[^\s\"'>]+))?")


def format_open(name, attrs, selfclose, depth):
    pad = INDENT * depth
    pairs = ATTR.findall(attrs)
    rendered = []
    for key, value in pairs:
        if value == "":
            rendered.append(key)
        else:
            rendered.append("%s=%s" % (key, value))
    tail = " /" if selfclose else ""
    one = "<%s%s%s>" % (name, "".join(" " + r for r in rendered), tail)
    if len(pad + one) <= WIDTH or len(rendered) <= 1:
        return [pad + one]
    lines = [pad + "<" + name]
    for r in rendered:
        lines.append(pad + INDENT + r)
    lines[-1] += tail + ">" if not selfclose else " />"
    if not selfclose:
        pass
    return lines


def tokenize(text):
    tokens, pos = [], 0
    for m in TAG.finditer(text):
        if m.start() > pos:
            tokens.append(("text", text[pos:m.start()]))
        if m.group(0).startswith("<!--"):
            pass
        else:
            tokens.append(("close" if m.group(1) else "open", m.group(2), m.group(3), m.group(4)))
        pos = m.end()
    if pos < len(text):
        tokens.append(("text", text[pos:]))
    return tokens


class Node:
    def __init__(self, name=None, attrs="", selfclose=False):
        self.name, self.attrs, self.selfclose = name, attrs, selfclose
        self.children, self.closed = [], False


def build(tokens):
    root = Node()
    stack = [root]
    for tok in tokens:
        top = stack[-1]
        if tok[0] == "text":
            top.children.append(tok[1])
        elif tok[0] == "open":
            _, name, attrs, slash = tok
            node = Node(name, attrs, bool(slash) or name in VOID and False)
            node.selfclose = bool(slash)
            top.children.append(node)
            if not slash:
                stack.append(node)
        else:
            name = tok[1]
            for depth in range(len(stack) - 1, 0, -1):
                if stack[depth].name == name:
                    stack[depth].closed = True
                    del stack[depth:]
                    break
    return root


def is_blank(c):
    return isinstance(c, str) and not c.strip()


def text_only(node):
    return all(isinstance(c, str) for c in node.children)


def render(node, depth, out):
    pad = INDENT * depth
    if node.name == "style":
        css = "".join(c for c in node.children if isinstance(c, str))
        out.append(pad + "<style>")
        body = format_css(css, depth + 1, masked=True)
        out.append(body)
        if node.closed:
            out.append(pad + "</style>")
        return
    open_lines = format_open(node.name, node.attrs, node.selfclose, depth)
    if node.selfclose:
        out.extend(open_lines)
        return
    kids = [c for c in node.children if not is_blank(c)]
    if node.closed and not kids:
        if len(open_lines) == 1:
            out.append(open_lines[0] + "</%s>" % node.name)
        else:
            out.extend(open_lines)
            out[-1] += "</%s>" % node.name
        return
    if node.closed and all(isinstance(c, str) for c in kids):
        text = "".join(kids)
        text = text.strip("\n") if "\n" in text and node.name in ("title",) else text
        if len(open_lines) == 1 and "\n" not in text:
            out.append(open_lines[0] + text.strip() + "</%s>" % node.name)
            return
    mixed = any(isinstance(c, str) for c in kids) and any(isinstance(c, Node) for c in kids)
    out.extend(open_lines)
    if mixed:
        raw = "".join(render_raw(c) for c in node.children)
        out.append(raw + ("</%s>" % node.name if node.closed else ""))
        return
    for c in kids:
        if isinstance(c, str):
            out.append(INDENT * (depth + 1) + c.strip())
        else:
            render(c, depth + 1, out)
    if node.closed:
        out.append(pad + "</%s>" % node.name)


def render_raw(c):
    if isinstance(c, str):
        return c
    attrs = c.attrs
    inner = "".join(render_raw(k) for k in c.children)
    if c.selfclose:
        return "<%s%s/>" % (c.name, attrs)
    return "<%s%s>%s%s" % (c.name, attrs, inner, "</%s>" % c.name if c.closed else "")


def format_rml(text):
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    masked, holders = mask(text)
    root = build(tokenize(masked))
    out = []
    for c in root.children:
        if isinstance(c, Node):
            render(c, 0, out)
        elif c.strip():
            out.append(c.strip())
    result = "\n".join(out)
    result = re.sub(r"\n{3,}", "\n\n", result)
    return unmask(result, holders).rstrip("\n") + "\n"


def format_file(path):
    text = path.read_text(encoding="utf-8")
    if path.suffix == ".rml":
        return format_rml(text)
    return format_css(text).rstrip("\n") + "\n"


def main():
    check = "--check" in sys.argv
    files = sorted(list(UI.rglob("*.rml")) + list(UI.rglob("*.rcss")) + list(UI.rglob("*.css")))
    bad = []
    for path in files:
        new = format_file(path)
        if new != path.read_text(encoding="utf-8"):
            bad.append(path.relative_to(ROOT))
            if not check:
                path.write_text(new, encoding="utf-8")
    if check and bad:
        for b in bad:
            print(f"{b}: not formatted; run tools/format_rml.py")
        return 1
    print(f"formatted {len(files)} files, {len(bad)} changed" if not check else f"rml format ok: {len(files)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
