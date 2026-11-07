from ..common import snake, sorted_structs, upper
from ..model import Alias, Enum, Struct, TypeRef

C_SCALARS = {
    'bool': 'uint8_t', 'i8': 'int8_t', 'u8': 'uint8_t', 'i16': 'int16_t', 'u16': 'uint16_t', 'i32': 'int32_t',
    'u32': 'uint32_t', 'i64': 'int64_t', 'u64': 'uint64_t', 'f32': 'float', 'f64': 'double',
}
CODEC = {
    'bool': 'u8', 'i8': 'i8', 'u8': 'u8', 'i16': 'i16', 'u16': 'u16', 'i32': 'i32', 'u32': 'u32', 'i64': 'i64',
    'u64': 'u64', 'f32': 'f32', 'f64': 'f64',
}


class CGen:
    def __init__(self, api):
        self.api = api
        self.containers = {}
        self.container_order = []

    def unalias(self, ref):
        target = self.api.find(ref.name)
        while isinstance(target, Alias):
            ref = target.target
            target = self.api.find(ref.name)
        return ref

    def scalar_of(self, ref):
        ref = self.unalias(ref)
        if ref.name in C_SCALARS:
            return ref.name
        target = self.api.find(ref.name)
        if isinstance(target, Enum):
            return target.base
        if ref.name == 'flagset':
            return self.api.find(ref.args[0].name).base
        if ref.name == 'indexset':
            return ref.args[1].name
        return None

    def mangle(self, ref):
        ref = self.unalias(ref)
        s = self.scalar_of(ref)
        if s and ref.name in C_SCALARS:
            return s.capitalize()
        if s:
            return ref.name
        if ref.name == 'string':
            return 'Str'
        if ref.name == 'bytes':
            return 'Bytes'
        if ref.name in ('list', 'optional', 'pair'):
            return ref.name.capitalize() + '_' + '_'.join(self.mangle(a) for a in ref.args)
        if ref.name == 'array':
            return f'Array{ref.args[1]}_' + self.mangle(ref.args[0])
        return ref.name

    def ctype(self, ref):
        ref = self.unalias(ref)
        s = self.scalar_of(ref)
        if s:
            target = self.api.find(ref.name)
            return 'Perlin' + target.name if isinstance(target, Enum) else C_SCALARS[s]
        if ref.name in ('string', 'rest_text'):
            return 'perlin_str'
        if ref.name in ('bytes', 'rest_bytes'):
            return 'perlin_bytes'
        if ref.name in ('list', 'optional', 'pair'):
            name = 'Perlin' + self.mangle(ref)
            if name not in self.containers:
                for a in ref.args:
                    self.ctype(a)
                self.containers[name] = ref
                self.container_order.append(name)
            return name
        return 'Perlin' + ref.name

    def decl(self, ref, name):
        ref = self.unalias(ref)
        if ref.name == 'array':
            return f'{self.ctype(ref.args[0])} {name}[{ref.args[1]}]'
        return f'{self.ctype(ref)} {name}'

    def write_expr(self, ref, value):
        ref = self.unalias(ref)
        s = self.scalar_of(ref)
        if s:
            return [f'perlin_w_{CODEC[s]}(w, ({C_SCALARS[s]})({value}));']
        if ref.name == 'string':
            return [f'perlin_w_str(w, {value});']
        if ref.name == 'bytes':
            return [f'perlin_w_bytes(w, {value});']
        if ref.name == 'rest_text':
            return [f'perlin_w_raw(w, {value}.data, {value}.len);']
        if ref.name == 'rest_bytes':
            return [f'perlin_w_raw(w, {value}.data, {value}.len);']
        if ref.name == 'array':
            return [f'for (uint32_t i_ = 0; i_ < {ref.args[1]}; i_++) {{'] + \
                ['    ' + l for l in self.write_expr(ref.args[0], f'{value}[i_]')] + ['}']
        if ref.name in ('list', 'optional', 'pair'):
            return [f'perlin_write_{self.mangle(ref)}(w, &{value});']
        return [f'perlin_write_{ref.name}(w, &{value});']

    def read_expr(self, ref, target):
        ref = self.unalias(ref)
        s = self.scalar_of(ref)
        if s:
            return [f'{target} = ({self.ctype(ref)})perlin_r_{CODEC[s]}(r);']
        if ref.name == 'string':
            return [f'{target} = perlin_r_str(r);']
        if ref.name == 'bytes':
            return [f'{target} = perlin_r_bytes(r);']
        if ref.name == 'rest_text':
            return [f'{target} = perlin_r_rest_str(r);']
        if ref.name == 'rest_bytes':
            return [f'{target} = perlin_r_rest(r);']
        if ref.name == 'array':
            return [f'for (uint32_t i_ = 0; i_ < {ref.args[1]}; i_++) {{'] + \
                ['    ' + l for l in self.read_expr(ref.args[0], f'{target}[i_]')] + ['}']
        if ref.name in ('list', 'optional', 'pair'):
            return [f'perlin_read_{self.mangle(ref)}(r, &{target}, a);']
        return [f'perlin_read_{ref.name}(r, &{target}, a);']

    def container_codec(self, name):
        ref = self.containers[name]
        m = self.mangle(ref)
        out = []
        if ref.name == 'list':
            el = ref.args[0]
            out += [f'static inline void perlin_write_{m}(perlin_writer* w, const {name}* v) {{',
                    '    perlin_w_u32(w, v->len);', '    for (uint32_t k_ = 0; k_ < v->len; k_++) {']
            out += ['        ' + l for l in self.write_expr(el, 'v->data[k_]')]
            out += ['    }', '}',
                    f'static inline void perlin_read_{m}(perlin_reader* r, {name}* v, perlin_arena* a) {{',
                    '    v->len = perlin_r_u32(r);',
                    '    v->data = NULL;',
                    '    if (r->failed || v->len == 0) { v->len = 0; return; }',
                    '    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }',
                    f'    v->data = ({self.ctype(el)}*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);',
                    '    if (!v->data) { r->failed = 1; v->len = 0; return; }',
                    '    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {']
            out += ['        ' + l for l in self.read_expr(el, 'v->data[k_]')]
            out += ['    }', '}']
        elif ref.name == 'pair':
            out += [f'static inline void perlin_write_{m}(perlin_writer* w, const {name}* v) {{']
            out += ['    ' + l for l in self.write_expr(ref.args[0], 'v->first')]
            out += ['    ' + l for l in self.write_expr(ref.args[1], 'v->second')]
            out += ['}', f'static inline void perlin_read_{m}(perlin_reader* r, {name}* v, perlin_arena* a) {{',
                    '    (void)a;']
            out += ['    ' + l for l in self.read_expr(ref.args[0], 'v->first')]
            out += ['    ' + l for l in self.read_expr(ref.args[1], 'v->second')]
            out += ['}']
        else:
            out += [f'static inline void perlin_write_{m}(perlin_writer* w, const {name}* v) {{',
                    '    perlin_w_u8(w, v->has ? 1 : 0);', '    if (v->has) {']
            out += ['        ' + l for l in self.write_expr(ref.args[0], 'v->value')]
            out += ['    }', '}', f'static inline void perlin_read_{m}(perlin_reader* r, {name}* v, perlin_arena* a) {{',
                    '    v->has = perlin_r_u8(r);', '    if (v->has) {']
            out += ['        ' + l for l in self.read_expr(ref.args[0], 'v->value')]
            out += ['    }', '}']
        return out

    def container_typedef(self, name):
        ref = self.containers[name]
        if ref.name == 'list':
            return [f'typedef struct {name} {{ {self.ctype(ref.args[0])}* data; uint32_t len; }} {name};']
        if ref.name == 'pair':
            return [f'typedef struct {name} {{ {self.decl(ref.args[0], "first")}; {self.decl(ref.args[1], "second")}; }} {name};']
        return [f'typedef struct {name} {{ uint8_t has; {self.decl(ref.args[0], "value")}; }} {name};']


