import json

from ..model import TypeRef


def ref(r):
    if r is None:
        return None
    if isinstance(r, int):
        return r
    if not r.args:
        return r.name
    return {'generic': r.name, 'args': [ref(a) for a in r.args]}


def lit(v):
    if v is None:
        return None
    return {'kind': v.kind, 'value': v.value if v.kind != 'ident' else v.value, 'text': v.text}


def tags(ts):
    return [{'name': t.name, 'args': list(t.args)} for t in ts]


def generate(api):
    doc = {
        'api': api.name,
        'version': api.version,
        'tags': [{'name': t.name, 'params': t.params, 'targets': t.targets, 'doc': t.doc()} for t in api.tags.values()],
        'consts': [{'name': c.name, 'type': ref(c.type), 'value': lit(c.value), 'tags': tags(c.tags)} for c in api.consts],
        'enums': [{'name': e.name, 'base': e.base, 'flags': e.is_flags, 'tags': tags(e.tags),
                   'members': [{'name': m.name, 'value': m.value, 'tags': tags(m.tags)} for m in e.members]} for e in api.enums],
        'aliases': [{'name': a.name, 'target': ref(a.target), 'tags': tags(a.tags)} for a in api.aliases],
        'structs': [{'name': s.name, 'synthetic': s.synthetic, 'tags': tags(s.tags),
                     'fields': [{'name': f.name, 'type': ref(f.type), 'default': lit(f.default), 'tags': tags(f.tags)}
                                for f in s.fields]} for s in api.structs],
        'ops': [{'name': o.name, 'id': o.id, 'request': ref(o.request), 'response': ref(o.response), 'hosts': o.hosts,
                 'permissions': o.permissions, 'tags': tags(o.tags)} for o in sorted(api.ops, key=lambda o: o.id)],
        'events': [{'name': e.name, 'id': e.id, 'payload': ref(e.payload), 'hosts': e.hosts, 'tags': tags(e.tags)}
                   for e in sorted(api.events, key=lambda e: e.id)],
    }
    return {'api/perlin_api.json': json.dumps(doc, indent=1, default=str) + '\n'}
