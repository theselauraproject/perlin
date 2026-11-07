from .model import BUILTINS, GENERICS, SCALARS, Alias, Enum, SchemaError, Struct, TypeRef

MAX_EVENT_ID = 128
HOSTS = ('client', 'server')


def fail(decl, msg):
    raise SchemaError(f'{decl.file}:{decl.line}: {decl.name}: {msg}')


def check_tags(api, decl, tags, kind):
    for t in tags:
        spec = api.tags.get(t.name)
        if not spec:
            fail(decl, f'unknown tag @{t.name}; declare it with a tag line first')
        if 'any' not in spec.targets and kind not in spec.targets:
            fail(decl, f'@{t.name} does not apply to {kind} (only {", ".join(spec.targets)})')
        required = [p for p in spec.params if not p.endswith('?') and not p.endswith('...')]
        variadic = any(p.endswith('...') for p in spec.params)
        if len(t.args) < len(required) or (not variadic and len(t.args) > len(spec.params)):
            fail(decl, f'@{t.name} takes ({", ".join(spec.params)}), got {len(t.args)} argument(s)')


def resolve(api, ref, decl, top=False):
    if ref.name in BUILTINS:
        if ref.args:
            fail(decl, f'{ref.name} takes no type arguments')
        if ref.name.startswith('rest_') and not top:
            fail(decl, f'{ref.name} may only be the last field or a whole response')
        return
    if ref.name in GENERICS:
        if len(ref.args) != GENERICS[ref.name]:
            fail(decl, f'{ref.name} takes {GENERICS[ref.name]} type argument(s)')
        if ref.name == 'array':
            if not isinstance(ref.args[1], int) or ref.args[1] <= 0:
                fail(decl, 'array<T, N> needs a positive length')
            resolve(api, ref.args[0], decl)
            return
        if ref.name == 'indexset':
            target = api.find(ref.args[0].name)
            if not isinstance(target, Enum):
                fail(decl, 'indexset<E, storage> needs an enum')
            if ref.args[1].name not in ('u32', 'u64'):
                fail(decl, 'indexset storage is u32 or u64')
            return
        if ref.name == 'flagset':
            target = api.find(ref.args[0].name)
            if not isinstance(target, Enum) or not target.is_flags:
                fail(decl, 'flagset<F> needs a flags declaration')
            return
        for a in ref.args:
            if not isinstance(a, TypeRef):
                fail(decl, f'{ref.name} takes types')
            resolve(api, a, decl)
        return
    target = api.find(ref.name)
    if target is None:
        fail(decl, f'unknown type {ref.name}')
    if ref.args:
        fail(decl, f'{ref.name} takes no type arguments')


def packed_size(api, ref):
    if ref.name in SCALARS:
        return SCALARS[ref.name]
    if ref.name == 'array':
        inner = packed_size(api, ref.args[0])
        return None if inner is None else inner * ref.args[1]
    target = api.find(ref.name)
    if isinstance(target, Enum):
        return SCALARS[target.base]
    if isinstance(target, Struct) and target.tag('layout'):
        return sum(packed_size(api, f.type) or 0 for f in target.fields)
    return None


def validate(api):
    seen = {}
    for d in api.enums + api.aliases + api.structs:
        if d.name in seen:
            fail(d, f'declared twice (first at {seen[d.name].file}:{seen[d.name].line})')
        seen[d.name] = d
    for c in api.consts:
        check_tags(api, c, c.tags, 'const')
    for e in api.enums:
        check_tags(api, e, e.tags, 'flags' if e.is_flags else 'enum')
        if e.base not in SCALARS or e.base in ('bool', 'f32', 'f64'):
            fail(e, 'enum base must be an integer type')
        names, values = set(), set()
        for m in e.members:
            check_tags(api, e, m.tags, 'member')
            if m.name in names:
                fail(e, f'enumerator {m.name} twice')
            if m.value in values:
                fail(e, f'value {m.value} twice')
            names.add(m.name)
            values.add(m.value)
            if e.is_flags and not 0 <= m.value < SCALARS[e.base] * 8:
                fail(e, f'flag bit {m.value} does not fit {e.base}')
    for a in api.aliases:
        check_tags(api, a, a.tags, 'alias')
        resolve(api, a.target, a)
    for s in api.structs:
        check_tags(api, s, s.tags, 'struct')
        names = set()
        for i, f in enumerate(s.fields):
            check_tags(api, s, f.tags, 'field')
            if f.name in names:
                fail(s, f'field {f.name} twice')
            names.add(f.name)
            resolve(api, f.type, s, top=i == len(s.fields) - 1)
            if f.default is not None and f.default.kind == 'ident':
                target = api.find(f.type.name)
                const = any(c.name == f.default.value for c in api.consts)
                if isinstance(target, Enum):
                    if not any(m.name == f.default.value for m in target.members):
                        fail(s, f'{f.name}: {f.default.value} is not a {target.name}')
                elif not const:
                    fail(s, f'{f.name}: unknown default {f.default.value}')
        if s.tag('layout'):
            for f in s.fields:
                if packed_size(api, f.type) is None:
                    fail(s, f'packed layout field {f.name} has no fixed size')
            want = s.tag('size')
            got = packed_size(api, TypeRef(s.name))
            if want and want.args[0] != got:
                fail(s, f'packed size is {got}, @size says {want.args[0]}')
    ids, names = {}, set()
    for op in api.ops:
        check_tags(api, op, op.tags, 'op')
        if op.name in names:
            fail(op, 'op declared twice')
        names.add(op.name)
        if op.id in ids:
            fail(op, f'id 0x{op.id:04X} already used by {ids[op.id].name}')
        ids[op.id] = op
        hosts = op.tag('hosts')
        if not hosts:
            fail(op, 'every op needs @hosts(...)')
        op.hosts = [h for h in hosts.args]
        for h in op.hosts:
            if h not in HOSTS:
                fail(op, f'unknown host {h}')
        perm = op.tag('permission')
        op.permissions = list(perm.args) if perm else []
        flags = next((e for e in api.enums if e.name == 'Permission'), None)
        for p in op.permissions:
            if not flags or not any(m.name == p for m in flags.members):
                fail(op, f'unknown permission {p}')
        for ref in (op.request, op.response):
            if ref:
                resolve(api, ref, op, top=True)
    ids, names = {}, set()
    for ev in api.events:
        check_tags(api, ev, ev.tags, 'event')
        if ev.name in names:
            fail(ev, 'event declared twice')
        names.add(ev.name)
        if ev.id in ids:
            fail(ev, f'id {ev.id} already used by {ids[ev.id].name}')
        if not 0 < ev.id < MAX_EVENT_ID:
            fail(ev, f'event ids are 1..{MAX_EVENT_ID - 1}: subscriptions are a {MAX_EVENT_ID}-bit mask')
        ids[ev.id] = ev
        hosts = ev.tag('hosts')
        if not hosts:
            fail(ev, 'every event needs @hosts(...)')
        ev.hosts = list(hosts.args)
        if ev.payload:
            resolve(api, ev.payload, ev)
            if not isinstance(api.find(ev.payload.name), Struct):
                fail(ev, 'an event payload is a struct')