PRELUDE = r'''#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct perlin_str { const char* data; uint32_t len; } perlin_str;
typedef struct perlin_bytes { const uint8_t* data; uint32_t len; } perlin_bytes;
typedef struct perlin_writer { uint8_t* data; uint32_t len; uint32_t cap; int overflow; } perlin_writer;
typedef struct perlin_reader { const uint8_t* p; const uint8_t* end; int failed; } perlin_reader;
typedef struct perlin_arena { uint8_t* base; size_t cap; size_t used; } perlin_arena;

static inline perlin_str perlin_cstr(const char* s) {
    perlin_str out;
    out.data = s;
    out.len = 0;
    while (s && s[out.len])
        out.len++;
    return out;
}
static inline perlin_arena perlin_arena_make(void* memory, size_t size) {
    perlin_arena a;
    a.base = (uint8_t*)memory;
    a.cap = size;
    a.used = 0;
    return a;
}
static inline void* perlin_arena_alloc(perlin_arena* a, size_t size) {
    size_t at;
    if (!a)
        return NULL;
    at = (a->used + 7u) & ~(size_t)7u;
    if (at + size > a->cap)
        return NULL;
    a->used = at + size;
    return a->base + at;
}
static inline perlin_writer perlin_writer_make(void* memory, uint32_t size) {
    perlin_writer w;
    w.data = (uint8_t*)memory;
    w.len = 0;
    w.cap = size;
    w.overflow = 0;
    return w;
}
static inline perlin_reader perlin_reader_make(const void* data, uint32_t size) {
    perlin_reader r;
    r.p = (const uint8_t*)data;
    r.end = r.p + size;
    r.failed = 0;
    return r;
}
static inline void perlin_w_raw(perlin_writer* w, const void* p, uint32_t n) {
    if (w->overflow || n > w->cap - w->len) {
        w->overflow = 1;
        return;
    }
    if (n)
        __builtin_memcpy(w->data + w->len, p, n);
    w->len += n;
}
static inline void perlin_r_raw(perlin_reader* r, void* out, uint32_t n) {
    if (r->failed || (size_t)(r->end - r->p) < n) {
        r->failed = 1;
        __builtin_memset(out, 0, n);
        return;
    }
    __builtin_memcpy(out, r->p, n);
    r->p += n;
}
#define PERLIN_SCALAR_CODEC(tag, type) \
    static inline void perlin_w_##tag(perlin_writer* w, type v) { perlin_w_raw(w, &v, sizeof v); } \
    static inline type perlin_r_##tag(perlin_reader* r) { type v; perlin_r_raw(r, &v, sizeof v); return v; }
PERLIN_SCALAR_CODEC(u8, uint8_t)
PERLIN_SCALAR_CODEC(i8, int8_t)
PERLIN_SCALAR_CODEC(u16, uint16_t)
PERLIN_SCALAR_CODEC(i16, int16_t)
PERLIN_SCALAR_CODEC(u32, uint32_t)
PERLIN_SCALAR_CODEC(i32, int32_t)
PERLIN_SCALAR_CODEC(u64, uint64_t)
PERLIN_SCALAR_CODEC(i64, int64_t)
PERLIN_SCALAR_CODEC(f32, float)
PERLIN_SCALAR_CODEC(f64, double)
#undef PERLIN_SCALAR_CODEC
static inline void perlin_w_str(perlin_writer* w, perlin_str s) {
    perlin_w_u32(w, s.len);
    perlin_w_raw(w, s.data, s.len);
}
static inline void perlin_w_bytes(perlin_writer* w, perlin_bytes b) {
    perlin_w_u32(w, b.len);
    perlin_w_raw(w, b.data, b.len);
}
static inline perlin_bytes perlin_r_bytes(perlin_reader* r) {
    perlin_bytes b;
    b.len = perlin_r_u32(r);
    b.data = r->p;
    if (r->failed || (size_t)(r->end - r->p) < b.len) {
        r->failed = 1;
        b.len = 0;
        b.data = NULL;
        return b;
    }
    r->p += b.len;
    return b;
}
static inline perlin_str perlin_r_str(perlin_reader* r) {
    perlin_bytes b = perlin_r_bytes(r);
    perlin_str s;
    s.data = (const char*)b.data;
    s.len = b.len;
    return s;
}
static inline perlin_bytes perlin_r_rest(perlin_reader* r) {
    perlin_bytes b;
    b.data = r->p;
    b.len = r->failed ? 0u : (uint32_t)(r->end - r->p);
    r->p = r->end;
    return b;
}
static inline perlin_str perlin_r_rest_str(perlin_reader* r) {
    perlin_bytes b = perlin_r_rest(r);
    perlin_str s;
    s.data = (const char*)b.data;
    s.len = b.len;
    return s;
}

#if defined(__wasm__)
#define PERLIN_C_IMPORT(name) __attribute__((import_module("perlin"), import_name(#name)))
#define PERLIN_C_EXPORT(name) __attribute__((export_name(#name)))
#else
#define PERLIN_C_IMPORT(name)
#define PERLIN_C_EXPORT(name)
#endif

#ifndef PERLIN_CALL_DECLARED
#define PERLIN_CALL_DECLARED
PERLIN_C_IMPORT(call) int32_t perlin_call(uint32_t op, const void* in, uint32_t in_len, void* out, uint32_t out_cap);
#endif

static inline int32_t perlin_invoke(uint32_t op, const perlin_writer* request, perlin_reader* reply, perlin_arena* a) {
    uint32_t cap = 1024;
    for (int attempt = 0; attempt < 4; attempt++) {
        size_t mark = a->used;
        uint8_t* out = (uint8_t*)perlin_arena_alloc(a, cap);
        if (!out)
            return -6;
        int32_t rc = perlin_call(op, request ? request->data : NULL, request ? request->len : 0u, out, cap);
        if (rc >= 0) {
            *reply = perlin_reader_make(out, (uint32_t)rc);
            a->used = (size_t)(out - a->base) + (size_t)rc;
            return 0;
        }
        a->used = mark;
        if (rc < -16) {
            cap = (uint32_t)(-rc - 16);
            continue;
        }
        return rc;
    }
    return -6;
}
'''


