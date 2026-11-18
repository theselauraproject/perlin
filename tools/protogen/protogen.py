#!/usr/bin/env python3
import argparse
import json
import os
import re
import sys

PRIMITIVES = {
    "bool": ("bool", "boolean"),
    "u8": ("uint8_t", "u8"),
    "i8": ("int8_t", "i8"),
    "byterot": ("uint8_t", "u8"),
    "u16": ("uint16_t", "be"),
    "i16": ("int16_t", "be"),
    "u32": ("uint32_t", "be"),
    "i32": ("int32_t", "be"),
    "u64": ("uint64_t", "be"),
    "i64": ("int64_t", "be"),
    "f32": ("float", "be"),
    "f64": ("double", "be"),
    "lu16": ("uint16_t", "le"),
    "li16": ("int16_t", "le"),
    "lu32": ("uint32_t", "le"),
    "li32": ("int32_t", "le"),
    "lu64": ("uint64_t", "le"),
    "li64": ("int64_t", "le"),
    "lf32": ("float", "le"),
    "lf64": ("double", "le"),
    "varint": ("uint32_t", "varint"),
    "varint64": ("uint64_t", "varint"),
    "zigzag32": ("int32_t", "zigzag"),
    "zigzag64": ("int64_t", "zigzag"),
    "uuid": ("Uuid", "uuid"),
    "restBuffer": ("std::vector<uint8_t>", "rest"),
    "varint128": ("std::array<uint64_t, 2>", "varint128"),
}

INTEGRAL = {"u8", "i8", "byterot", "u16", "i16", "u32", "i32", "u64", "i64", "lu16", "li16", "lu32", "li32",
            "lu64", "li64", "varint", "varint64", "zigzag32", "zigzag64"}

KEYWORDS = set("""alignas alignof and and_eq asm auto bitand bitor bool break case catch char class compl concept
const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do
double dynamic_cast else enum explicit export extern false final float for friend goto if import inline int long
module mutable namespace new noexcept not not_eq nullptr operator or or_eq override private protected public
register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch
template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile
wchar_t while xor xor_eq""".split())

class GenError(Exception):
    pass

