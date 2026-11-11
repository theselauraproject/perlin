from ..common import snake


def link(api, r):
    if r is None:
        return '-'
    if isinstance(r, int):
        return str(r)
    if r.args:
        return f'{r.name}&lt;' + ', '.join(link(api, a) for a in r.args) + '&gt;'
    if api.find(r.name) is not None:
        return f'[{r.name}](#{r.name.lower()})'
    return f'`{r.name}`'


def tagline(d, skip=('doc', 'hosts', 'permission')):
    extra = [t for t in d.tags if t.name not in skip]
    return ' '.join('`@' + t.name + ('(' + ', '.join(str(a) for a in t.args) + ')' if t.args else '') + '`' for t in extra)


def generate(api):
    out = [f'# Perlin mod API reference (schema v{api.version})', '',
           'Generated from `mods/api/*.papi` by `mods/codegen/generate.py`. Edit the schema, not this file.', '',
           '## Tags', '', '| Tag | Parameters | Applies to | Meaning |', '|---|---|---|---|']
    for t in api.tags.values():
        out.append(f'| `@{t.name}` | {", ".join(t.params) or "-"} | {", ".join(t.targets)} | {t.doc()} |')
    out += ['', '## Ops', '', '| Id | Op | C | Request | Response | Hosts | Permission | Notes |', '|---|---|---|---|---|---|---|---|']
    for o in sorted(api.ops, key=lambda o: o.id):
        note = (o.doc() + ' ' + tagline(o)).strip()
        out.append(f'| 0x{o.id:04X} | {o.name} | `perlin_{snake(o.name)}` | {link(api, o.request)} | {link(api, o.response)} | '
                   f'{", ".join(o.hosts)} | {", ".join(o.permissions) or "-"} | {note} |')
    out += ['', '## Events', '', '| Id | Event | Payload | Hosts | Notes |', '|---|---|---|---|---|']
    for e in sorted(api.events, key=lambda e: e.id):
        note = (e.doc() + ' ' + tagline(e)).strip()
        out.append(f'| {e.id} | {e.name} | {link(api, e.payload)} | {", ".join(e.hosts)} | {note} |')
    out += ['', '## Enums', '']
    for e in api.enums:
        out += [f'### {e.name}', '', (e.doc() + f' ({"flags" if e.is_flags else "enum"} over `{e.base}`)').strip(), '']
        out.append(', '.join(f'`{m.name}`={("1<<" if e.is_flags else "")}{m.value}' for m in e.members))
        out.append('')
    out += ['## Aliases', '']
    for a in api.aliases:
        out.append(f'- **{a.name}** = {link(api, a.target)} {a.doc()}')
    out += ['', '## Structs', '']
    for s in api.structs:
        out += [f'### {s.name}', '']
        if s.doc():
            out += [s.doc(), '']
        if s.fields:
            out += ['| Field | Type | Default | Notes |', '|---|---|---|---|']
            for f in s.fields:
                note = ' '.join(['`@' + t.name + ('(' + ', '.join(str(a) for a in t.args) + ')' if t.args else '') + '`' for t in f.tags if t.name != 'doc'] +
                                [next((t.args[0] for t in f.tags if t.name == 'doc' and t.args), '')]).strip()
                out.append(f'| {f.name} | {link(api, f.type)} | {f.default.text if f.default else ""} | {note} |')
        out.append('')
    return {'docs/API.md': '\n'.join(out) + '\n'}