def generate(api):
    g = CGen(api)
    structs = sorted_structs(api)
    out = ['#ifndef PERLIN_ABI_H', '#define PERLIN_ABI_H', '', PRELUDE]
    out.append(f'#define PERLIN_SCHEMA_VERSION {api.version}')
    for c in api.consts:
        v = c.value.text if c.value.kind != 'bool' else str(int(c.value.value))
        suffix = 'u' if c.type.name.startswith('u') else ''
        if c.type.name in ('u64', 'i64'):
            suffix = 'ull' if c.type.name == 'u64' else 'll'
        out.append(f'#define PERLIN_{upper(c.name)} ({v}{suffix})')
    out.append('')
    for e in api.enums:
        out.append(f'typedef {C_SCALARS[e.base]} Perlin{e.name};')
        out.append('enum {')
        if e.is_flags:
            out.append(f'    PERLIN_{upper(e.name)}_NONE = 0,')
        for m in e.members:
            value = f'(1u << {m.value})' if e.is_flags else str(m.value)
            out.append(f'    PERLIN_{upper(e.name)}_{upper(m.name)} = {value},')
        out.append('};')
    out.append('')
    out.append('enum PerlinOpId {')
    for op in sorted(api.ops, key=lambda o: o.id):
        out.append(f'    PERLIN_OP_{upper(op.name)} = 0x{op.id:04X},')
    out.append('};')
    out.append('enum PerlinEventId {')
    for ev in sorted(api.events, key=lambda e: e.id):
        out.append(f'    PERLIN_EVENT_{upper(ev.name)} = {ev.id},')
    out.append('};')
    count = max(e.id for e in api.events) + 1
    out += [f'#define PERLIN_EVENT_COUNT {count}', f'#define PERLIN_SUBSCRIPTION_WORDS {(count + 31) // 32}',
            '#define PERLIN_SUBSCRIBE(words, event) ((words)[(event) / 32] |= (1u << ((event) % 32)))', '']
    for s in structs:
        out.append(f'typedef struct Perlin{s.name} Perlin{s.name};')
    out.append('')
    bodies = []
    for s in structs:
        fields = [f'    {g.decl(f.type, f.name)};' for f in s.fields] or ['    uint8_t unused_;']
        bodies.append([f'struct Perlin{s.name} {{'] + fields + ['};'])
    defined = set()
    pending = list(g.container_order)
    struct_names = {s.name for s in structs}
    emitted = []
    for s, body in zip(structs, bodies):
        for name in list(g.container_order):
            if name in defined:
                continue
            ref = g.containers[name]
            inner = [g.unalias(a) for a in ref.args if isinstance(a, TypeRef)]
            needs_complete = ref.name != 'list' and any(a.name in struct_names and a.name not in {x.name for x in structs[:structs.index(s)]} for a in inner)
            if not needs_complete:
                emitted += g.container_typedef(name)
                defined.add(name)
        emitted += body
    for name in g.container_order:
        if name not in defined:
            emitted += g.container_typedef(name)
            defined.add(name)
    out += emitted
    out.append('')
    for s in structs:
        out.append(f'static inline void perlin_write_{s.name}(perlin_writer* w, const Perlin{s.name}* v);')
        out.append(f'static inline void perlin_read_{s.name}(perlin_reader* r, Perlin{s.name}* v, perlin_arena* a);')
    for name in g.container_order:
        m = g.mangle(g.containers[name])
        out.append(f'static inline void perlin_write_{m}(perlin_writer* w, const {name}* v);')
        out.append(f'static inline void perlin_read_{m}(perlin_reader* r, {name}* v, perlin_arena* a);')
    out.append('')
    for s in structs:
        out.append(f'static inline void perlin_write_{s.name}(perlin_writer* w, const Perlin{s.name}* v) {{')
        out.append('    (void)w;')
        out.append('    (void)v;')
        for f in s.fields:
            out += ['    ' + l for l in g.write_expr(f.type, 'v->' + f.name)]
        out.append('}')
        out.append(f'static inline void perlin_read_{s.name}(perlin_reader* r, Perlin{s.name}* v, perlin_arena* a) {{')
        out.append('    (void)r;')
        out.append('    (void)a;')
        out.append('    __builtin_memset(v, 0, sizeof *v);')
        for f in s.fields:
            out += ['    ' + l for l in g.read_expr(f.type, 'v->' + f.name)]
        out.append('}')
    for name in list(g.container_order):
        out += g.container_codec(name)
    out.append('')
    for op in sorted(api.ops, key=lambda o: o.id):
        fn = 'perlin_' + snake(op.name)
        params = []
        if op.request:
            params.append(f'const {g.ctype(op.request)}* in')
        if op.response:
            params.append(f'{g.ctype(op.response)}* out')
        params.append('perlin_arena* a')
        out.append(f'static inline int32_t {fn}({", ".join(params)}) {{')
        if op.request:
            out += ['    size_t mark_ = a->used;',
                    '    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);',
                    '    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);',
                    '    perlin_writer* w = &wb_;']
            out += ['    ' + l for l in g.write_expr(op.request, '(*in)')]
            out += ['    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }',
                    '    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;']
        out.append('    perlin_reader rb_;')
        out.append(f'    int32_t rc_ = perlin_invoke(PERLIN_OP_{upper(op.name)}, {"&wb_" if op.request else "NULL"}, &rb_, a);')
        out.append('    if (rc_ < 0) return rc_;')
        if op.response:
            out.append('    perlin_reader* r = &rb_;')
            out += ['    ' + l for l in g.read_expr(op.response, '(*out)')]
            out.append('    return rb_.failed ? -6 : 0;')
        else:
            out.append('    return 0;')
        out.append('}')
    out.append('')
    for ev in sorted(api.events, key=lambda e: e.id):
        if not ev.payload:
            continue
        fn = 'perlin_decode_event_' + snake(ev.name)
        out.append(f'static inline int {fn}(const void* data, uint32_t len, {g.ctype(ev.payload)}* out, perlin_arena* a) {{')
        out.append('    perlin_reader rb_ = perlin_reader_make(data, len);')
        out.append(f'    perlin_read_{ev.payload.name}(&rb_, out, a);')
        out.append('    return rb_.failed ? -3 : 0;')
        out.append('}')
    out += ['', '#ifdef __cplusplus', '}', '#endif', '', '#endif', '']
    return {'sdk/perlin_abi.h': '\n'.join(out)}
