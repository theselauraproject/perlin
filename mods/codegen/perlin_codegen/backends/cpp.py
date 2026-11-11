from ..common import const_name, hosts_word, snake, sorted_structs
from ..model import SCALARS, Alias, Enum, Struct

CPP_SCALARS = {
    'bool': 'bool', 'i8': 'int8_t', 'u8': 'uint8_t', 'i16': 'int16_t', 'u16': 'uint16_t', 'i32': 'int32_t',
    'u32': 'uint32_t', 'i64': 'int64_t', 'u64': 'uint64_t', 'f32': 'float', 'f64': 'double',
}


def cpp_type(api, ref, q=''):
    n = ref.name
    if n in CPP_SCALARS:
        return CPP_SCALARS[n]
    if n == 'string':
        return 'std::string'
    if n == 'bytes':
        return 'std::vector<uint8_t>'
    if n == 'rest_text':
        return q + 'RestText'
    if n == 'rest_bytes':
        return q + 'RestBytes'
    if n == 'list':
        return f'std::vector<{cpp_type(api, ref.args[0], q)}>'
    if n == 'optional':
        return f'std::optional<{cpp_type(api, ref.args[0], q)}>'
    if n == 'pair':
        return f'std::pair<{cpp_type(api, ref.args[0], q)}, {cpp_type(api, ref.args[1], q)}>'
    if n == 'array':
        return f'std::array<{cpp_type(api, ref.args[0], q)}, {ref.args[1]}>'
    if n == 'flagset':
        return f'::perlin::meta::Flags<{q}{ref.args[0].name}>' if q else f'meta::Flags<{ref.args[0].name}>'
    if n == 'indexset':
        inner = f'{q}{ref.args[0].name}, {CPP_SCALARS[ref.args[1].name]}'
        return f'::perlin::meta::IndexSet<{inner}>' if q else f'meta::IndexSet<{inner}>'
    return q + n


def literal(api, ref, lit):
    target = api.find(ref.name)
    if lit.kind == 'bool':
        return 'true' if lit.value else 'false'
    if lit.kind == 'string':
        return '"' + lit.text[1:-1] + '"'
    if lit.kind == 'ident':
        if isinstance(target, Enum):
            return f'{target.name}::{lit.value}'
        return const_name(lit.value)
    if ref.name == 'f32':
        text = lit.text if lit.kind == 'float' else lit.text + '.0'
        return text + 'f' if '.' in text else text + '.0f'
    if ref.name == 'f64':
        return lit.text
    if ref.name.startswith('u') and lit.kind == 'int' and lit.value > 0x7FFFFFFF:
        return lit.text + ('ull' if ref.name == 'u64' else 'u')
    return lit.text


def field_line(api, f):
    t = cpp_type(api, f.type)
    if f.default is not None:
        return f'        {t} {f.name} = {literal(api, f.type, f.default)};'
    plain = f.type.name in CPP_SCALARS or isinstance(api.find(f.type.name), Enum) or f.type.name == 'array'
    return f'        {t} {f.name}{{}};' if plain else f'        {t} {f.name};'


def enum_block(e):
    base = CPP_SCALARS[e.base]
    lines = [f'    enum class {e.name} : {base} {{']
    if e.is_flags and not any(m.name == 'None' for m in e.members):
        lines.append('        None = 0,')
    for m in e.members:
        value = f'{base}{{1}} << {m.value}' if e.is_flags else str(m.value)
        lines.append(f'        {m.name} = {value},')
    lines.append('    };')
    return lines


