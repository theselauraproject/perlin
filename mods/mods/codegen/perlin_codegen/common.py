import re

from .model import Alias, Enum, Struct, TypeRef


def snake(name):
    s = re.sub(r'(?<=[a-z0-9])([A-Z])', r'_\1', name)
    s = re.sub(r'(?<=[A-Z])([A-Z][a-z])', r'_\1', s)
    return s.lower()


def upper(name):
    return snake(name).upper()


def const_name(name):
    return 'k' + name


def deps(api, ref, out):
    if not isinstance(ref, TypeRef):
        return
    target = api.find(ref.name)
    if target is not None:
        out.add(target.name)
    for a in ref.args:
        deps(api, a, out)


def sorted_structs(api):
    done, order = set(), []
    by_name = {s.name: s for s in api.structs}
    aliases = {a.name: a for a in api.aliases}

    def visit(s, stack):
        if s.name in done:
            return
        if s.name in stack:
            raise ValueError('struct cycle through ' + s.name)
        stack.add(s.name)
        need = set()
        for f in s.fields:
            deps(api, f.type, need)
        for n in sorted(need):
            if n in aliases:
                inner = set()
                deps(api, aliases[n].target, inner)
                for m in inner:
                    if m in by_name:
                        visit(by_name[m], stack)
            if n in by_name:
                visit(by_name[n], stack)
        stack.discard(s.name)
        done.add(s.name)
        order.append(s)

    for s in api.structs:
        visit(s, set())
    return order


def hosts_word(hosts):
    h = set(hosts)
    if h == {'client', 'server'}:
        return 'Both'
    return 'Client' if 'client' in h else 'Server'


def member_value(api, enum_name, member):
    e = api.find(enum_name)
    for m in e.members:
        if m.name == member:
            return m.value
    return 0


def wire_size_note(api, ref):
    return str(ref)


def is_enum(api, ref):
    return isinstance(api.find(ref.name), Enum)


def resolve_alias(api, ref):
    target = api.find(ref.name)
    while isinstance(target, Alias):
        ref = target.target
        target = api.find(ref.name)
    return ref
