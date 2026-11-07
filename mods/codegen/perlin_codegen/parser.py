import os
import re

from .model import (Alias, Api, Const, Enum, Event, Field, Member, Op, SchemaError, Struct, Tag, TagDecl,
                    TypeRef)

TOKEN = re.compile(r'''
    (?P<ws>\s+)
  | (?P<comment>\#[^\n]*)
  | (?P<string>"(?:[^"\\]|\\.)*")
  | (?P<number>-?0x[0-9A-Fa-f]+|-?\d+\.\d*(?:[eE][-+]?\d+)?|-?\d+(?:[eE][-+]?\d+)?)
  | (?P<ident>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<arrow>->)
  | (?P<ellipsis>\.\.\.)
  | (?P<punct>[;{}()<>,:=@?.])
''', re.VERBOSE)


class Literal:
    def __init__(self, kind, value, text):
        self.kind = kind
        self.value = value
        self.text = text

    def __repr__(self):
        return self.text


def tokenize(text, path):
    pos, line, out = 0, 1, []
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m:
            raise SchemaError(f'{path}:{line}: unexpected {text[pos]!r}')
        kind = m.lastgroup
        value = m.group(kind)
        if kind not in ('ws', 'comment'):
            out.append((kind, value, line))
        line += value.count('\n')
        pos = m.end()
    out.append(('eof', '', line))
    return out