def types_header(api):
    out = ['#pragma once', '#include <array>', '#include <cstdint>', '#include <optional>', '#include <string>',
           '#include <utility>', '#include <vector>', '', '#include "perlin/meta.hpp"', '',
           'namespace perlin::abi {', f'    inline constexpr uint32_t kSchemaVersion = {api.version};']
    for c in api.consts:
        out.append(f'    inline constexpr {CPP_SCALARS[c.type.name]} {const_name(c.name)} = {literal(api, c.type, c.value)};')
    out.append('')
    for e in api.enums:
        out += enum_block(e)
        out.append('')
    out.append('}')
    out.append('')
    for e in api.enums:
        if e.is_flags:
            out.append(f'template <> struct perlin::meta::enable_flags<perlin::abi::{e.name}> : std::true_type {{}};')
        r = e.tag('range')
        if r:
            out.append(f'template <> struct perlin::meta::enum_range<perlin::abi::{e.name}> {{')
            out.append(f'    static constexpr int min = {r.args[0]};')
            out.append(f'    static constexpr int max = {r.args[1]};')
            out.append('};')
    out += ['', 'namespace perlin::abi {',
            '    template <class E>', '        requires std::is_enum_v<E>',
            '    constexpr bool abi_enum_tag(E) {', '        return true;', '    }', '}', '',
            'namespace perlin::meta {', '    template <class E>', '        requires std::is_enum_v<E>',
            '    struct enable_format<E> : std::bool_constant < requires(E e) {', '        abi_enum_tag(e);',
            '    } > {};', '}', '', 'namespace perlin::abi {',
            '    struct RestText {', '        std::string value;', '        static constexpr bool wire_rest_tag = true;',
            '        operator const std::string&() const { return value; }', '    };',
            '    struct RestBytes {', '        std::vector<uint8_t> value;',
            '        static constexpr bool wire_rest_tag = true;', '    };', '']
    for a in api.aliases:
        out.append(f'    using {a.name} = {cpp_type(api, a.target)};')
    out.append('')
    for s in sorted_structs(api):
        out.append(f'    struct {s.name} {{')
        for f in s.fields:
            out.append(field_line(api, f))
        cpp = s.tag('cpp')
        if cpp:
            out.append('        ' + cpp.args[0])
        out.append('    };')
        size = s.tag('size')
        if size:
            out.append(f'    static_assert(sizeof({s.name}) == {size.args[0]});')
        out.append('')
    out.append('}')
    out.append('')
    return '\n'.join(out)


def op_type(api, ref):
    if ref is None:
        return 'Nothing'
    return cpp_type(api, ref)


def perm_expr(op):
    return ' | '.join(op.permissions) if op.permissions else 'None'


def table_header(api):
    out = ['#ifndef PERLIN_ABI_TABLE_H', '#define PERLIN_ABI_TABLE_H', '',
           f'#define PERLIN_ABI_VERSION {api.version}', '', '#define PERLIN_ABI_OPS(X) \\']
    ops = sorted(api.ops, key=lambda o: o.id)
    for i, op in enumerate(ops):
        tail = ' \\' if i < len(ops) - 1 else ''
        out.append(f'    X({op.name}, 0x{op.id:04X}, {op_type(api, op.request)}, {op_type(api, op.response)}, '
                   f'{perm_expr(op)}, {hosts_word(op.hosts)}){tail}')
    out += ['', '#define PERLIN_ABI_EVENTS(X) \\']
    events = sorted(api.events, key=lambda e: e.id)
    for i, ev in enumerate(events):
        tail = ' \\' if i < len(events) - 1 else ''
        payload = ev.payload.name if ev.payload else 'EventNothing'
        wiring = 'Internal' if ev.has('internal') else 'Method'
        out.append(f'    X({ev.name}, {ev.id}, {payload}, {hosts_word(ev.hosts)}, {wiring}){tail}')
    out += ['', '#endif', '']
    return '\n'.join(out)


