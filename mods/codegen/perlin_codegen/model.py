from dataclasses import dataclass, field
from typing import Optional


SCALARS = {
    'bool': 1, 'i8': 1, 'u8': 1, 'i16': 2, 'u16': 2, 'i32': 4, 'u32': 4, 'i64': 8, 'u64': 8, 'f32': 4, 'f64': 8,
}
BUILTINS = set(SCALARS) | {'string', 'bytes', 'rest_text', 'rest_bytes'}
GENERICS = {'list': 1, 'optional': 1, 'pair': 2, 'array': 2, 'flagset': 1, 'indexset': 2}


@dataclass
class Tag:
    name: str
    args: list
    line: int = 0


@dataclass
class TypeRef:
    name: str
    args: list = field(default_factory=list)

    def __str__(self):
        if not self.args:
            return self.name
        return self.name + '<' + ', '.join(str(a) for a in self.args) + '>'


@dataclass
class Field:
    name: str
    type: TypeRef
    default: object = None
    tags: list = field(default_factory=list)


@dataclass
class Member:
    name: str
    value: int
    tags: list = field(default_factory=list)


@dataclass
class Decl:
    kind: str
    name: str
    tags: list = field(default_factory=list)
    line: int = 0
    file: str = ''

    def tag(self, name):
        for t in self.tags:
            if t.name == name:
                return t
        return None

    def has(self, name):
        return self.tag(name) is not None

    def doc(self):
        t = self.tag('doc')
        return t.args[0] if t and t.args else ''


@dataclass
class TagDecl(Decl):
    params: list = field(default_factory=list)
    targets: list = field(default_factory=list)


@dataclass
class Const(Decl):
    type: TypeRef = None
    value: object = None


@dataclass
class Enum(Decl):
    base: str = 'i32'
    members: list = field(default_factory=list)
    is_flags: bool = False


@dataclass
class Alias(Decl):
    target: TypeRef = None


@dataclass
class Struct(Decl):
    fields: list = field(default_factory=list)
    synthetic: bool = False


@dataclass
class Op(Decl):
    id: int = 0
    request: Optional[TypeRef] = None
    response: Optional[TypeRef] = None
    hosts: list = field(default_factory=list)
    permissions: list = field(default_factory=list)


@dataclass
class Event(Decl):
    id: int = 0
    payload: Optional[TypeRef] = None
    hosts: list = field(default_factory=list)


@dataclass
class Api:
    name: str = 'perlin'
    version: int = 0
    tags: dict = field(default_factory=dict)
    consts: list = field(default_factory=list)
    enums: list = field(default_factory=list)
    aliases: list = field(default_factory=list)
    structs: list = field(default_factory=list)
    ops: list = field(default_factory=list)
    events: list = field(default_factory=list)
    order: list = field(default_factory=list)

    def find(self, name):
        for group in (self.enums, self.aliases, self.structs):
            for d in group:
                if d.name == name:
                    return d
        return None

    def struct(self, name):
        for s in self.structs:
            if s.name == name:
                return s
        return None


class SchemaError(Exception):
    pass