class Parser:
    def __init__(self, tokens, path, api):
        self.t = tokens
        self.i = 0
        self.path = path
        self.api = api

    def peek(self, k=0):
        return self.t[self.i + k]

    def error(self, msg):
        raise SchemaError(f'{self.path}:{self.peek()[2]}: {msg}')

    def take(self, kind=None, value=None):
        tok = self.peek()
        if (kind and tok[0] != kind) or (value is not None and tok[1] != value):
            self.error(f'expected {value or kind}, found {tok[1] or tok[0]!r}')
        self.i += 1
        return tok

    def at(self, value):
        return self.peek()[1] == value and self.peek()[0] in ('punct', 'arrow', 'ident', 'ellipsis')

    def accept(self, value):
        if self.at(value):
            self.i += 1
            return True
        return False

    def ident(self):
        return self.take('ident')[1]

    def literal(self):
        kind, value, _ = self.peek()
        if kind == 'string':
            self.i += 1
            return Literal('string', bytes(value[1:-1], 'utf-8').decode('unicode_escape'), value)
        if kind == 'number':
            self.i += 1
            if value.lower().startswith(('0x', '-0x')):
                return Literal('int', int(value, 16), value)
            if any(c in value for c in '.eE') and not value.lower().startswith('0x'):
                return Literal('float', float(value), value)
            return Literal('int', int(value), value)
        if kind == 'ident':
            self.i += 1
            if value in ('true', 'false'):
                return Literal('bool', value == 'true', value)
            return Literal('ident', value, value)
        self.error('expected a value')

    def tags(self):
        out = []
        while self.at('@'):
            line = self.take()[2]
            name = self.ident()
            args = []
            spec = self.api.tags.get(name)
            if (spec is None or spec.params) and self.accept('('):
                if not self.at(')'):
                    args.append(self.literal().value)
                    while self.accept(','):
                        args.append(self.literal().value)
                self.take('punct', ')')
            out.append(Tag(name, args, line))
        return out

    def type_ref(self):
        name = self.ident()
        args = []
        if self.accept('<'):
            while True:
                if self.peek()[0] == 'number':
                    args.append(self.literal().value)
                else:
                    args.append(self.type_ref())
                if not self.accept(','):
                    break
            self.take('punct', '>')
        return TypeRef(name, args)

    def field(self, terminators):
        name = self.ident()
        self.take('punct', ':')
        ref = self.type_ref()
        default = None
        if self.accept('='):
            default = self.literal()
        tags = self.tags()
        return Field(name, ref, default, tags)

    def params(self):
        self.take('punct', '(')
        fields = []
        if not self.at(')'):
            fields.append(self.field((',', ')')))
            while self.accept(','):
                fields.append(self.field((',', ')')))
        self.take('punct', ')')
        return fields

    def synthetic(self, name, fields, line):
        s = Struct('struct', name, [], line, self.path, fields, True)
        self.api.structs.append(s)
        self.api.order.append(s)
        return TypeRef(name)

    def parse(self):
        while self.peek()[0] != 'eof':
            kw = self.ident()
            line = self.peek()[2]
            getattr(self, 'decl_' + kw, lambda: self.error(f'unknown declaration {kw!r}'))()

    def decl_api(self):
        self.api.name = self.ident()
        self.take('ident', 'version')
        self.api.version = self.literal().value
        self.take('punct', ';')

    def decl_tag(self):
        line = self.peek()[2]
        name = self.ident()
        params = []
        if self.accept('('):
            while not self.at(')'):
                p = self.ident()
                if self.accept('...'):
                    p += '...'
                elif self.accept('?'):
                    p += '?'
                params.append(p)
                self.accept(',')
            self.take('punct', ')')
        self.take('ident', 'on')
        targets = [self.ident()]
        while self.accept(','):
            targets.append(self.ident())
        tags = self.tags()
        self.take('punct', ';')
        d = TagDecl('tag', name, tags, line, self.path, params, targets)
        self.api.tags[name] = d

    def decl_const(self):
        line = self.peek()[2]
        name = self.ident()
        self.take('punct', ':')
        ref = self.type_ref()
        self.take('punct', '=')
        value = self.literal()
        tags = self.tags()
        self.take('punct', ';')
        c = Const('const', name, tags, line, self.path, ref, value)
        self.api.consts.append(c)
        self.api.order.append(c)

    def enum_body(self, flags):
        line = self.peek()[2]
        name = self.ident()
        self.take('punct', ':')
        base = self.ident()
        tags = self.tags()
        self.take('punct', '{')
        members = []
        while not self.accept('}'):
            m = self.ident()
            self.take('punct', '=')
            value = self.literal()
            if value.kind != 'int':
                self.error('enumerator values are integers')
            members.append(Member(m, value.value, self.tags()))
            self.take('punct', ';')
        e = Enum('flags' if flags else 'enum', name, tags, line, self.path, base, members, flags)
        self.api.enums.append(e)
        self.api.order.append(e)

    def decl_enum(self):
        self.enum_body(False)

    def decl_flags(self):
        self.enum_body(True)

    def decl_alias(self):
        line = self.peek()[2]
        name = self.ident()
        self.take('punct', '=')
        ref = self.type_ref()
        tags = self.tags()
        self.take('punct', ';')
        a = Alias('alias', name, tags, line, self.path, ref)
        self.api.aliases.append(a)
        self.api.order.append(a)

    def decl_struct(self):
        line = self.peek()[2]
        name = self.ident()
        tags = self.tags()
        self.take('punct', '{')
        fields = []
        while not self.accept('}'):
            fields.append(self.field((';',)))
            self.take('punct', ';')
        s = Struct('struct', name, tags, line, self.path, fields)
        self.api.structs.append(s)
        self.api.order.append(s)

    def decl_op(self):
        line = self.peek()[2]
        name = self.ident()
        self.take('punct', '=')
        ident = self.literal().value
        tags = self.tags()
        request = response = None
        if self.at('('):
            request = self.synthetic(name + 'Request', self.params(), line)
        elif self.peek()[0] == 'ident':
            request = self.type_ref()
        tags += self.tags()
        if self.accept('->'):
            if self.at('('):
                response = self.synthetic(name + 'Response', self.params(), line)
            else:
                response = self.type_ref()
        tags += self.tags()
        self.take('punct', ';')
        op = Op('op', name, tags, line, self.path, ident, request, response)
        self.api.ops.append(op)

    def decl_event(self):
        line = self.peek()[2]
        name = self.ident()
        self.take('punct', '=')
        ident = self.literal().value
        tags = self.tags()
        payload = None
        if self.at('('):
            payload = self.synthetic('Event' + name, self.params(), line)
        elif self.peek()[0] == 'ident':
            payload = self.type_ref()
        tags += self.tags()
        self.take('punct', ';')
        self.api.events.append(Event('event', name, tags, line, self.path, ident, payload))


def load(directory):
    api = Api()
    files = sorted(f for f in os.listdir(directory) if f.endswith('.papi'))
    if not files:
        raise SchemaError(f'no .papi files in {directory}')
    for f in files:
        path = os.path.join(directory, f)
        with open(path, encoding='utf-8') as fh:
            text = fh.read()
        Parser(tokenize(text, f), f, api).parse()
    from .validate import validate
    validate(api)
    return api