def spec_header(api):
    ops = sorted(api.ops, key=lambda o: o.id)
    events = sorted(api.events, key=lambda e: e.id)
    out = ['#pragma once', '#include <array>', '#include <cstdint>', '#include <string_view>', '',
           '#include "perlin/gen/abi_types.hpp"', '', 'namespace perlin::abi {',
           '    enum class Op : uint32_t {']
    out += [f'        {op.name} = 0x{op.id:04X},' for op in ops]
    out += ['    };', '', '    enum class Event : uint32_t {']
    out += [f'        {ev.name} = {ev.id},' for ev in events]
    out += ['    };', '',
            '    enum class Hosts : uint8_t { Client = 1, Server = 2, Both = 3 };',
            '    struct Raw {};', '    using Nothing = EventNothing;', '',
            f'    inline constexpr uint32_t kEventCount = {max(e.id for e in events) + 1};',
            '    inline constexpr uint32_t kSubscriptionWords = (kEventCount + 31) / 32;', '',
            '    template <Op O> struct OpSpec;']
    for op in ops:
        perm = ' | '.join(f'static_cast<uint32_t>(Permission::{p})' for p in op.permissions) or '0u'
        since = op.tag('since').args[0] if op.tag('since') else 1
        cat = op.tag('category').args[0] if op.tag('category') else 'misc'
        out += [f'    template <> struct OpSpec<Op::{op.name}> {{',
                f'        using Request = {op_type(api, op.request)};',
                f'        using Response = {op_type(api, op.response)};',
                f'        static constexpr uint32_t kPermission = {perm};',
                f'        static constexpr Hosts kHosts = Hosts::{hosts_word(op.hosts)};',
                f'        static constexpr uint32_t kSince = {since};',
                f'        static constexpr bool kHot = {"true" if op.has("hot") else "false"};',
                f'        static constexpr std::string_view kName = "{op.name}";',
                f'        static constexpr std::string_view kCategory = "{cat}";',
                '    };']
    out += ['', '    template <Event E> struct EventSpec;']
    for ev in events:
        payload = ev.payload.name if ev.payload else 'EventNothing'
        since = ev.tag('since').args[0] if ev.tag('since') else 1
        out += [f'    template <> struct EventSpec<Event::{ev.name}> {{',
                f'        using Payload = {payload};',
                f'        static constexpr Hosts kHosts = Hosts::{hosts_word(ev.hosts)};',
                f'        static constexpr bool kMethod = {"false" if ev.has("internal") else "true"};',
                f'        static constexpr bool kCancellable = {"true" if ev.has("cancellable") else "false"};',
                f'        static constexpr bool kHot = {"true" if ev.has("hot") else "false"};',
                f'        static constexpr uint32_t kSince = {since};',
                f'        static constexpr std::string_view kName = "{ev.name}";',
                '    };']
    out += ['', '    template <Event E> using payload_t = typename EventSpec<E>::Payload;', '',
            '    constexpr uint32_t PermissionOf(Op op) {', '        switch (op) {']
    for op in ops:
        out.append(f'        case Op::{op.name}: return OpSpec<Op::{op.name}>::kPermission;')
    out += ['        }', '        return 0;', '    }', '', '    constexpr Hosts HostsOf(Op op) {', '        switch (op) {']
    for op in ops:
        out.append(f'        case Op::{op.name}: return Hosts::{hosts_word(op.hosts)};')
    out += ['        }', '        return Hosts::Both;', '    }', '', '    constexpr Hosts HostsOf(Event event) {',
            '        switch (event) {']
    for ev in events:
        out.append(f'        case Event::{ev.name}: return Hosts::{hosts_word(ev.hosts)};')
    out += ['        }', '        return Hosts::Both;', '    }', '', '    constexpr std::string_view NameOf(Op op) {',
            '        switch (op) {']
    for op in ops:
        out.append(f'        case Op::{op.name}: return "{op.name}";')
    out += ['        }', '        return "?";', '    }', '', '    constexpr std::string_view NameOf(Event event) {',
            '        switch (event) {']
    for ev in events:
        out.append(f'        case Event::{ev.name}: return "{ev.name}";')
    out += ['        }', '        return "?";', '    }', '', '    constexpr std::string_view SnakeName(Event event) {',
            '        switch (event) {']
    for ev in events:
        out.append(f'        case Event::{ev.name}: return "{snake(ev.name)}";')
    out += ['        }', '        return {};', '    }', '', '    constexpr std::string_view SnakeName(Op op) {',
            '        switch (op) {']
    for op in ops:
        out.append(f'        case Op::{op.name}: return "{snake(op.name)}";')
    out += ['        }', '        return {};', '    }', '', '    constexpr bool Cancellable(Event event) {',
            '        switch (event) {']
    for ev in events:
        out.append(f'        case Event::{ev.name}: return {"true" if ev.has("cancellable") else "false"};')
    out += ['        }', '        return false;', '    }', '',
            f'    inline constexpr std::array<Op, {len(ops)}> kAllOps = {{']
    out += [f'        Op::{op.name},' for op in ops]
    out += ['    };', f'    inline constexpr std::array<Event, {len(events)}> kAllEvents = {{']
    out += [f'        Event::{ev.name},' for ev in events]
    out += ['    };', '}', '']
    return '\n'.join(out)


