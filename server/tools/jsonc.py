"""Load JSON with // comments and trailing commas, as Minecraft's data files use."""
import json
import re


def load(path):
    with open(path, encoding='utf8') as f:
        text = f.read()

    out = []
    in_string = False
    i = 0
    while i < len(text):
        c = text[i]
        if in_string:
            out.append(c)
            if c == '\\':
                out.append(text[i + 1])
                i += 1
            elif c == '"':
                in_string = False
        elif c == '"':
            in_string = True
            out.append(c)
        elif c == '/' and i + 1 < len(text) and text[i + 1] == '/':
            # leave i on the newline: the outer loop copies it through
            while i < len(text) and text[i] != '\n':
                i += 1
            continue
        else:
            out.append(c)
        i += 1

    s = ''.join(out)
    s = re.sub(r',(\s*[}\]])', r'\1', s)
    return json.loads(s)