def words(name):
    parts = re.split(r"[^0-9A-Za-z]+", name)
    out = []
    for part in parts:
        if not part:
            continue
        out.extend(re.findall(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+|[0-9]+", part) or [part])
    return out

def pascal(name):
    text = "".join(w[:1].upper() + w[1:] for w in words(name))
    if not text or text[0].isdigit():
        text = "V" + text
    return text

def camel(name):
    text = pascal(name)
    text = text[:1].lower() + text[1:]
    if text in KEYWORDS:
        text += "_"
    return text

class Decl:
    def __init__(self, kind, name):
        self.kind = kind
        self.name = name
        self.lines = []
        self.members = []
        self.codec = []
        self.packet = None
        self.extra = []

class Generator:
    def __init__(self, spec, overrides):
        self.types = dict(spec["types"])
        for name, expr in overrides.get("types", {}).items():
            self.types[name] = expr
        for path, expr in overrides.get("retype", {}).items():
            owner, field = path.split(".")
            body = self.types[owner]
            for entry in body[1]:
                if entry.get("name") == field:
                    entry["type"] = expr
                    break
            else:
                raise GenError(f"retype target {path} not found")
        self.custom = overrides.get("custom", {})
        self.extra_packets = overrides.get("packets", {})
        self.skip_types = set(overrides.get("skip", []))
        self.decls = []
        self.names = set()
        self.memo = {}
        self.named = {}
        self.building = set()
        self.reserved = {}
        self.recursive = set()
        self.warnings = []
        self.opaque = []
        self.by_name = {}
        self.enums = {}
        self.stack = []
        self.slots = set()

    def unique(self, base):
        name = base
        n = 2
        while name in self.names or name in KEYWORDS:
            name = f"{base}{n}"
            n += 1
        self.names.add(name)
        return name

    def native(self, name):
        return self.types.get(name) == "native"

    def resolve(self, expr):
        seen = 0
        while isinstance(expr, str) and expr in self.types and self.types[expr] != "native":
            body = self.types[expr]
            if isinstance(body, list) and body[0] in ("container", "mapper", "bitfield"):
                return expr
            expr = body
            seen += 1
            if seen > 16:
                raise GenError(f"alias loop at {expr}")
        return expr

    def definition(self, expr):
        if isinstance(expr, str) and expr in self.types and self.types[expr] != "native":
            return self.types[expr]
        return expr

    def kind_of(self, expr):
        if isinstance(expr, str):
            if expr == "void":
                return "void"
            if expr in PRIMITIVES:
                return "prim"
            if expr in ("nbt", "lnbt", "nbtLoop"):
                return "nbt"
            if expr in self.types and self.types[expr] != "native":
                return "named"
            raise GenError(f"unsupported type '{expr}'")
        return expr[0]

    def prim_cpp(self, name):
        return PRIMITIVES[name][0]

    def cpp_type(self, expr, hint):
        key = id(expr)
        if isinstance(expr, str):
            kind = self.kind_of(expr)
            if kind == "void":
                return "void"
            if kind == "prim":
                return self.prim_cpp(expr)
            if kind == "nbt":
                return "Nbt" if expr != "nbtLoop" else "std::vector<Nbt>"
            return self.named_type(expr)
        if key in self.memo:
            return self.memo[key]
        kind = expr[0]
        args = expr[1] if len(expr) > 1 else None
        if kind == "container":
            result = self.make_struct(self.unique(pascal(hint)), args).name
        elif kind == "mapper":
            result = self.make_enum(self.unique(pascal(hint)), args)
        elif kind == "bitfield":
            result = self.make_bitfield(self.unique(pascal(hint)), expr[1])
        elif kind == "array":
            inner = self.cpp_type(args["type"], hint + "_entry")
            if inner == "bool":
                inner = "uint8_t"
            if "countType" in args:
                result = f"std::vector<{inner}>"
            elif "count" in args and isinstance(args["count"], int):
                result = f"std::array<{inner}, {args['count']}>"
            else:
                result = f"std::vector<{inner}>"
        elif kind == "maybeIncompleteArray":
            result = f"std::vector<{self.cpp_type(args['type'], hint + '_entry')}>"
        elif kind == "option" or kind == "optionalOnRemaining":
            inner = args if kind == "option" else args["type"]
            result = f"std::optional<{self.cpp_type(inner, hint)}>"
        elif kind == "bitflags":
            result = self.prim_cpp(args["type"])
            self.record_flags(hint, args)
        elif kind == "pstring":
            result = "std::string"
        elif kind == "buffer":
            result = "std::vector<uint8_t>"
        elif kind == "encapsulated":
            result = self.cpp_type(args["type"], hint)
        elif kind == "switch":
            result = self.switch_type(args, hint)
        else:
            raise GenError(f"unsupported construct '{kind}'")
        self.memo[key] = result
        return result

    def named_type(self, name):
        if name in self.named:
            return self.named[name]
        body = self.types[name]
        if name in self.building:
            if name in self.reserved:
                self.recursive.add(self.reserved[name])
                return self.reserved[name]
            raise GenError(f"recursive type '{name}'")
        self.building.add(name)
        try:
            if isinstance(body, list) and body[0] == "container":
                reserved = self.unique(pascal(name))
                self.reserved[name] = reserved
                self.names.discard(reserved)
                cpp = self.make_struct(reserved, body[1]).name
            elif isinstance(body, list) and body[0] == "mapper":
                cpp = self.make_enum(self.unique(pascal(name)), body[1])
            elif isinstance(body, list) and body[0] == "bitfield":
                cpp = self.make_bitfield(self.unique(pascal(name)), body[1])
            else:
                if isinstance(body, list) and body[0] == "bitflags":
                    self.record_flags(name, body[1])
                cpp = self.cpp_type(body, name)
        finally:
            self.building.discard(name)
        self.named[name] = cpp
        return cpp

    def record_flags(self, hint, args):
        flags = args.get("flags")
        if isinstance(flags, list):
            items = [(n, 1 << i) for i, n in enumerate(flags)]
        else:
            items = [(n, int(v)) for n, v in flags.items()]
        name = self.unique("Flags" + pascal(hint)) if hint else None
        if not name:
            return
        decl = Decl("flags", name)
        ctype = self.prim_cpp(args["type"])
        decl.lines.append(f"namespace {name} {{")
        used = set()
        bits = 64 if "64" in ctype else 32 if "32" in ctype else 16 if "16" in ctype else 8
        unsigned = "uint" + str(bits) + "_t"
        for n, v in items:
            if v >= (1 << bits) or v < 0:
                continue
            ident = pascal(n)
            while ident in used:
                ident += "_"
            used.add(ident)
            decl.lines.append(f"    constexpr {unsigned} {ident} = {v}ull;")
        decl.lines.append("}")
        self.decls.append(decl)

    def make_enum(self, name, args):
        under = self.prim_cpp(args["type"])
        decl = Decl("enum", name)
        used = set()
        values = []
        for key, label in args["mappings"].items():
            number = int(key, 0)
            ident = pascal(str(label))
            while ident in used:
                ident += "_"
            used.add(ident)
            values.append((ident, number, str(label)))
        decl.lines.append(f"enum class {name} : {under} {{")
        bits = int(re.search(r"(\d+)", under).group(1))
        for ident, number, _ in values:
            if under.startswith("uint") and number < 0:
                number += 1 << bits
            decl.lines.append(f"    {ident} = {number}{'ull' if bits == 64 else ''},")
        decl.lines.append("};")
        decl.extra = values
        decl.under = args["type"]
        self.decls.append(decl)
        self.enums[name] = decl
        return name

    def make_bitfield(self, name, fields):
        decl = Decl("struct", name)
        total = sum(f["size"] for f in fields)
        if total % 8:
            raise GenError(f"bitfield {name} is not byte aligned")
        decl.lines.append(f"struct {name} {{")
        used = set()
        body = []
        shift = total
        nbytes = total // 8
        for f in fields:
            ident = camel(f["name"])
            while ident in used:
                ident += "_"
            used.add(ident)
            ctype = "int32_t" if f.get("signed") else "uint32_t"
            if f["size"] > 32:
                ctype = "int64_t" if f.get("signed") else "uint64_t"
            decl.lines.append(f"    {ctype} {ident}{{}};")
            shift -= f["size"]
            mask = (1 << f["size"]) - 1
            body.append((ident, ctype, shift, mask, f["size"], bool(f.get("signed"))))
        decl.lines.append("};")
        codec = ["uint64_t bits = 0;", "if constexpr (S::kReading) {", f"    std::vector<uint8_t> raw;", f"    s.bytes(raw, {nbytes});",
                 "    if (!s.ok) return;", "    for (uint8_t b : raw) bits = (bits << 8) | b;"]
        for ident, ctype, sh, mask, size, signed in body:
            if signed:
                codec.append(f"    {{ uint64_t part = (bits >> {sh}) & {mask}ull; v.{ident} = (part & {1 << (size - 1)}ull) ? static_cast<{ctype}>(part) - {1 << size} : static_cast<{ctype}>(part); }}")
            else:
                codec.append(f"    v.{ident} = static_cast<{ctype}>((bits >> {sh}) & {mask}ull);")
        codec.append("} else {")
        for ident, ctype, sh, mask, size, signed in body:
            codec.append(f"    bits |= (static_cast<uint64_t>(v.{ident}) & {mask}ull) << {sh};")
        codec.append(f"    std::vector<uint8_t> raw({nbytes});")
        codec.append(f"    for (int i = 0; i < {nbytes}; i++) raw[i] = static_cast<uint8_t>(bits >> (8 * ({nbytes - 1} - i)));")
        codec.append(f"    s.bytes(raw, {nbytes});")
        codec.append("}")
        decl.codec = codec
        decl.bits = [b[0] for b in body]
        decl.bit_names = {f["name"]: camel_name for f, camel_name in zip(fields, decl.bits)}
        self.decls.append(decl)
        self.by_name[name] = decl
        return name

    def switch_type(self, args, hint):
        distinct = []
        for key, body in args["fields"].items():
            if self.is_void(body):
                continue
            if body not in distinct:
                distinct.append(body)
        default = args.get("default")
        if default is not None and not self.is_void(default) and default not in distinct:
            distinct.append(default)
        if not distinct:
            return None
        types = []
        for i, body in enumerate(distinct):
            suffix = "" if len(distinct) == 1 else f"_case{i}"
            types.append(self.cpp_type(body, hint + suffix))
        if len(types) == 1:
            return f"std::optional<{types[0]}>"
        return "std::variant<std::monostate, " + ", ".join(types) + ">"

    def is_void(self, body):
        return body is None or body == "void"

    def flag_condition(self, member, texpr, flag, key):
        target = self.definition(self.resolve(texpr))
        if not (isinstance(target, list) and target[0] == "bitflags"):
            raise GenError(f"'{flag}' is not a flag")
        args = target[1]
        flags = args["flags"]
        if isinstance(flags, list):
            if flag not in flags:
                raise GenError(f"flag '{flag}' not found")
            bit = flags.index(flag)
            mask = 1 << bit
        else:
            if flag not in flags:
                raise GenError(f"flag '{flag}' not found")
            mask = int(flags[flag])
            bit = mask.bit_length() - 1
        if args["type"] == "varint128":
            test = f"((({member})[{bit // 64}] >> {bit % 64}) & 1ull) != 0"
        else:
            test = f"(static_cast<uint64_t>({member}) & {mask}ull) != 0"
        return test if key == "true" else f"!({test})"

    def condition(self, scope, discriminator, key):
        if discriminator.startswith("../"):
            field = discriminator[3:]
            if not self.stack:
                raise GenError(f"switch discriminator '{discriminator}' has no parent")
            pname, pscope, needs = self.stack[-1]
            if field not in pscope:
                raise GenError(f"switch discriminator '{discriminator}' not found")
            slot = f"parentSlot{pascal(pname)}{pascal(field)}"
            needs[field] = slot
            self.slots.add(slot)
            member, texpr = pscope[field]
            cpp = self.cpp_type(texpr, field)
            inner = self.condition({field: (f"static_cast<{cpp}>({slot})", texpr)}, field, key)
            return inner
        if "." in discriminator and discriminator.split(".", 1)[0] in scope:
            base, flag = discriminator.split(".", 1)
            member, texpr = scope[base]
            return self.flag_condition(member, texpr, flag, key)
        if key.startswith("/"):
            if discriminator not in scope:
                raise GenError(f"switch discriminator '{discriminator}' not found")
            return f"{scope[discriminator][0]} == net::context::{key[1:]}()"
        if discriminator not in scope:
            raise GenError(f"switch discriminator '{discriminator}' not found")
        member, texpr = scope[discriminator]
        resolved = self.resolve(texpr)
        if isinstance(resolved, list) and resolved[0] == "option":
            inner = self.condition({discriminator: (f"(*{member})", resolved[1])}, discriminator, key)
            return f"{member}.has_value() && {inner}"
        target = self.definition(resolved)
        if isinstance(target, list) and target[0] == "mapper":
            enum = self.cpp_type(resolved if isinstance(resolved, str) else texpr, discriminator)
            for ident, number, label in self.enums[enum].extra:
                if label == key:
                    return f"{member} == {enum}::{ident}"
            try:
                return f"static_cast<int64_t>({member}) == {int(key, 0)}"
            except ValueError:
                raise GenError(f"case '{key}' not in mapper for {discriminator}")
        if resolved == "bool":
            return f"{member} == {'true' if key == 'true' else 'false'}"
        if isinstance(resolved, str) and resolved in INTEGRAL:
            try:
                return f"{member} == {int(key, 0)}"
            except ValueError:
                raise GenError(f"unsupported switch key '{key}'")
        if isinstance(resolved, str) and resolved == "string" or (isinstance(target, list) and target[0] == "pstring"):
            return f"{member} == \"{key}\""
        raise GenError(f"cannot compare switch on {discriminator}: {resolved}")

    def plan_fields(self, fields, ctx):
        steps = []
        for f in fields:
            ftype = f["type"]
            if f.get("anon"):
                if isinstance(ftype, list) and ftype[0] == "switch":
                    args = ftype[1]
                    branches = []
                    cases = list(args["fields"].items())
                    if args.get("default") is not None:
                        cases.append(("__default", args["default"]))
                    for key, body in cases:
                        if self.is_void(body):
                            branches.append((key, []))
                            continue
                        body = self.definition(body)
                        if not (isinstance(body, list) and body[0] == "container"):
                            raise GenError("anonymous switch branch is not a container")
                        branches.append((key, self.plan_fields(body[1], ctx)))
                    steps.append(("anon_switch", args, branches))
                elif isinstance(ftype, list) and ftype[0] == "container":
                    steps.extend(self.plan_fields(ftype[1], ctx))
                else:
                    raise GenError("unsupported anonymous field")
                continue
            spec = f["name"]
            key = (spec, json.dumps(ftype, sort_keys=True))
            if key in ctx["keys"]:
                mname = ctx["keys"][key]
            else:
                ident = camel(spec)
                n = 2
                base = ident
                while ident in ctx["names"]:
                    ident = f"{base}{n}"
                    n += 1
                ctx["names"].add(ident)
                ctx["keys"][key] = ident
                ctx["members"].append((ident, ftype, spec))
                mname = ident
            ctx["scope"][spec] = (f"v.{mname}", ftype)
            steps.append(("field", mname, ftype, spec))
        return steps

    def make_struct(self, name, fields):
        decl = Decl("struct", name)
        self.names.add(name)
        ctx = {"keys": {}, "names": set(), "members": [], "scope": {}}
        steps = self.plan_fields(fields, ctx)
        scope = ctx["scope"]
        members = ctx["members"]

        needs = {}
        self.stack.append((name, scope, needs))
        member_lines = []
        try:
            for mname, mtype, spec in members:
                cpp = self.cpp_type(mtype, f"{name}_{spec}")
                if cpp is None or cpp == "void":
                    continue
                member_lines.append(f"    {cpp} {mname}{{}};")
        finally:
            self.stack.pop()
        self.needs = needs
        decl.lines.append(f"struct {name} {{")
        decl.lines.extend(member_lines)
        decl.lines.append("};")
        decl.member_types = [self.cpp_type(t, f"{name}_{sp}") for _, t, sp in members]

        code = self.emit_steps(steps, scope, name)
        for mname, mtype, spec in members:
            definition = self.definition(mtype)
            if isinstance(definition, list) and definition[0] == "array" and isinstance(definition[1].get("count"), str):
                ref = definition[1]["count"]
                code.insert(0, f"if constexpr (!S::kReading) {scope[ref][0]} = static_cast<decltype({scope[ref][0]})>(v.{mname}.size());")
        decl.codec = code
        decl.fields = [(spec, mname, mtype) for mname, mtype, spec in members]
        self.decls.append(decl)
        self.by_name[name] = decl
        return decl

    def emit_steps(self, steps, scope, owner, depth=0):
        code = []
        needs = self.needs
        for step in steps:
            if step[0] == "field":
                _, mname, mtype, spec = step
                code.extend(self.emit(mtype, f"v.{mname}", scope, f"{owner}_{spec}", "s", "S", depth, True))
                if spec in needs:
                    code.append(f"{needs[spec]} = static_cast<int64_t>(v.{mname});")
            else:
                _, args, branches = step
                code.extend(self.emit_anon_switch(args, branches, scope, owner, depth))
        return code

    def emit_anon_switch(self, args, branches, scope, owner, depth):
        lines = []
        first = True
        for key, steps in branches:
            body = self.emit_steps(steps, scope, owner, depth + 1)
            if key == "__default":
                if first:
                    lines.extend(body)
                    continue
                lines.append("else {")
            else:
                cond = self.condition(scope, args["compareTo"], key)
                lines.append(f"{'if' if first else 'else if'} ({cond}) {{")
            lines.extend("    " + l for l in body)
            lines.append("}")
            first = False
        return lines

    def emit(self, expr, target, scope, hint, s, S, depth, top=False):
        kind = self.kind_of(expr)
        lines = []
        if kind == "void":
            return lines
        if kind == "prim":
            op = PRIMITIVES[expr][1]
            lines.append(f"{s}.{op}({target});")
            return lines
        if kind == "nbt":
            if expr == "nbtLoop":
                lines += [f"if constexpr ({S}::kReading) {{", f"    {target}.clear();",
                          f"    while ({s}.more() && {s}.ok) {{ Nbt item; {s}.nbt(item, false); {target}.push_back(std::move(item)); }}",
                          "} else {", f"    for (auto& item : {target}) {s}.nbt(item, false);", "}"]
                return lines
            lines.append(f"{s}.nbt({target}, {'true' if expr == 'lnbt' else 'false'});")
            return lines
        if kind == "named":
            body = self.types[expr]
            if isinstance(body, list) and body[0] in ("container", "bitfield"):
                lines.append(f"Transfer({s}, {target});")
                return lines
            if isinstance(body, list) and body[0] == "mapper":
                return self.emit_mapper(body[1], self.named_type(expr), target, s, S)
            return self.emit(body, target, scope, expr, s, S, depth)
        args = expr[1] if len(expr) > 1 else None
        if kind in ("container", "bitfield"):
            lines.append(f"Transfer({s}, {target});")
            return lines
        if kind == "mapper":
            return self.emit_mapper(args, self.cpp_type(expr, hint), target, s, S)
        if kind == "bitflags":
            return self.emit(args["type"], target, scope, hint, s, S, depth)
        if kind == "pstring":
            ctype, op = PRIMITIVES[args["countType"]][0], PRIMITIVES[args["countType"]][1]
            n = f"n{depth}"
            lines += ["{", f"    {ctype} {n} = static_cast<{ctype}>({target}.size());", f"    {s}.{op}({n});",
                      f"    if constexpr ({S}::kReading) {{ if (!{s}.length({n})) return; }}",
                      f"    {s}.chars({target}, static_cast<size_t>({n}));", "}"]
            return lines
        if kind == "buffer" and "countType" not in args and isinstance(args.get("count"), int):
            lines.append(f"{s}.bytes({target}, {args['count']});")
            return lines
        if kind == "buffer":
            ctype, op = PRIMITIVES[args["countType"]][0], PRIMITIVES[args["countType"]][1]
            n = f"n{depth}"
            lines += ["{", f"    {ctype} {n} = static_cast<{ctype}>({target}.size());", f"    {s}.{op}({n});",
                      f"    if constexpr ({S}::kReading) {{ if (!{s}.length({n})) return; }}",
                      f"    {s}.bytes({target}, static_cast<size_t>({n}));", "}"]
            return lines
        if kind in ("array", "maybeIncompleteArray"):
            element = f"e{depth}"
            inner = self.emit(args["type"], element, scope, hint + "_entry", s, S, depth + 1)
            if "countType" in args:
                ctype, op = PRIMITIVES[args["countType"]][0], PRIMITIVES[args["countType"]][1]
                n = f"n{depth}"
                lines += ["{", f"    {ctype} {n} = static_cast<{ctype}>({target}.size());", f"    {s}.{op}({n});",
                          f"    if constexpr ({S}::kReading) {{ if (!{s}.count({n})) return; {target}.clear(); {target}.resize(static_cast<size_t>({n})); }}"]
                if kind == "maybeIncompleteArray":
                    lines.append(f"    if constexpr ({S}::kReading) {{ size_t kept = 0; for (auto& {element} : {target}) {{ if (!{s}.more()) break; kept++; ")
                    lines.extend("        " + l for l in inner)
                    lines.append("    } {target}.resize(kept); } else {".replace("{target}", target))
                    lines.append(f"    for (auto& {element} : {target}) {{")
                    lines.extend("        " + l for l in inner)
                    lines.append("    } }")
                else:
                    lines.append(f"    for (auto& {element} : {target}) {{")
                    lines.extend("        " + l for l in inner)
                    lines.append("    }")
                lines.append("}")
                return lines
            count = args["count"]
            if isinstance(count, int):
                lines.append(f"for (auto& {element} : {target}) {{")
                lines.extend("    " + l for l in inner)
                lines.append("}")
                return lines
            if count not in scope:
                raise GenError(f"array count field '{count}' not found")
            counter = scope[count][0]
            lines += [f"if constexpr ({S}::kReading) {{ if (!{s}.count({counter})) return; {target}.clear(); {target}.resize(static_cast<size_t>({counter})); }}",
                      f"for (auto& {element} : {target}) {{"]
            lines.extend("    " + l for l in inner)
            lines.append("}")
            return lines
        if kind == "option":
            inner = self.emit(args, f"(*{target})", scope, hint, s, S, depth + 1)
            lines.append(f"if (Present({s}, {target})) {{")
            lines.extend("    " + l for l in inner)
            lines.append("}")
            return lines
        if kind == "optionalOnRemaining":
            inner = self.emit(args["type"], f"(*{target})", scope, hint, s, S, depth + 1)
            lines.append(f"if (PresentIfRemaining({s}, {target})) {{")
            lines.extend("    " + l for l in inner)
            lines.append("}")
            return lines
        if kind == "encapsulated":
            inner = self.emit(args["type"], target, scope, hint, "sub", "SS", depth + 1)
            lines.append(f"Frame({s}, [&](auto& sub) {{")
            if any(re.search(r"\bSS\b", l) for l in inner):
                lines.append("    using SS = std::remove_cvref_t<decltype(sub)>;")
            lines.extend("    " + l for l in inner)
            lines.append("});")
            return lines
        if kind == "switch":
            return self.emit_switch(args, target, scope, hint, s, S, depth)
        raise GenError(f"cannot emit '{kind}'")

    def emit_mapper(self, args, enum, target, s, S):
        under = PRIMITIVES[args["type"]][0]
        op = PRIMITIVES[args["type"]][1]
        return ["{", f"    {under} raw = static_cast<{under}>({target});", f"    {s}.{op}(raw);",
                f"    {target} = static_cast<{enum}>(raw);", "}"]

    def emit_switch(self, args, target, scope, hint, s, S, depth):
        distinct = []
        for key, body in args["fields"].items():
            if not self.is_void(body) and body not in distinct:
                distinct.append(body)
        default = args.get("default")
        if default is not None and not self.is_void(default) and default not in distinct:
            distinct.append(default)
        if not distinct:
            return []
        lines = [f"if constexpr ({S}::kReading) {target} = {{}};"]
        groups = []
        for body in distinct:
            keys = [k for k, b in args["fields"].items() if b == body]
            groups.append((body, keys))
        first = True
        single = len(distinct) == 1
        for index, (body, keys) in enumerate(groups):
            is_default = default is not None and not self.is_void(default) and body == default and not keys
            cpp = self.cpp_type(body, hint if single else f"{hint}_case{index}")
            pointer = f"p{depth}"
            inner = self.emit(body, f"(*{pointer})", scope, hint, s, S, depth + 1)
            if is_default:
                lines.append("else {" if not first else "{")
            else:
                parts = [self.condition(scope, args['compareTo'], k) for k in keys]
                cond = parts[0] if len(parts) == 1 else " || ".join(f"({c})" for c in parts)
                lines.append(f"{'if' if first else 'else if'} ({cond}) {{")
            picker = f"Pick<{cpp}>" if single else f"PickAt<{index + 1}>"
            lines.append(f"    if (auto* {pointer} = {picker}({s}, {target})) {{")
            lines.extend("        " + l for l in inner)
            lines.append("    }")
            lines.append("}")
            first = False
        return lines

def hard_deps(cpp, known):
    out = []
    i = 0
    text = cpp
    stripped = []
    while i < len(text):
        if text.startswith("std::vector<", i):
            depth = 0
            j = i + len("std::vector")
            while j < len(text):
                if text[j] == "<":
                    depth += 1
                elif text[j] == ">":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            i = j + 1
            continue
        stripped.append(text[i])
        i += 1
    for ident in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", "".join(stripped)):
        if ident in known:
            out.append(ident)
    return out

def order_decls(decls):
    known = {d.name for d in decls}
    by_name = {d.name: d for d in decls}
    done = set()
    result = []

    def visit(d, trail):
        if d.name in done:
            return
        if d.name in trail:
            return
        trail = trail | {d.name}
        for cpp in getattr(d, "member_types", []):
            if cpp is None:
                continue
            for dep in hard_deps(cpp, known):
                if dep != d.name:
                    visit(by_name[dep], trail)
        if d.name not in done:
            done.add(d.name)
            result.append(d)

    for d in decls:
        visit(d, frozenset())
    return result

def defer_incomplete(decls):
    position = {d.name: i for i, d in enumerate(decls) if d.kind == "struct"}
    for index, d in enumerate(decls):
        if d.kind != "struct":
            continue
        later = False
        for cpp in getattr(d, "member_types", []):
            for word in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", cpp or ""):
                if word != d.name and position.get(word, -1) > index:
                    later = True
        if not later:
            continue
        d.deferred = True
        n = d.name
        for i, line in enumerate(d.lines):
            if line.startswith("    ") and line.endswith("{};") and any(
                    position.get(w, -1) > index for w in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", line)):
                d.lines[i] = line[:-3] + ";"
        d.lines[-1:] = [f"    {n}();", f"    ~{n}();", f"    {n}(const {n}&);", f"    {n}({n}&&) noexcept;",
                        f"    {n}& operator=(const {n}&);", f"    {n}& operator=({n}&&) noexcept;", "};"]

def struct_name_for_packet(name):
    return pascal(name) + "Packet"

def build(gen):
    packet_map = gen.types["mcpe_packet"]
    wrapper = packet_map[1]
    ids = None
    params = None
    for f in wrapper:
        if f.get("name") == "name":
            ids = {int(k): v for k, v in f["type"][1]["mappings"].items()}
        if f.get("name") == "params":
            params = f["type"][1]["fields"]
    packets = []
    for number, name in sorted(ids.items()):
        ptype = params.get(name)
        packets.append((number, name, ptype))
    for name, info in gen.extra_packets.items():
        packets.append((info["id"], name, info["type"]))
    return packets

def emit_codec(decl, bridged=False):
    name = decl.name
    out = []
    out.append(f"template <class S> static void Serialize(S& s, {name}& v) {{")
    out.append("    (void)s; (void)v;")
    out.extend("    " + l for l in decl.codec)
    out.append("}")
    if getattr(decl, "deferred", False):
        out.append(f"{name}::{name}() = default;")
        out.append(f"{name}::~{name}() = default;")
        out.append(f"{name}::{name}(const {name}&) = default;")
        out.append(f"{name}::{name}({name}&&) noexcept = default;")
        out.append(f"{name}& {name}::operator=(const {name}&) = default;")
        out.append(f"{name}& {name}::operator=({name}&&) noexcept = default;")
    if bridged:
        out.append(f"void Write(WriteStream& s, const {name}& v) {{ if (BridgeWrite(s, v)) return; Serialize(s, const_cast<{name}&>(v)); }}")
        out.append(f"bool Read(ReadStream& s, {name}& v) {{ const int r = BridgeRead(s, v); if (r >= 0) return r != 0; Serialize(s, v); return s.ok; }}")
    else:
        out.append(f"void Write(WriteStream& s, const {name}& v) {{ Serialize(s, const_cast<{name}&>(v)); }}")
        out.append(f"bool Read(ReadStream& s, {name}& v) {{ Serialize(s, v); return s.ok; }}")
    return out

class Generated:
    pass

def generate(spec, overrides):
    gen = Generator(spec, overrides)
    packets = build(gen)
    packet_decls = []
    registry = []
    opaque = []
    for number, name, ptype in packets:
        struct = struct_name_for_packet(name)
        if name in gen.custom:
            registry.append((number, name, struct, "custom", gen.custom[name]))
            gen.names.add(struct)
            continue
        if ptype is None or ptype not in gen.types:
            raise GenError(f"packet {name} has no type")
        body = gen.types[ptype]
        mark = len(gen.decls)
        names_before = set(gen.names)
        saved = dict(gen.named), dict(gen.memo)
        try:
            if not (isinstance(body, list) and body[0] == "container"):
                raise GenError("packet body is not a container")
            decl = gen.make_struct(gen.unique(struct), body[1])
            decl.packet = (number, name)
            decl.ptype = ptype
            packet_decls.append(decl)
            registry.append((number, name, decl.name, "generated", None))
        except (GenError, KeyError) as e:
            del gen.decls[mark:]
            gen.names = names_before
            gen.named, gen.memo = saved
            decl = Decl("struct", gen.unique(struct))
            decl.lines = [f"struct {decl.name} {{", "    std::vector<uint8_t> payload;", "};"]
            decl.codec = ["s.rest(v.payload);"]
            decl.packet = (number, name)
            gen.decls.append(decl)
            packet_decls.append(decl)
            registry.append((number, name, decl.name, "opaque", str(e)))
            opaque.append((name, str(e)))
    g = Generated()
    g.gen = gen
    g.registry = registry
    g.packet_decls = packet_decls
    g.opaque = opaque
    g.by_packet = {d.packet[1]: d for d in packet_decls}
    g.mode = {name: mode for _, name, _, mode, _ in registry}
    return g

def legacy_overrides(spec, overrides):
    out = {}
    retype = {}
    for path, expr in overrides.get("retype", {}).items():
        owner, field = path.split(".")
        body = spec["types"].get(owner)
        if isinstance(body, list) and body[0] == "container" and any(e.get("name") == field for e in body[1]):
            retype[path] = expr
    out["retype"] = retype
    return out

def expand(types, expr, memo, stack):
    if isinstance(expr, str):
        if expr in types and types[expr] != "native":
            if expr in memo:
                return memo[expr]
            if expr in stack:
                return ["ref", expr]
            value = expand(types, types[expr], memo, stack | {expr})
            memo[expr] = value
            return value
        return expr
    if isinstance(expr, list):
        return [expand(types, e, memo, stack) for e in expr]
    if isinstance(expr, dict):
        return {k: expand(types, v, memo, stack) for k, v in expr.items()}
    return expr

class Shape:
    def __init__(self, kind, cpp=None, args=None, expr=None):
        self.kind = kind
        self.cpp = cpp
        self.args = args
        self.expr = expr

def shape(gen, expr, depth=0):
    if depth > 32:
        return Shape("unknown")
    if isinstance(expr, str):
        if expr == "void":
            return Shape("void")
        if expr in PRIMITIVES:
            return Shape("prim", PRIMITIVES[expr][0], expr)
        if expr in ("nbt", "lnbt"):
            return Shape("nbt", "Nbt")
        if expr == "nbtLoop":
            return Shape("nbtloop", "std::vector<Nbt>")
        if expr in gen.types and gen.types[expr] != "native":
            body = gen.types[expr]
            if isinstance(body, list) and body[0] in ("container", "bitfield"):
                cpp = gen.named.get(expr)
                return Shape("struct", cpp) if cpp else Shape("unknown")
            if isinstance(body, list) and body[0] == "mapper":
                cpp = gen.named.get(expr)
                return Shape("enum", cpp, body[1]) if cpp else Shape("unknown")
            return shape(gen, body, depth + 1)
        return Shape("unknown")
    if not isinstance(expr, list) or not expr:
        return Shape("unknown")
    kind = expr[0]
    args = expr[1] if len(expr) > 1 else None
    if kind in ("container", "bitfield"):
        cpp = gen.memo.get(id(expr))
        return Shape("struct", cpp) if cpp else Shape("unknown")
    if kind == "mapper":
        cpp = gen.memo.get(id(expr))
        return Shape("enum", cpp, args) if cpp else Shape("unknown")
    if kind in ("array", "maybeIncompleteArray"):
        fixed = kind == "array" and "countType" not in args and isinstance(args.get("count"), int)
        return Shape("array", None, args, "fixed" if fixed else "vector")
    if kind == "option":
        return Shape("option", None, args)
    if kind == "optionalOnRemaining":
        return Shape("option", None, args["type"])
    if kind == "bitflags":
        return Shape("flags", PRIMITIVES[args["type"]][0], args)
    if kind == "pstring":
        return Shape("string", "std::string")
    if kind == "buffer":
        return Shape("bytes", "std::vector<uint8_t>")
    if kind == "encapsulated":
        return shape(gen, args["type"], depth + 1)
    if kind == "switch":
        cpp = gen.memo.get(id(expr))
        if not cpp:
            return Shape("void")
        return Shape("switch", cpp, args)
    return Shape("unknown")

def flag_masks(args):
    flags = args.get("flags")
    if isinstance(flags, list):
        return {n: 1 << i for i, n in enumerate(flags)}
    return {n: int(v) for n, v in flags.items()}

def mask_literal(mask):
    return f"{mask & ((1 << 64) - 1)}ull, {mask >> 64}ull"

def switch_groups(args, is_void):
    distinct = []
    for key, body in args["fields"].items():
        if not is_void(body) and body not in distinct:
            distinct.append(body)
    default = args.get("default")
    if default is not None and not is_void(default) and default not in distinct:
        distinct.append(default)
    groups = []
    for body in distinct:
        keys = {k for k, b in args["fields"].items() if b == body}
        if default is not None and body == default:
            keys.add("__default")
        groups.append((body, keys))
    return groups

class Bridge:
    def __init__(self, old, new, old_ns, new_ns):
        self.old = old
        self.new = new
        self.old_ns = old_ns
        self.new_ns = new_ns
        self.pairs = {}
        self.order = []
        self.bodies = {}
        self.counter = 0

    def ns(self, gen):
        return self.old_ns if gen is self.old else self.new_ns

    def struct_fn(self, fg, fcpp, tg, tcpp):
        key = (id(fg), fcpp, tcpp)
        if key in self.pairs:
            return self.pairs[key]
        fn = f"C{len(self.pairs)}"
        self.pairs[key] = fn
        self.order.append((key, fg, fcpp, tg, tcpp, fn))
        return fn

    def build_struct(self, key, fg, fcpp, tg, tcpp, fn):
        fdecl = fg.by_name.get(fcpp)
        tdecl = tg.by_name.get(tcpp)
        lines = []
        if fdecl is not None and tdecl is not None:
            if hasattr(tdecl, "bit_names") and hasattr(fdecl, "bit_names"):
                for spec, ident in tdecl.bit_names.items():
                    if spec in fdecl.bit_names:
                        lines.append(f"b.{ident} = static_cast<decltype(b.{ident})>(a.{fdecl.bit_names[spec]});")
            elif hasattr(tdecl, "fields") and hasattr(fdecl, "fields"):
                used = set()
                for spec, tm, texpr in tdecl.fields:
                    tsh = shape(tg, texpr)
                    if tsh.kind in ("void", "unknown"):
                        continue
                    cands = [(s, m, e) for s, m, e in fdecl.fields if s == spec and m not in used]
                    if not cands:
                        continue
                    pick = None
                    for c in cands:
                        if json.dumps(c[2], sort_keys=True, default=str) == json.dumps(texpr, sort_keys=True, default=str):
                            pick = c
                            break
                    if pick is None:
                        pick = cands[0]
                    used.add(pick[1])
                    fsh = shape(fg, pick[2])
                    lines.extend(self.conv(fg, fsh, tg, tsh, f"a.{pick[1]}", f"b.{tm}", 0))
        fq = f"{self.ns(fg)}::{fcpp}"
        tq = f"{self.ns(tg)}::{tcpp}"
        body = [f"void {fn}(const {fq}& a, {tq}& b) {{", "    (void)a; (void)b;"]
        body.extend("    " + l for l in lines)
        body.append("}")
        self.bodies[fn] = (f"void {fn}(const {fq}& a, {tq}& b);", body)

    def flush(self):
        i = 0
        while i < len(self.order):
            self.build_struct(*self.order[i])
            i += 1

    def var(self):
        self.counter += 1
        return f"t{self.counter}"

    def conv(self, fg, f, tg, t, src, dst, depth):
        if depth > 24 or f.kind in ("void", "unknown") or t.kind in ("void", "unknown"):
            return []
        dtype = f"std::remove_cvref_t<decltype({dst})>"
        if f.kind == "prim" and t.kind == "prim":
            special = {"varint128", "uuid", "restBuffer"}
            if f.args in special or t.args in special:
                return [f"{dst} = {src};"] if f.cpp == t.cpp else []
            return [f"{dst} = static_cast<{dtype}>({src});"]
        if f.kind in ("prim", "enum", "flags") and t.kind in ("prim", "enum", "flags") and f.kind != t.kind:
            if any(c.startswith("std::") or c == "Uuid" for c in (f.cpp, t.cpp)):
                return []
            return [f"{dst} = static_cast<{dtype}>({src});"]
        if f.kind == "enum" and t.kind == "enum":
            fe = fg.enums.get(f.cpp)
            te = tg.enums.get(t.cpp)
            if fe is None or te is None:
                return [f"{dst} = static_cast<{dtype}>({src});"]
            fmap = {label: (ident, number) for ident, number, label in fe.extra}
            tmap = {label: (ident, number) for ident, number, label in te.extra}
            if all(label in tmap and tmap[label][1] == fmap[label][1] for label in fmap):
                return [f"{dst} = static_cast<{dtype}>({src});"]
            fq = f"{self.ns(fg)}::{f.cpp}"
            tq = f"{self.ns(tg)}::{t.cpp}"
            lines = [f"switch ({src}) {{"]
            for label, (ident, number) in fmap.items():
                if label in tmap:
                    lines.append(f"case {fq}::{ident}: {dst} = {tq}::{tmap[label][0]}; break;")
            lines.append(f"default: {dst} = static_cast<{tq}>(static_cast<std::underlying_type_t<{tq}>>({src})); break;")
            lines.append("}")
            return lines
        if f.kind == "flags" and t.kind == "flags":
            fm = flag_masks(f.args)
            tm = flag_masks(t.args)
            if fm == tm and f.cpp == t.cpp:
                return [f"{dst} = {src};"]
            lines = [f"{dst} = {{}};"]
            for name, mask in fm.items():
                if name in tm:
                    lines.append(f"if (TestMask({src}, {mask_literal(mask)})) SetMask({dst}, {mask_literal(tm[name])});")
            return lines
        if f.kind == "flags" and t.kind == "array":
            el = shape(tg, t.args["type"])
            if el.kind != "enum":
                return []
            te = tg.enums.get(el.cpp)
            if te is None:
                return []
            tq = f"{self.ns(tg)}::{el.cpp}"
            fm = flag_masks(f.args)
            lines = [f"{dst}.clear();"]
            for ident, number, label in te.extra:
                if label in fm:
                    lines.append(f"if (TestMask({src}, {mask_literal(fm[label])})) {dst}.push_back({tq}::{ident});")
            return lines
        if f.kind == "array" and t.kind == "flags":
            el = shape(fg, f.args["type"])
            if el.kind != "enum":
                return []
            fe = fg.enums.get(el.cpp)
            if fe is None:
                return []
            fq = f"{self.ns(fg)}::{el.cpp}"
            tm = flag_masks(t.args)
            v = self.var()
            lines = [f"{dst} = {{}};", f"for (const auto& {v} : {src}) {{", f"    switch ({v}) {{"]
            for ident, number, label in fe.extra:
                if label in tm:
                    lines.append(f"    case {fq}::{ident}: SetMask({dst}, {mask_literal(tm[label])}); break;")
            lines += ["    default: break;", "    }", "}"]
            return lines
        if f.kind == "struct" and t.kind == "struct":
            fn = self.struct_fn(fg, f.cpp, tg, t.cpp)
            return [f"{fn}({src}, {dst});"]
        if f.kind == t.kind and f.kind in ("string", "bytes", "nbt", "nbtloop"):
            return [f"{dst} = {src};"]
        if f.kind == "string" and t.kind == "bytes" or f.kind == "bytes" and t.kind == "string":
            return [f"{dst}.assign({src}.begin(), {src}.end());"]
        if f.kind == "array" and t.kind == "array":
            fe = shape(fg, f.args["type"])
            te = shape(tg, t.args["type"])
            i = self.var()
            inner = self.conv(fg, fe, tg, te, f"{src}[{i}]", f"{dst}[{i}]", depth + 1)
            if t.expr == "fixed":
                lines = [f"for (size_t {i} = 0; {i} < {dst}.size() && {i} < {src}.size(); {i}++) {{"]
            else:
                lines = [f"{dst}.clear();", f"{dst}.resize({src}.size());", f"for (size_t {i} = 0; {i} < {src}.size(); {i}++) {{"]
            lines.extend("    " + l for l in inner)
            lines.append("}")
            return lines
        if f.kind == "option" and t.kind == "option":
            inner = self.conv(fg, shape(fg, f.args), tg, shape(tg, t.args), f"(*{src})", f"(*{dst})", depth + 1)
            return [f"if ({src}.has_value()) {{", f"    {dst}.emplace();"] + ["    " + l for l in inner] + ["} else {", f"    {dst}.reset();", "}"]
        if f.kind == "option":
            inner = self.conv(fg, shape(fg, f.args), tg, t, f"(*{src})", dst, depth + 1)
            return [f"if ({src}.has_value()) {{"] + ["    " + l for l in inner] + ["}"]
        if t.kind == "option":
            inner = self.conv(fg, f, tg, shape(tg, t.args), src, f"(*{dst})", depth + 1)
            return [f"{dst}.emplace();"] + inner
        if f.kind == "switch" or t.kind == "switch":
            return self.conv_switch(fg, f, tg, t, src, dst, depth)
        return []

    def alternatives(self, gen, sh):
        if sh.kind != "switch":
            return [(1, sh, {"__any"})]
        groups = switch_groups(sh.args, gen.is_void)
        return [(i + 1, shape(gen, body), keys) for i, (body, keys) in enumerate(groups)]

    def conv_switch(self, fg, f, tg, t, src, dst, depth):
        falts = self.alternatives(fg, f)
        talts = self.alternatives(tg, t)
        fsingle = f.kind != "switch" or len(falts) == 1
        tsingle = t.kind != "switch" or len(talts) == 1
        lines = []
        for fi, fsh, fkeys in falts:
            match = None
            for ti, tsh, tkeys in talts:
                if fkeys & tkeys or "__any" in fkeys or "__any" in tkeys:
                    match = (ti, tsh)
                    break
            if match is None and len(talts) == 1:
                match = (talts[0][0], talts[0][1])
            if match is None:
                continue
            ti, tsh = match
            if f.kind != "switch":
                fval, test = src, None
            elif fsingle:
                fval, test = f"(*{src})", f"{src}.has_value()"
            else:
                fval, test = f"std::get<{fi}>({src})", f"{src}.index() == {fi}"
            v = self.var()
            if t.kind != "switch":
                emplace, target = None, dst
            elif tsingle:
                emplace, target = f"auto& {v} = {dst}.emplace();", v
            else:
                emplace, target = f"auto& {v} = {dst}.template emplace<{ti}>();", v
            inner = self.conv(fg, fsh, tg, tsh, fval, target, depth + 1)
            body = ([emplace] if emplace else []) + inner
            if test:
                lines.append(f"if ({test}) {{")
                lines.extend("    " + l for l in body)
                lines.append("}")
            else:
                lines.extend(body)
        return lines

def emit_namespace(g, namespace, prefix, out_dir, parts, alias, bridged=set(), extra_decls=None, json_and_roundtrip=True):
    gen = g.gen
    registry = g.registry
    packet_decls = g.packet_decls
    os.makedirs(out_dir, exist_ok=True)
    hdr = ["#pragma once", '#include "network/Stream.hpp"', "", "#include <array>", "#include <cstdint>", "#include <optional>",
           "#include <string>", "#include <variant>", "#include <vector>", "", "namespace perlin::" + namespace + " {", "",
           "using net::Nbt;", "using net::ReadStream;", "using net::Uuid;", "using net::WriteStream;", ""]
    for slot in sorted(gen.slots):
        hdr.append(f"inline thread_local int64_t {slot} = 0;")
    if gen.slots:
        hdr.append("")
    type_decls = order_decls([d for d in gen.decls if d.packet is None])
    defer_incomplete(type_decls)
    forward = sorted(d.name for d in type_decls if d.kind == "struct")
    if forward:
        hdr.extend(f"struct {n};" for n in forward)
        hdr.append("")
    for decl in type_decls:
        hdr.extend(decl.lines)
        if decl.kind == "struct":
            hdr.append(f"void Write(WriteStream&, const {decl.name}&);")
            hdr.append(f"bool Read(ReadStream&, {decl.name}&);")
        hdr.append("")
    hdr.append("}")
    if alias:
        hdr += ["", "namespace perlin {", f"namespace proto = {namespace};", "}"]
    write_if_changed(os.path.join(out_dir, "Types.hpp"), "\n".join(hdr) + "\n")

    pk = ["#pragma once", f'#include "{prefix}/Types.hpp"', ""]
    for number, name, struct, mode, info in registry:
        if mode == "custom":
            pk.append(f'#include "{info}"')
    pk += ["", "namespace perlin::" + namespace + " {", ""]
    by_name = {d.name: d for d in packet_decls}
    for number, name, struct, mode, info in registry:
        if mode == "custom":
            continue
        decl = by_name[struct]
        lines = list(decl.lines)
        lines.insert(len(lines) - 1, f"    static constexpr uint32_t kId = {number};")
        lines.insert(len(lines) - 1, f'    static constexpr const char* kName = "{name}";')
        pk.extend(lines)
        pk.append(f"void Write(WriteStream&, const {struct}&);")
        pk.append(f"bool Read(ReadStream&, {struct}&);")
        if name in bridged:
            pk.append(f"bool BridgeWrite(WriteStream&, const {struct}&);")
            pk.append(f"int BridgeRead(ReadStream&, {struct}&);")
        pk.append("")
    pk.append("struct PacketInfo {")
    pk.append("    uint32_t id;")
    pk.append("    const char* name;")
    pk.append("};")
    pk.append("inline constexpr PacketInfo kPackets[] = {")
    for number, name, struct, mode, info in registry:
        pk.append(f'    {{{number}, "{name}"}},')
    pk.append("};")
    pk.append("const char* PacketName(uint32_t id);")
    if json_and_roundtrip:
        pk.append("bool PacketJson(uint32_t id, const uint8_t* data, size_t size, std::string& out);")
    pk.append("}")
    write_if_changed(os.path.join(out_dir, "Packets.hpp"), "\n".join(pk) + "\n")

    all_codec = [d for d in gen.decls if d.kind == "struct"]
    groups = [[] for _ in range(parts)]
    weights = [0] * parts
    for decl in all_codec:
        if decl.packet is not None and decl.name not in by_name:
            continue
        i = weights.index(min(weights))
        groups[i].append(decl)
        weights[i] += len(decl.codec) + 3
    for i, group in enumerate(groups):
        src = [f'#include "{prefix}/Packets.hpp"', "", "namespace perlin::" + namespace + " {", "namespace {", "using net::Transfer;", "using net::Frame;",
               "using net::Pick;", "using net::PickAt;", "using net::Present;", "using net::PresentIfRemaining;", "}", ""]
        for decl in group:
            src.extend(emit_codec(decl, decl.packet is not None and decl.packet[1] in bridged))
            src.append("")
        if i == 0:
            src.append("const char* PacketName(uint32_t id) {")
            src.append("    switch (id) {")
            for number, name, struct, mode, info in registry:
                src.append(f'    case {number}: return "{name}";')
            src.append('    default: return "unknown";')
            src.append("    }")
            src.append("}")
        src.append("}")
        write_if_changed(os.path.join(out_dir, f"Codec{i}.cpp"), "\n".join(src) + "\n")

    if not json_and_roundtrip:
        return type_decls

    rt = [f'#include "{prefix}/Packets.hpp"', "", "#include <string>", "", "namespace perlin::" + namespace + " {", "namespace {",
          "template <class P> int Check(std::string& failures, int& skipped) {",
          "    P value{};", "    std::vector<uint8_t> first;", "    net::WriteStream out(first);", "    Write(out, value);",
          "    if (!out.ok) {", "        skipped++;", "        return 0;", "    }",
          "    P back{};", "    net::ReadStream in(first.data(), first.size());",
          "    if (!Read(in, back) || in.remaining() != 0) {", '        failures += std::string(P::kName) + " (read)\\n";', "        return 1;", "    }",
          "    std::vector<uint8_t> second;", "    net::WriteStream again(second);", "    Write(again, back);",
          "    if (first != second) {", '        failures += std::string(P::kName) + " (mismatch)\\n";', "        return 1;", "    }",
          "    return 0;", "}", "}", "",
          "int RoundTripAll(std::string& failures, int& skipped) {", "    int bad = 0;"]
    for number, name, struct, mode, info in registry:
        if mode == "generated":
            rt.append(f"    bad += Check<{struct}>(failures, skipped);")
    rt += ["    return bad;", "}", "}"]
    write_if_changed(os.path.join(out_dir, "RoundTrip.cpp"), "\n".join(rt) + "\n")

    pj = [f'#include "{prefix}/Packets.hpp"', "", "#include <glaze/glaze.hpp>", "", "#include <string>", "",
          "namespace perlin::" + namespace + " {", "namespace {",
          "template <class P> bool Json(const uint8_t* data, size_t size, std::string& out) {",
          "    P packet{};", "    net::ReadStream in(data, size);",
          "    if (!Read(in, packet) || !in.ok)", "        return false;",
          "    return !glz::write<glz::opts{.prettify = false}>(packet, out);", "}", "}", "",
          "bool PacketJson(uint32_t id, const uint8_t* data, size_t size, std::string& out) {", "    switch (id) {"]
    no_json = {"ClientboundDataStorePacket"}
    for number, name, struct, mode, info in registry:
        if mode == "generated" and struct not in no_json:
            pj.append(f"    case {number}: return Json<{struct}>(data, size, out);")
    pj += ["    default: return false;", "    }", "}", "}"]
    write_if_changed(os.path.join(out_dir, "PacketJson.cpp"), "\n".join(pj) + "\n")
    return type_decls

BRIDGE_SUPPORT = """namespace {
inline bool TestMask(uint64_t v, uint64_t lo, uint64_t) { return (v & lo) != 0; }
inline bool TestMask(uint32_t v, uint64_t lo, uint64_t) { return (v & lo) != 0; }
inline bool TestMask(uint16_t v, uint64_t lo, uint64_t) { return (v & lo) != 0; }
inline bool TestMask(uint8_t v, uint64_t lo, uint64_t) { return (v & lo) != 0; }
inline bool TestMask(int64_t v, uint64_t lo, uint64_t) { return (static_cast<uint64_t>(v) & lo) != 0; }
inline bool TestMask(int32_t v, uint64_t lo, uint64_t) { return (static_cast<uint64_t>(static_cast<uint32_t>(v)) & lo) != 0; }
inline bool TestMask(const std::array<uint64_t, 2>& v, uint64_t lo, uint64_t hi) { return (v[0] & lo) != 0 || (v[1] & hi) != 0; }
template <class T> inline void SetMask(T& v, uint64_t lo, uint64_t) { v = static_cast<T>(static_cast<uint64_t>(v) | lo); }
inline void SetMask(std::array<uint64_t, 2>& v, uint64_t lo, uint64_t hi) { v[0] |= lo; v[1] |= hi; }
}
"""

def emit_bridge(bridge, cur, legacy, version, cur_ns, old_ns, out_dir, packets):
    entry = []
    for name in packets:
        nd = cur.by_packet[name]
        od = legacy.by_packet[name]
        up = bridge.struct_fn(legacy.gen, od.name, cur.gen, nd.name)
        down = bridge.struct_fn(cur.gen, nd.name, legacy.gen, od.name)
        entry.append(f"bool ReadInto(ReadStream& s, {cur_ns}::{nd.name}& v) {{ {old_ns}::{od.name} o; const bool ok = Read(s, o); {up}(o, v); return ok; }}")
        entry.append(f"void WriteFrom(WriteStream& s, const {cur_ns}::{nd.name}& v) {{ {old_ns}::{od.name} o; {down}(v, o); Write(s, o); }}")
    bridge.flush()
    src = ['#include "protocol/Packets.hpp"', f'#include "protocol/v{version}/Packets.hpp"', '#include "protocol/Bridges.hpp"', "",
           "#include <type_traits>", "", f"namespace perlin::bridge{version} {{", "using net::ReadStream;", "using net::WriteStream;", BRIDGE_SUPPORT]
    src.append("namespace {")
    for fn, (decl, body) in bridge.bodies.items():
        src.append(decl)
    src.append("")
    for fn, (decl, body) in bridge.bodies.items():
        src.extend(body)
        src.append("")
    src.append("}")
    src.extend(entry)
    src.append("}")
    write_if_changed(os.path.join(out_dir, f"Bridge{version}.cpp"), "\n".join(src) + "\n")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spec", required=True)
    ap.add_argument("--overrides")
    ap.add_argument("--out", required=True)
    ap.add_argument("--parts", type=int, default=6)
    ap.add_argument("--legacy-parts", type=int, default=2)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--namespace", default="proto2225")
    ap.add_argument("--version", type=int, default=2225)
    ap.add_argument("--legacy", action="append", default=[], help="protocol=spec.json")
    ap.add_argument("--no-current-alias", action="store_true")
    opts = ap.parse_args()

    spec = json.load(open(opts.spec))
    overrides = json.load(open(opts.overrides)) if opts.overrides else {}
    cur = generate(spec, overrides)

    legacies = []
    for item in opts.legacy:
        version, path = item.split("=", 1)
        version = int(version)
        lspec = json.load(open(path))
        legacies.append((version, lspec, generate(lspec, legacy_overrides(lspec, overrides))))
    legacies.sort(key=lambda x: x[0])

    cur_gen_types = cur.gen.types
    cmemo = {}
    differing = {}
    bridged = set()
    for version, lspec, lg in legacies:
        lmemo = {}
        names = []
        for number, name, struct, mode, info in cur.registry:
            if mode != "generated" or lg.mode.get(name) != "generated":
                continue
            a = expand(cur_gen_types, cur.by_packet[name].ptype, cmemo, frozenset())
            b = expand(lg.gen.types, lg.by_packet[name].ptype, lmemo, frozenset())
            if json.dumps(a, sort_keys=True) != json.dumps(b, sort_keys=True):
                names.append(name)
        differing[version] = names
        bridged.update(names)

    emit_namespace(cur, opts.namespace, "protocol", opts.out, opts.parts, not opts.no_current_alias, bridged)
    for version, lspec, lg in legacies:
        emit_namespace(lg, f"proto{version}", f"protocol/v{version}", os.path.join(opts.out, f"v{version}"),
                       opts.legacy_parts, False, set(), None, False)
        bridge = Bridge(lg.gen, cur.gen, f"perlin::proto{version}", f"perlin::{opts.namespace}")
        emit_bridge(bridge, cur, lg, version, f"perlin::{opts.namespace}", f"perlin::proto{version}", opts.out, differing[version])

    bh = ["#pragma once", '#include "network/Stream.hpp"', "", "namespace perlin {"]
    for number, name, struct, mode, info in cur.registry:
        if name in bridged:
            bh.append(f"namespace {opts.namespace} {{ struct {struct}; }}")
    for version, lspec, lg in legacies:
        bh.append(f"namespace bridge{version} {{")
        for name in differing[version]:
            struct = cur.by_packet[name].name
            bh.append(f"bool ReadInto(net::ReadStream&, {opts.namespace}::{struct}&);")
            bh.append(f"void WriteFrom(net::WriteStream&, const {opts.namespace}::{struct}&);")
        bh.append("}")
    bh.append("}")
    write_if_changed(os.path.join(opts.out, "Bridges.hpp"), "\n".join(bh) + "\n")

    versions = [v for v, _, _ in legacies]
    disp = ['#include "protocol/Packets.hpp"', '#include "protocol/Bridges.hpp"', "", f"namespace perlin::{opts.namespace} {{", "namespace {",
            "int Band() {", "    const int p = net::context::Protocol();", f"    if (p <= 0 || p >= {opts.version})", "        return 0;"]
    for v in reversed(versions[1:]):
        disp.append(f"    if (p >= {v})")
        disp.append(f"        return {v};")
    if versions:
        disp.append(f"    return {versions[0]};")
    else:
        disp.append("    return 0;")
    disp += ["}", "}", ""]
    for number, name, struct, mode, info in cur.registry:
        if name not in bridged:
            continue
        rd = [f"int BridgeRead(ReadStream& s, {struct}& v) {{", "    switch (Band()) {"]
        wr = [f"bool BridgeWrite(WriteStream& s, const {struct}& v) {{", "    switch (Band()) {"]
        for version in versions:
            if name in differing[version]:
                rd.append(f"    case {version}: return bridge{version}::ReadInto(s, v) ? 1 : 0;")
                wr.append(f"    case {version}: bridge{version}::WriteFrom(s, v); return true;")
        rd += ["    default: return -1;", "    }", "}"]
        wr += ["    default: return false;", "    }", "}"]
        disp.extend(rd)
        disp.extend(wr)
        disp.append("")
    disp.append("}")
    write_if_changed(os.path.join(opts.out, "Bridge.cpp"), "\n".join(disp) + "\n")

    report = [f"{len(cur.registry)} packets"]
    for version, lspec, lg in legacies:
        report.append(f"protocol {version}: {len(differing[version])} packets bridged, {len(lg.opaque)} opaque")
    if cur.opaque:
        report.append(f"{len(cur.opaque)} opaque packets:")
        report.extend(f"  {n}: {why}" for n, why in cur.opaque)
    sys.stdout.write("\n".join(report) + "\n")

CODEC_PATCHES = [(
    "static void Serialize(S& s, ItemV4& v) {",
    "    if constexpr (S::kReading) v.extra = {};\n    if (v.networkId == net::context::ShieldItemID()) {",
    "    if constexpr (S::kReading) v.extra = {};\n"
    "    if constexpr (!S::kReading) { if (v.networkId == 0) { uint32_t zero = 0; s.varint(zero); return; } }\n"
    "    if constexpr (!S::kReading) { if (std::holds_alternative<std::monostate>(v.extra)) { if (v.networkId == net::context::ShieldItemID()) v.extra.template emplace<1>(); else v.extra.template emplace<2>(); } }\n"
    "    if (v.networkId == net::context::ShieldItemID()) {",
)]

def write_if_changed(path, text):
    for anchor, old, new in CODEC_PATCHES:
        i = text.find(anchor)
        if i >= 0:
            j = text.find(old, i)
            if j >= 0:
                text = text[:j] + new + text[j + len(old):]
    if os.path.exists(path) and open(path).read() == text:
        return
    with open(path, "w", newline="\n") as f:
        f.write(text)

if __name__ == "__main__":
    try:
        main()
    except GenError as e:
        sys.stderr.write(f"protogen: {e}\n")
        sys.exit(1)