def calls_header(api):
    from ..common import snake
    q = '::perlin::abi::'
    out = ['#pragma once', '#include "perlin/rpc.hpp"', '', 'namespace perlin::api {']
    for op in sorted(api.ops, key=lambda o: o.id):
        fn = snake(op.name)
        req = op.request
        res = 'Nothing' if op.response is None else cpp_type(api, op.response, q)
        ret = '::perlin::Result<void>' if res == 'Nothing' else f'::perlin::Result<{res}>'
        call = f'::perlin::call<{q}Op::{op.name}>'
        req_struct = api.struct(req.name) if req else None
        if req_struct is not None and req_struct.synthetic:
            params = ', '.join(f'{cpp_type(api, f.type, q)} {f.name}' for f in req_struct.fields)
            args = ', '.join(f'std::move({f.name})' if movable(api, f) else f.name for f in req_struct.fields)
            out += [f'    inline {ret} {fn}({params}) {{',
                    f'        return {call}({q}{req.name}{{{args}}});', '    }']
        elif req is not None:
            out += [f'    inline {ret} {fn}(const {cpp_type(api, req, q)}& request) {{',
                    f'        return {call}(request);', '    }']
        else:
            out += [f'    inline {ret} {fn}() {{', f'        return {call}();', '    }']
    out += ['}', '']
    return '\n'.join(out)


def movable(api, f):
    return not (f.type.name in CPP_SCALARS or isinstance(api.find(f.type.name), Enum))


def event_tags_header(api):
    out = ['#pragma once', '', 'namespace perlin::event {']
    for e in sorted(api.events, key=lambda e: e.id):
        out.append(f'    using {e.name} = EventTag<abi::Event::{e.name}>;')
    out += ['}', '']
    return '\n'.join(out)


def plugin_wire_header(api):
    out = ['#pragma once', '', 'template <class T> void wire_events(T& self, Context& ctx) {']
    for e in sorted(api.events, key=lambda e: e.id):
        if e.has('internal'):
            continue
        n = e.name
        out += [
            f'    if constexpr (requires(T& t, const typename event::{n}::Payload& p) {{ t.on{n}(p); }})',
            f'        ctx.on<event::{n}>([&self](const typename event::{n}::Payload& p) {{ return self.on{n}(p); }});',
            f'    else if constexpr (requires(T& t) {{ t.on{n}(); }})',
            f'        ctx.on<event::{n}>([&self]() {{ return self.on{n}(); }});',
        ]
    out += ['}', '']
    return '\n'.join(out)


def service_header(api, host):
    cls = host.capitalize() + 'Service'
    ops = [o for o in sorted(api.ops, key=lambda o: o.id) if host in o.hosts]
    out = ['#pragma once', '#include "perlin/host.hpp"', '', 'namespace perlin::abi::host {',
           f'    template <class Ctx> class {cls} {{', '    public:', f'        virtual ~{cls}() = default;', '']
    for op in ops:
        res = op_type(api, op.response)
        ret = 'Result<void>' if res == 'Nothing' else f'Result<{res}>'
        args = 'Ctx&' if op.request is None else f'Ctx&, const {op_type(api, op.request)}&'
        out.append(f'        virtual {ret} Op{op.name}({args}) {{ return std::unexpected(Status::Unavailable); }}')
    out += ['', '        void Bind(OpTable<Ctx>& table) {']
    for op in ops:
        if op.request is None:
            out.append(f'            table.template Handle<Op::{op.name}>([this](Ctx& c) {{ return Op{op.name}(c); }});')
        else:
            req = op_type(api, op.request)
            out.append(f'            table.template Handle<Op::{op.name}>([this](Ctx& c, const {req}& r) {{ return Op{op.name}(c, r); }});')
    out += ['        }', '    };', '}', '']
    return '\n'.join(out)


def generate(api):
    return {
        'sdk/perlin/gen/client_service.hpp': service_header(api, 'client'),
        'sdk/perlin/gen/server_service.hpp': service_header(api, 'server'),
        'sdk/perlin/gen/event_tags.hpp': event_tags_header(api),
        'sdk/perlin/gen/plugin_wire.hpp': plugin_wire_header(api),
        'sdk/perlin/gen/abi_types.hpp': types_header(api),
        'sdk/perlin/gen/abi_spec.hpp': spec_header(api),
        'sdk/perlin/gen/calls.hpp': calls_header(api),
        'sdk/perlin/abi_table.h': table_header(api),
    }
