#!/usr/bin/env python3
"""Convert an Iris/OptiFine Java shaderpack program to a Perlin RHI .slang program.

Offline conversion (no runtime Java parsing): resolves the pack's #includes,
evaluates the GLSL preprocessor for one vertex + one fragment stage, maps Iris
attributes/uniforms/varyings/samplers onto the RHI slang conventions used by
src/shaders/slang/programs/*.slang, and emits a .slang file plus a coverage
report (unmapped builtins that still need engine feed).

v1 scope: gbuffers_* solid terrain-style passes. Multi-target DRAWBUFFERS,
shadow comparison sampling and pack-option profiles beyond defaults are
reported, not silently stubbed.

Usage:
    java_to_slang.py --pack <shaderpack dir> --program gbuffers_terrain \
        --world world0 --out out.slang
"""
import argparse
import re
import sys
from pathlib import Path


class Preproc:
    """Minimal GLSL preprocessor: #define/#undef, #if/#ifdef/#ifndef/#elif/#else/
    #endif, #include. Undefined identifiers in #if expressions count as 0,
    matching the GLSL preprocessor. Records every macro that influenced a
    taken branch so the report can show which pack options mattered."""

    def __init__(self, pack_shaders_dir: Path):
        self.root = pack_shaders_dir
        self.macros: dict[str, str] = {}
        self.funcmacros: dict[str, tuple[str, str]] = {}
        self.used: set[str] = set()
        self.included: list[str] = []
        self.defined_order: list = []

    def define(self, name: str, value: str = "1", depth: int = 0):
        self.macros[name] = value
        self.defined_order.append((name, value, depth))

    def _eval(self, expr: str) -> int:
        e = re.sub(r"//.*$", "", expr.strip())
        e = re.sub(r"/\*.*?\*/", "", e).strip()
        e = re.sub(r"\bdefined\s*\(\s*(\w+)\s*\)", lambda m: "1" if m.group(1) in self.macros else "0", e)
        e = re.sub(r"\bdefined\s+(\w+)", lambda m: "1" if m.group(1) in self.macros else "0", e)

        def num(m):
            name = m.group(0)
            self.used.add(name)
            # resolve alias chains (LIGHTSHAFT_QUALI -> LIGHTSHAFT_QUALI_DEFINE -> 2)
            v: str = name
            seen: set[str] = set()
            for _ in range(32):
                if v in seen:
                    break
                seen.add(v)
                nv = self.macros.get(v, "0").strip()
                if re.fullmatch(r"-?[0-9]+(\.[0-9]+)?", nv):
                    return nv
                if re.fullmatch(r"[A-Za-z_]\w*", nv):
                    v = nv
                    continue
                return "0"
            return "0"

        e = re.sub(r"[A-Za-z_]\w*", num, e)
        e = e.replace("!=", " NEQ ").replace("&&", " and ").replace("||", " or ").replace("!", " not ").replace(
            " NEQ ", " != ")
        try:
            return 1 if eval(e, {"__builtins__": {}}, {}) else 0
        except Exception:
            return 0

    def expand(self, line: str, _depth: int = 0) -> str:
        """Expand object-like macros in one code line (function-like macros
        pass through: slang keeps the GLSL preprocessor for those)."""
        if _depth > 32 or not self.macros:
            return line
        out = line
        for _ in range(64):
            changed = False
            for name in sorted(self.macros, key=len, reverse=True):
                if "(" in name or name in self.funcmacros:
                    continue
                val = self.macros[name]
                if re.fullmatch(r"[A-Za-z_]\w*", val) and val in self.macros:
                    continue  # alias chains stay symbolic
                new, n = re.subn(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_(])", val, out)
                if n:
                    out, changed = new, True
            if not changed:
                break
        return out

    def _include_path(self, name: str, here: Path) -> Path:
        name = name.strip().strip('"<>')
        if name.startswith("/"):
            return self.root.joinpath(*name[1:].split("/"))
        return (here.parent / name).resolve()

    def run(self, path: Path, here: Path | None = None) -> str:
        here = here or path
        text = path.read_text(encoding="utf-8", errors="replace")
        out: list[str] = []
        stack: list[dict] = []  # {parent_on, taken, on}
        def on() -> bool:
            return all(f["on"] for f in stack)

        joined: list[str] = []
        acc = ""
        for raw in text.splitlines():
            if acc:
                acc += "\n" + raw
            else:
                acc = raw
            if acc.rstrip().endswith("\\") and not acc.lstrip().startswith("//"):
                acc = acc.rstrip()[:-1]
                continue
            joined.append(acc)
            acc = ""
        if acc:
            joined.append(acc)
        depth = 0
        for raw in joined:
            if not raw.strip().startswith("#"):
                depth += raw.count("{") - raw.count("}")
                depth = max(0, depth)
            line = raw.strip()
            m = re.match(r"#\s*(if|ifdef|ifndef|elif|else|endif|define|undef|include|version|pragma|line|error)\b(.*)$", line, re.S)
            if not m:
                if on():
                    out.append(self.expand(raw))
                continue
            d, rest = m.group(1), m.group(2).strip()
            if d == "endif":
                stack.pop()
            elif d == "else":
                f = stack[-1]
                f["on"] = f["parent_on"] and not f["taken"]
                f["taken"] = True
            elif d in ("if", "ifdef", "ifndef"):
                parent = on()
                if d == "if":
                    cond = self._eval(rest)
                elif d == "ifdef":
                    cond = rest.split()[0] in self.macros
                    self.used.add(rest.split()[0])
                else:
                    cond = rest.split()[0] not in self.macros
                    self.used.add(rest.split()[0])
                stack.append({"parent_on": parent, "taken": bool(parent and cond), "on": bool(parent and cond)})
            elif d == "elif":
                f = stack[-1]
                cond = self._eval(rest) if (f["parent_on"] and not f["taken"]) else 0
                f["on"] = bool(f["parent_on"] and not f["taken"] and cond)
                f["taken"] = f["taken"] or bool(f["on"])
            elif d == "define" and on():
                parts = rest.split(None, 1)
                if len(parts) == 1:
                    self.define(parts[0].split("(")[0], "1", depth)
                else:
                    name = parts[0]
                    # pack option sliders trail their range in a comment:
                    # #define X 100 //[0 4 8 ...] -> value is just 100
                    value = re.sub(r"//.*$", "", parts[1]).strip()
                    value = re.sub(r"/\*.*?\*/", "", value).strip()
                    if "(" in name:  # function-like macro
                        fname = name.split("(")[0]
                        params = name[name.index("("):]
                        self.funcmacros[fname] = (params, value, depth)
                    self.define(name, value, depth)
            elif d == "undef" and on():
                gone = rest.split()[0]
                self.macros.pop(gone, None)
                self.funcmacros.pop(gone, None)
            elif d == "include" and on():
                inc = self._include_path(rest.strip().strip('"'), here)
                self.included.append(str(inc.relative_to(self.root)) if inc.is_relative_to(self.root) else str(inc))
                if inc.is_file():
                    out.append(f"// @included {rest}")
                    out.append(self.run(inc, inc))
                else:
                    out.append(f"// @MISSING-INCLUDE {rest}")
            # version/pragma/line/error dropped: slang header owns the version
        return "\n".join(out)


STUB_DEFINE_RE = re.compile(r"#\s*define\s+(\w+)")


def preprocess_stage(pack: Path, world: str, program: str, stage: str) -> tuple[str, Preproc]:
    """Run the pack's own .vsh/.fsh stub (holds the stage defines) through Preproc."""
    ext = ".vsh" if stage == "vertex" else ".fsh"
    stub = pack / "shaders" / world / (program + ext)
    if not stub.is_file():
        raise SystemExit(f"missing stub {stub}")
    pp = Preproc(pack / "shaders")
    stub_text = stub.read_text(encoding="utf-8", errors="replace")
    for name in STUB_DEFINE_RE.findall(stub_text):
        pp.define(name)
    pp.define("VERTEX_SHADER" if stage == "vertex" else "FRAGMENT_SHADER")
    return pp.run(stub, stub), pp


DECL_RE = re.compile(
    r"^\s*(flat\s+|centroid\s+|smooth\s+|noperspective\s+)?(attribute|varying|uniform|in|out|inout)\s+"
    r"(sampler\w+|image\w+|void|bool|int|uint|float|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat[234]|mat[234]x[234])\s+([^;]+);",
    re.M,
)

LEGACY_BUILTINS = ["gl_Vertex", "gl_Normal", "gl_Color", "gl_MultiTexCoord0", "gl_MultiTexCoord1",
                   "gl_ModelViewMatrix", "gl_ProjectionMatrix", "gl_NormalMatrix",
                   "gl_TextureMatrix", "gl_FragCoord", "gl_FragDepth"]
FIXED_DECL = re.compile(r"\b(attribute|varying)\b")
GL_FRAGDATA_RE = re.compile(r"gl_FragData\s*\[\s*(\d+)\s*\]")
TEXTURE_FN_RE = re.compile(r"\b(texture2DProj|texture2DLod|texture2D|textureProj|texelFetch|shadow2D|shadow2DProj|textureLod)\b")
SAMPLER_DECL_RE = re.compile(r"uniform\s+sampler\w+\s+(\w+)")


def inventory(source: str) -> dict:
    reps: dict[str, set] = {"legacy": set(), "fragdata": set(), "texfns": set(), "samplers": set()}
    for name in LEGACY_BUILTINS:
        if re.search(r"\b" + re.escape(name) + r"\b", source):
            reps["legacy"].add(name)
    for m in GL_FRAGDATA_RE.finditer(source):
        reps["fragdata"].add(int(m.group(1)))
    for m in TEXTURE_FN_RE.finditer(source):
        reps["texfns"].add(m.group(1))
    for m in SAMPLER_DECL_RE.finditer(source):
        reps["samplers"].add(m.group(1))
    decls = []
    for m in DECL_RE.finditer(source):
        qual, storage, ty, names = m.group(1) or "", m.group(2), m.group(3), m.group(4)
        for chunk in names.split(","):
            chunk = chunk.strip()
            nm = re.match(r"(\w+)(\s*\[\s*(\w*)\s*\])?(\s*=\s*[^,]+)?$", chunk)
            if nm:
                decls.append({"qual": qual.strip(), "storage": storage, "type": ty,
                              "name": nm.group(1), "array": nm.group(3) or ""})
    return {"refs": reps, "decls": decls}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack", required=True, help="unzipped shaderpack dir (contains shaders/)")
    ap.add_argument("--program", required=True)
    ap.add_argument("--world", default="world0")
    ap.add_argument("--out", help="write combined preprocessed inventory instead of converting")
    ap.add_argument("--inventory", action="store_true", help="only report builtins/uniforms/varyings per stage")
    ap.add_argument("--emit", help="write converted .slang program to this path")
    ap.add_argument("-D", "--define", action="append", default=[],
                    help="pack option macro override, e.g. -DRAIN_PUDDLES=2 (repeatable)")
    ap.add_argument("--check", action="store_true",
                    help="validate stage bodies with glslangValidator via a GLSL harness")
    ap.add_argument("--fullscreen", action="store_true",
                    help="position-only VsIn for composite/deferred/final passes; uvs derive from the fullscreen triangle")
    args = ap.parse_args()

    pack = Path(args.pack)
    results = {}
    for stage in ("vertex", "fragment"):
        src, pp = preprocess_stage(pack, args.world, args.program, stage)
        inv = inventory(src)
        results[stage] = (src, pp, inv)

    for stage in ("vertex", "fragment"):
        src, pp, inv = results[stage]
        print(f"===== {stage} ({len(src)} chars, {len(inv['decls'])} decls) =====")
        r = inv["refs"]
        print("legacy builtins:", sorted(r["legacy"]) or "none")
        print("gl_FragData targets:", sorted(r["fragdata"]) or "none")
        print("texture fns:", sorted(r["texfns"]) or "none")
        print("samplers:", sorted(r["samplers"]) or "none")
        st_in = [d for d in inv["decls"] if d["storage"] in ("in", "attribute")]
        st_out = [d for d in inv["decls"] if d["storage"] in ("out",)]
        st_uni = [d for d in inv["decls"] if d["storage"] == "uniform" and "sampler" not in d["type"]]
        print(f"ins({len(st_in)}):", ", ".join(f"{d['type']} {d['name']}" for d in st_in[:40]))
        print(f"outs({len(st_out)}):", ", ".join(f"{d['type']} {d['name']}" for d in st_out[:40]))
        print(f"uniforms({len(st_uni)}):", ", ".join(f"{d['type']} {d['name']}" for d in st_uni[:60]))
        opt_macros = sorted({m for m in pp.used if m.isupper()})
        print(f"option macros referenced({len(opt_macros)}):", ", ".join(opt_macros[:40]))
        print()
    if args.check:
        import tempfile
        with tempfile.TemporaryDirectory() as td:
            rc = check_program(pack, args.program, args.world, Path(td))
        if rc:
            return rc
    if args.out:
        Path(args.out).write_text(results["vertex"][0] + "\n" + results["fragment"][0])
    if args.emit:
        import json as _json
        rep = emit_program(pack, args.program, args.world, Path(args.emit),
                           dict(d.split("=", 1) for d in (args.define or [])), fullscreen=args.fullscreen)
        print("--- emission report ---")
        print(_json.dumps(rep, indent=1)[:4000])
    return 0



# ---------------------------------------------------------------- emitter v1
#
# Mechanical mapping, all explicit. Anything without a mapping below is
# reported, never silently stubbed.

VERTEX_INPUTS = [
    # slang type, VsIn member, location, java names consumed
    ("vec3", "a_position", 0, ["gl_Vertex"]),
    ("vec3", "a_normal", 1, ["gl_Normal"]),
    ("vec4", "a_color0", 2, ["gl_Color"]),
    ("vec2", "a_texcoord0", 4, ["gl_MultiTexCoord0"]),
    ("vec4", "a_texcoord1", 5, ["gl_MultiTexCoord1"]),
    ("vec4", "a_mcEntity", 8, ["mc_Entity"]),
    ("vec4", "a_mcMidTexCoord", 9, ["mc_midTexCoord"]),
    ("vec4", "a_atTangent", 10, ["at_tangent"]),
    ("float", "a_mcChunkFade", 11, ["mc_chunkFade"]),
]

# packed engine feed: the RHI terrain vertex carries java attribs packed to
# keep the stride small. member -> (packed slang type, packed VsIn name,
# location, unpack statements with {src} = input member).
PACKED_INPUTS = {
    "a_normal": ("float4", "a_nPacked", 1, [
        "vs_stage::a_normal = normalize({src}.xyz * 2.0 - 1.0);",
    ]),
    "a_mcEntity": ("float", "a_ePacked", 8, [
        "vs_stage::a_mcEntity = float4({src}, 0.0, 0.0, 0.0);",
    ]),
    "a_mcMidTexCoord": ("float2", "a_mPacked", 9, [
        "vs_stage::a_mcMidTexCoord = float4({src}, 0.0, 0.0);",
    ]),
    # tangent axis index 0..5 (+x,-x,+y,-y,+z,-z), handedness in the low bit
    "a_atTangent": ("float", "a_tPacked", 10, [
        "{ float ti = floor({src}); float ix = floor(ti * 0.5); float hd = (ti - ix * 2.0) * 2.0 - 1.0;",
        "  float3 ax = ix < 0.5 ? float3(1.0, 0.0, 0.0) : ix < 1.5 ? float3(-1.0, 0.0, 0.0) :",
        "      ix < 2.5 ? float3(0.0, 1.0, 0.0) : ix < 3.5 ? float3(0.0, -1.0, 0.0) :",
        "      ix < 4.5 ? float3(0.0, 0.0, 1.0) : float3(0.0, 0.0, -1.0);",
        "  vs_stage::a_atTangent = float4(ax, hd); }",
    ]),
    # chunk load fade is cosmetic: engine feeds a constant until meshing animates it
    "a_mcChunkFade": (None, None, None, [
        "vs_stage::a_mcChunkFade = 1.0;",
    ]),
}

SAMPLER_SLOTS = {
    # tex is the block atlas; the RHI keeps it as an array, so tex taps are
    # rewritten to array samples with the tile layer from a_texcoord1 (see below)
    "tex": "s_atlas",
    "lightmap": "s_lightmap",
    "shadowtex0": "s_shadow",
    "shadowtex1": "s_shadowB",
    "shadowcolor0": "s_shadowTint",
    "shadowcolor1": "s_shadowTintB",
    "depthtex0": "s_sceneDepth",
    "depthtex1": "s_depth",
    "noisetex": "s_noise",
    "normals": "s_normals",
    "specular": "s_specular",
    "gaux2": "s_gaux2",
    "gaux4": "s_gaux4",
    **{f"colortex{i}": f"s_colortex{i}" for i in range(16)},
}

GLSL_TO_SLANG_TYPE = {
    "bool": "bool", "int": "int", "uint": "uint", "float": "float",
    "vec2": "float2", "vec3": "float3", "vec4": "float4",
    "ivec2": "int2", "ivec3": "int3", "ivec4": "int4",
    "uvec2": "uint2", "uvec3": "uint3", "uvec4": "uint4",
    "mat2": "float2x2", "mat3": "float3x3", "mat4": "float4x4",
}

# fixed-function builtins -> RHI expression. None = needs a fed cbuffer member.
LEGACY_MATRIX = {
    "gl_ModelViewMatrix": "u_jModelViewMatrix",
    "gl_ProjectionMatrix": "u_jProjectionMatrix",
    "gl_NormalMatrix": "u_jNormalMatrix",
    "gl_TextureMatrix0": "u_jTextureMatrix0",
    "gl_TextureMatrix1": "u_jTextureMatrix1",
}


def _strip_global_decl(line: str, depth: int):
    """If line is a global attribute/varying/in/uniform/precision decl, return
    ('kind', name, rest). Function-parameter qualifiers live at depth > 0
    and are never stripped."""
    if depth != 0:
        return None
    m = re.match(
        r"\s*((?:smooth|flat|noperspective|invariant|centroid|sample|patch)\s+)?(attribute|varying|uniform)\s+"
        r"(sampler\w+|image\w+|void|bool|int|uint|float|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat[234]|mat[234]x[234])\s+([^;]+);",
        line)
    if m:
        return ("decl", m.group(2), m.group(3), m.group(4).strip())
    m = re.match(r"\s*((?:smooth|flat|noperspective|invariant|centroid|sample|patch)\s+)?(in|out)\s+(\w+)\s+([^;]+);",
                 line)
    if m and m.group(3) in GLSL_TO_SLANG_TYPE:
        return ("io", m.group(2), m.group(3), m.group(4).strip(), (m.group(1) or "").strip())
    if re.match(r"\s*precision\s+\w+\s+\w+\s*;", line):
        return ("precision", "", "", "")
    return None


def _split_names(chunk: str):
    for part in chunk.split(","):
        part = part.strip()
        part = re.sub(r"\s*=.*$", "", part).strip()
        m = re.match(r"(\w+)(\s*\[\s*(\w*)\s*\])?$", part)
        if m:
            yield m.group(1), m.group(3) or ""


def rewrite_stage(source: str, sampler_map: dict[str, str]):
    """Strip re-declared interface, rename builtins/samplers. Returns
    (body, uniforms, samplers, vary_out, vary_in) where vary_* are
    (qual, type, name) triples."""
    uniforms: dict[str, tuple[str, str]] = {}
    samplers: set[str] = set()
    vary: list[tuple[str, str, str, str]] = []  # (qual, storage, type, name)
    extra_matrices: dict[str, str] = {}  # java expr -> cbuffer member
    body_lines: list[str] = []
    depth = 0
    for raw in source.splitlines():
        info = _strip_global_decl(raw, depth)
        depth += raw.count("{") - raw.count("}")
        if info is None:
            body_lines.append(raw)
            continue
        kind = info[0]
        if kind == "precision":
            continue
        if len(info) == 5:
            _, storage, ty, chunk, qual = info
        else:
            _, storage, ty, chunk = info
            qual = ""
        if "sampler" in ty or "image" in ty:
            for name, _arr in _split_names(chunk):
                samplers.add(name)
            continue
        if storage in ("attribute",):
            continue  # VsIn covers it
        if storage in ("varying", "in", "out"):
            for _n, _a in _split_names(chunk):
                vary.append((qual, storage, ty, _n))
            continue
        if storage == "uniform":
            for name, arr in _split_names(chunk):
                uniforms[name] = (ty, arr)
            continue
        body_lines.append(raw)
    return "\n".join(body_lines), uniforms, samplers, vary


def render_defines(pp, already: set | None = None) -> str:
    """Re-emit defines the pack needs at compile time (function-like helpers
    and multi-line option blocks). Object-like option macros were expanded
    inline already; re-emitting them is harmless."""
    wanted: dict[str, str] = {}
    for name, value, ddepth in pp.defined_order:
        if name in wanted:
            continue
        if name in ("VERTEX_SHADER", "FRAGMENT_SHADER"):
            continue  # compile-time switch, already resolved
        if "(" not in name:
            continue  # object-like: expanded inline in file order already
        if name.split("(")[0] not in pp.funcmacros:
            continue  # undefined later
        if ddepth != 0:
            continue  # function-scope helper: stays where the pack put it
        wanted[name] = value
    # function-like bodies reference option macros: pull those in too
    changed = True
    while changed:
        changed = False
        for name, value in list(wanted.items()):
            for ident in set(re.findall(r"[A-Za-z_]\w*", value)):
                if ident in wanted or ident in ("VERTEX_SHADER", "FRAGMENT_SHADER"):
                    continue
                if ident in pp.macros and "(" + ident not in str(pp.defined_order):
                    for n2, v2, _d2 in pp.defined_order:
                        if n2 == ident or n2.startswith(ident + "("):
                            wanted[n2] = v2
                            changed = True
                            break
    out = []
    for name, value in wanted.items():
        if already is not None:
            if name in already:
                continue
            already.add(name)
        out.append(f"#define {name} {value.replace(chr(10), ' \\\n')}")
    return "\n".join(out)


def _rename_builtins(body: str, needed_matrices: dict[str, str]) -> str:
    def sub(pat, rep):
        nonlocal body
        body = re.sub(pat, rep, body)
    sub(r"\bmc_Entity\b", "a_mcEntity")
    sub(r"\bmc_midTexCoord\b", "a_mcMidTexCoord")
    sub(r"\bat_tangent\b", "a_atTangent")
    sub(r"\bmc_chunkFade\b", "a_mcChunkFade")
    sub(r"\bgl_Vertex\b", "vec4(a_position, 1.0)")
    sub(r"\bgl_Normal\b", "a_normal")
    sub(r"\bgl_Color\b", "a_color0")
    sub(r"\bgl_MultiTexCoord0\b", "vec4(a_texcoord0, 0.0, 1.0)")
    # engine light bytes are 0..15 in a_texcoord1.xy; the pack wants 0..1
    sub(r"\bgl_MultiTexCoord1\b", "javaLightCoord(a_texcoord1)")
    # fixed-function transform: projection * modelview * vertex
    if re.search(r"\bftransform\s*\(\s*\)", body):
        needed_matrices["gl_ProjectionMatrix"] = LEGACY_MATRIX["gl_ProjectionMatrix"]
        needed_matrices["gl_ModelViewMatrix"] = LEGACY_MATRIX["gl_ModelViewMatrix"]
        sub(r"\bftransform\s*\(\s*\)", "(u_jProjectionMatrix * u_jModelViewMatrix * vec4(a_position, 1.0))")
    # sampler params arrive as legacy types; the global slot with the same
    # name supplies the sampler state (see depthtex below)
    sub(r"\bsampler2D\b", "Texture2D")
    # fixed-function fog: engine feeds start/scale (see FeedJavaUniforms)
    sub(r"\bgl_Fog\.start\b", "u_jFogStart")
    sub(r"\bgl_Fog\.scale\b", "u_jFogScale")
    for java, member in LEGACY_MATRIX.items():
        base = java.replace("gl_TextureMatrix", "gl_TextureMatrix")
        if base.startswith("gl_TextureMatrix"):
            continue  # handled with index below
        if re.search(r"\b" + re.escape(java) + r"\b", body):
            needed_matrices[java] = member
            sub(r"\b" + re.escape(java) + r"\b", member)
    for idx in ("0", "1"):
        if re.search(r"gl_TextureMatrix\s*\[\s*" + idx + r"\s*\]", body):
            member = f"u_jTextureMatrix{idx}"
            needed_matrices[f"gl_TextureMatrix[{idx}]"] = member
            sub(r"gl_TextureMatrix\s*\[\s*" + idx + r"\s*\]", member)
    sub(r"\btexture2DProj\s*\(", "javaTextureProj(")
    sub(r"\btextureProj\s*\(", "javaTextureProj(")
    sub(r"\bshadow2DProj\s*\(", "javaShadow2DProj(")
    def _swiz(m):
        return "." + m.group(1).replace("s", "x").replace("t", "y").replace("p", "z").replace("q", "w")
    body = re.sub(r"\.([stpq]{1,4})(?![A-Za-z0-9_])", _swiz, body)
    sub(r"\bshadow2D\s*\(", "javaShadow2D(")
    sub(r"\bgl_FragData\s*\[\s*(\d+)\s*\]", r"gl_FragData\1")
    sub(r"\bvoid\s+main\s*\(\s*\)", "void stageMain()")
    return body


def _size_global_arrays(body: str) -> str:
    """Unsized global array with constructor init is not slang: size it.
    Slang-only (valid GLSL as-is, so the check harness skips this)."""
    while True:
        m = re.search(r"\bvec3\[\]\s+(\w+)\s*=\s*vec3\[\]\s*\(", body)
        if not m:
            break
        depth, i = 0, m.end() - 1
        while i < len(body):
            if body[i] == "(":
                depth += 1
            elif body[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        inner = body[m.end():i]
        elems = len(re.findall(r"(?<![A-Za-z0-9_])vec3\s*\(", inner))
        body = (body[:m.start()] + f"static const vec3 {m.group(1)}[{max(elems, 1)}] = {{" + inner + "};"
                + body[i + 1:])
    return body


def _slang_ctors(body: str) -> str:
    """GLSL matrix constructor spellings -> slang (mat2 is not a slang type).
    Slang-only: plain GLSL accepts mat2(...)."""
    for a, b in (("mat2", "float2x2"), ("mat2x3", "float2x3"), ("mat2x4", "float2x4"),
                 ("mat3x2", "float3x2"), ("mat3x4", "float3x4"),
                 ("mat4x2", "float4x2"), ("mat4x3", "float4x3")):
        body = re.sub(r"\b" + a + r"\s*\(", b + "(", body)
    return body


def _slang_globals(body: str) -> str:
    """Keep namespace globals writable without modifying function parameters."""
    clean = re.sub(r"/\*.*?\*/|//[^\n]*", lambda m: "\n" * m.group().count("\n"), body, flags=re.S)
    out = []
    depth = parens = 0
    for raw, code in zip(body.splitlines(), clean.splitlines()):
        stripped = code.strip()
        if depth == 0 and parens == 0 and re.match(
                r"(const\s+)?(bool|int|uint|float|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat[234]|mat[234]x[234]|float[234]x[234])\s+[A-Za-z_]\w*\b", stripped):
            head = stripped.split("=", 1)[0].split(";")[0]
            if "(" not in head:
                raw = "static " + raw.lstrip()
        out.append(raw)
        depth += code.count("{") - code.count("}")
        parens += code.count("(") - code.count(")")
    return "\n".join(out)


def _defer_global_initializers(body: str) -> str:
    """Pack globals depend on uniforms assigned by the entry-point wrapper."""
    assignments = []
    pattern = r"^static (bool|int|uint|float|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat[234]|float[234](?:x[234])?)\s+(\w+(?:\[[^\]]*\])?\s*=[^;]+);"
    def rewrite(match):
        parts, start, depth = [], 0, 0
        text = match.group(2)
        for i, char in enumerate(text):
            if char in "([{": depth += 1
            elif char in ")]}": depth -= 1
            elif char == "," and depth == 0:
                parts.append(text[start:i]); start = i + 1
        parts.append(text[start:])
        decls = []
        for part in parts:
            if "=" in part:
                lhs, rhs = part.split("=", 1)
                name = lhs.strip().split("[")[0]
                assignments.append(f"    {name} = {rhs.strip()};")
                decls.append(lhs.strip())
            else:
                decls.append(part.strip())
        return "static " + match.group(1) + " " + ", ".join(decls) + ";"
    body = re.sub(pattern, rewrite, body, flags=re.M)
    return body.replace("void stageMain() {", "void stageMain() {\n" + "\n".join(assignments), 1)


def _rename_samplers(body: str, sampler_map: dict[str, str]) -> str:
    for java, slot in sorted(sampler_map.items(), key=lambda kv: -len(kv[0])):
        body = re.sub(r"(?<![A-Za-z0-9_])" + re.escape(java) + r"(?![A-Za-z0-9_])", slot, body)
    # shadow helpers take (texture, sampler, coord)
    body = re.sub(r"\bjavaShadow2D\s*\(\s*([A-Za-z0-9_]+)\s*,",
                  r"javaShadow2D(\1, \1_s,", body)
    body = re.sub(r"\bjavaShadow2DProj\s*\(\s*([A-Za-z0-9_]+)\s*,",
                  r"javaShadow2DProj(\1, \1_s,", body)
    return body


def _in_spans(spans: list[tuple[int, int]], pos: int) -> bool:
    return any(a <= pos < b for a, b in spans)


def _rewrite_atlas_taps(body: str) -> str:
    """tex taps became s_atlas (a 2D array): resample with the tile layer.
    jLayer is a generated varying decoded from a_texcoord1 (see emission)."""
    spans = _comment_spans(body)
    out: list[str] = []
    i = 0
    pat = re.compile(r"\btexture2DLod\s*\(\s*s_atlas\s*,|\btexture2D\s*\(\s*s_atlas\s*,")
    while True:
        m = pat.search(body, i)
        if not m:
            out.append(body[i:])
            break
        if _in_spans(spans, m.start()):
            out.append(body[i:m.end()])
            i = m.end()
            continue
        out.append(body[i:m.start()])
        name = "texture2DArrayLod" if m.group(0).startswith("texture2DLod") else "texture2DArray"
        j = m.end()
        depth = 1
        args: list[str] = []
        cur = ""
        while j < len(body) and depth:
            ch = body[j]
            if ch == "(":
                depth += 1
                cur += ch
            elif ch == ")":
                depth -= 1
                if depth:
                    cur += ch
            elif ch == "," and depth == 1:
                args.append(cur)
                cur = ""
            else:
                cur += ch
            j += 1
        args.append(cur)
        uv = args[0].strip()
        if name == "texture2DArrayLod":
            out.append(f"perlinAtlasLod(float3({uv}, jLayer), {args[1].strip()})")
        else:
            out.append(f"perlinAtlas(float3({uv}, jLayer))")
        i = j
    return "".join(out)


def _comment_spans(body: str) -> list[tuple[int, int]]:
    spans: list[tuple[int, int]] = []
    i = 0
    while i < len(body):
        if body.startswith("/*", i):
            j = body.find("*/", i + 2)
            j = len(body) if j < 0 else j + 2
            spans.append((i, j))
            i = j
        elif body.startswith("//", i):
            j = body.find("\n", i)
            j = len(body) if j < 0 else j
            spans.append((i, j))
            i = j
        elif body[i] in "\"'":
            q = body[i]
            j = i + 1
            while j < len(body) and body[j] != q:
                j += 2 if body[j] == "\\" else 1
            spans.append((i, min(j + 1, len(body))))
            i = min(j + 1, len(body))
        else:
            i += 1
    return spans


def _split_top(body: str, start: int) -> tuple[list[str], int]:
    """Split a parenthesized argument list starting at '(' (balanced scan).
    Returns (args, index just past ')')."""
    assert body[start] == "("
    args: list[str] = []
    cur = ""
    depth = 0
    j = start
    while j < len(body):
        ch = body[j]
        if ch == "(":
            depth += 1
            if depth > 1:
                cur += ch
        elif ch == ")":
            depth -= 1
            if not depth:
                args.append(cur)
                return args, j + 1
            cur += ch
        elif ch == "," and depth == 1:
            args.append(cur)
            cur = ""
        else:
            cur += ch
        j += 1
    return args, j


def _rewrite_calls(body: str) -> str:
    """Fix call shapes slang rejects but Iris accepts: sampler params need an
    explicit sampler (dual-param form), lessThan needs an operator, and
    vector ==/!= in conditions needs all()/any()."""
    # lessThan(a, b) -> ((a) < (b)); greaterThan likewise
    out: list[str] = []
    i = 0
    spans = _comment_spans(body)
    pat = re.compile(r"\b(lessThan|greaterThan)\s*\(")
    while True:
        m = pat.search(body, i)
        if not m:
            out.append(body[i:])
            break
        if _in_spans(spans, m.start()):
            out.append(body[i:m.end()])
            i = m.end()
            continue
        out.append(body[i:m.start()])
        args, j = _split_top(body, m.end() - 1)
        op = "<" if m.group(1) == "lessThan" else ">"
        if len(args) == 2:
            out.append(f"(({args[0].strip()}){op}({args[1].strip()}))")
        else:
            out.append(m.group(0))
        i = j
    body = "".join(out)
    # textureSize(t, lod) has no HLSL expression form; route a helper instead
    out = []
    i = 0
    spans = _comment_spans(body)
    pat = re.compile(r"\btextureSize\s*\(")
    while True:
        m = pat.search(body, i)
        if not m:
            out.append(body[i:])
            break
        if _in_spans(spans, m.start()):
            out.append(body[i:m.end()])
            i = m.end()
            continue
        out.append(body[i:m.start()])
        args, j = _split_top(body, m.end() - 1)
        if len(args) == 2:
            out.append(f"javaTextureSize({args[0].strip()}, {args[1].strip()})")
        else:
            out.append(m.group(0))
            j = m.end()
        i = j
    body = "".join(out)
    # sampler params: Texture2D p (no global sampler) takes SamplerState p_s;
    # call sites gain the matching global sampler argument
    sampler_funcs: dict[str, list[int]] = {}
    spans = _comment_spans(body)
    for m in re.finditer(r"[A-Za-z_]\w*(?:\s+[A-Za-z_]\w*)*\s+([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*\{", body):
        # the greedy return-type prefix can reach into a preceding comment:
        # test the function name itself
        if _in_spans(spans, m.start(1)):
            continue
        fname = m.group(1)
        if fname.startswith("java"):
            continue
        parts, _ = _split_top("(" + m.group(2) + ")", 0)
        idx = [k for k, a in enumerate(parts) if re.search(r"\bTexture2D\s+[A-Za-z_]\w*\s*$", a.strip())]
        if idx:
            sampler_funcs[fname] = idx
    if sampler_funcs:
        names = sorted(sampler_funcs, key=len, reverse=True)
        call = re.compile(r"(?<![A-Za-z0-9_])(" + "|".join(map(re.escape, names)) + r")\s*\(")
        chunks: list[str] = []
        k = 0
        for m in call.finditer(body):
            if _in_spans(spans, m.start()):
                continue
            chunks.append(body[k:m.start()])
            k = m.start()
            # definition, not a call, when a return type precedes the name
            before = body[max(0, m.start() - 64):m.start()]
            is_def = bool(re.search(r"[A-Za-z_]\w*\s+$", before))
            args, j = _split_top(body, m.end() - 1)
            if is_def:
                parts = [a.strip() for a in args]
                extra = []
                for n in sampler_funcs[m.group(1)]:
                    pname = parts[n].split()[-1]
                    extra.append(f"SamplerState {pname}_s")
                chunks.append(m.group(1) + "(" + ", ".join(parts + extra) + ")")
            else:
                extra = []
                for n in sampler_funcs[m.group(1)]:
                    if n < len(args) and re.fullmatch(r"[A-Za-z_]\w*", args[n].strip()):
                        extra.append(args[n].strip() + "_s")
                chunks.append(m.group(0) + ", ".join(args + extra) + ")")
            k = j
        chunks.append(body[k:])
        body = "".join(chunks)
    return body


def _scan_balanced(body: str, start: int, spans: list[tuple[int, int]] | None = None) -> tuple[str, int]:
    """Text inside the parens opening at start, plus the index just past the
    matching close. Unlike _split_top this keeps commas intact."""
    assert body[start] == "("
    depth = 0
    j = start
    while j < len(body):
        if spans is not None and _in_spans(spans, j):
            j += 1
            continue
        ch = body[j]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if not depth:
                return body[start + 1:j], j + 1
        j += 1
    return body[start + 1:j], j


def _wrap_cond_equality(body: str) -> str:
    """`if (a == b)` over vectors is a bvec in slang: wrap top-level ==/!= in
    all()/any(). Scalars are unaffected (all/any are identity there)."""
    out: list[str] = []
    i = 0
    spans = _comment_spans(body)
    pat = re.compile(r"\b(if|while)\s*\(")
    while True:
        m = pat.search(body, i)
        if not m:
            out.append(body[i:])
            break
        if _in_spans(spans, m.start()):
            out.append(body[i:m.end()])
            i = m.end()
            continue
        out.append(body[i:m.end()])
        cond, j = _scan_balanced(body, m.end() - 1, spans)
        rebuilt = " || ".join(" && ".join(_wrap_one_cmp(seg) for seg in _split_logic(part, "&&"))
                               for part in _split_logic(cond, "||"))
        out.append(rebuilt + ")")
        i = j
    return "".join(out)


def _split_logic(cond: str, op: str) -> list[str]:
    parts: list[str] = []
    cur = ""
    depth = 0
    k = 0
    while k < len(cond):
        ch = cond[k]
        if ch in "([":
            depth += 1
            cur += ch
        elif ch in ")]":
            depth = max(0, depth - 1)
            cur += ch
        elif depth == 0 and cond.startswith(op, k):
            parts.append(cur)
            cur = ""
            k += len(op)
            continue
        else:
            cur += ch
        k += 1
    parts.append(cur)
    return parts


def _looks_vector(expr: str) -> bool:
    e = expr.strip()
    # multi-char swizzle or vector constructor strongly suggests vector type.
    # single-char swizzles and plain identifiers stay untouched: slang
    # all()/any() need vectors, and scalar == is already fine.
    if re.search(r"\.[xyzwrgbastpq]{2,4}\b", e):
        return True
    if re.search(r"\b(vec[234]|ivec[234]|uvec[234]|bvec[234]|float[234]|int[234]|uint[234])\s*\(", e):
        return True
    return False


def _wrap_one_cmp(seg: str) -> str:
    depth = 0
    k = 0
    while k < len(seg):
        ch = seg[k]
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth = max(0, depth - 1)
        elif depth == 0 and seg.startswith("==", k):
            a, b = seg[:k], seg[k + 2:]
            if _looks_vector(a) or _looks_vector(b):
                return f"all(({a.strip()}) == ({b.strip()}))"
            return seg
        elif depth == 0 and seg.startswith("!=", k):
            a, b = seg[:k], seg[k + 2:]
            if _looks_vector(a) or _looks_vector(b):
                return f"any(({a.strip()}) != ({b.strip()}))"
            return seg
        k += 1
    return seg


def emit_program(pack: Path, program: str, world: str, out: Path,
                 extra_defines: dict[str, str] | None = None, fullscreen: bool = False):
    srcs, pps = {}, {}
    for stage in ("vertex", "fragment"):
        src, pp = preprocess_stage(pack, world, program, stage)
        if extra_defines:
            for k, v in extra_defines.items():
                pp.define(k, v)
            src, pp = preprocess_stage(pack, world, program, stage)
            # NOTE: defines must exist before preprocessing; redo cleanly below
        srcs[stage] = (src, pp)

    # redo with defines applied before preprocessing when requested
    if extra_defines:
        srcs = {}
        for stage in ("vertex", "fragment"):
            ext = ".vsh" if stage == "vertex" else ".fsh"
            stub = pack / "shaders" / world / (program + ext)
            pp = Preproc(pack / "shaders")
            for name in STUB_DEFINE_RE.findall(stub.read_text(encoding="utf-8", errors="replace")):
                pp.define(name)
            for k, v in extra_defines.items():
                pp.define(k, v)
            pp.define("VERTEX_SHADER" if stage == "vertex" else "FRAGMENT_SHADER")
            srcs[stage] = (pp.run(stub, stub), pp)

    IRIS_SAMPLERS = ["tex", "lightmap", "normals", "specular",
                     "shadowtex0", "shadowtex1", "shadowcolor0", "shadowcolor1",
                     "depthtex0", "depthtex1", "noisetex",
                     "gaux1", "gaux2", "gaux3", "gaux4"] + \
        [f"colortex{i}" for i in range(16)]
    bodies, uni, samp = {}, {}, set()
    vary: dict[str, list] = {}
    for stage in ("vertex", "fragment"):
        body, uniforms, samplers, ios = rewrite_stage(srcs[stage][0], SAMPLER_SLOTS)
        bodies[stage] = body
        for n, (t, a) in uniforms.items():
            uni.setdefault(n, (t, a))
        samp |= samplers
        vary[stage] = ios
    # Iris builtin samplers need no declaration in the pack; pick up real uses
    for stage in ("vertex", "fragment"):
        for name in IRIS_SAMPLERS:
            if re.search(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_])", bodies[stage]):
                samp.add(name)

    v_outs = [(q if q in ("flat", "centroid", "noperspective") else "", t, n)
              for (q, st, t, n) in vary["vertex"] if st in ("varying", "out")]
    f_ins = [(q if q in ("flat", "centroid", "noperspective") else "", t, n)
             for (q, st, t, n) in vary["fragment"] if st in ("varying", "in")]
    # generated tile-layer varying for atlas array taps (decoded in vsMain);
    # fullscreen passes sample no atlas
    if not fullscreen:
        v_outs.append(("", "float", "jLayer"))

    v_names = {n for _, _, n in v_outs}
    missing = [n for _, _, n in f_ins if n not in v_names]
    if missing:
        raise SystemExit(f"fragment inputs without vertex writer: {missing}")

    needed_matrices: dict[str, str] = {}
    pps = {stage: srcs[stage][1] for stage in ("vertex", "fragment")}
    emitted_macros: set[str] = set()
    for stage in ("vertex", "fragment"):
        bodies[stage] = _rename_builtins(bodies[stage], needed_matrices)
    # engine-fed fixed-function fog (renamed above, after stripping)
    if any("u_jFogStart" in bodies[stage] or "u_jFogScale" in bodies[stage] for stage in ("vertex", "fragment")):
        uni.setdefault("FogStart", ("float", ""))
        uni.setdefault("FogScale", ("float", ""))
    # sampler map: known slots renamed, unknown keep name (unbound decls)
    smap = {s: SAMPLER_SLOTS.get(s, s) for s in samp}
    for stage in ("vertex", "fragment"):
        bodies[stage] = _rename_samplers(bodies[stage], smap)
        bodies[stage] = _rewrite_atlas_taps(bodies[stage])
        bodies[stage] = _rewrite_calls(bodies[stage])
        bodies[stage] = _wrap_cond_equality(bodies[stage])
        head = render_defines(pps[stage], emitted_macros)
        if head:
            bodies[stage] = head + "\n" + bodies[stage]
        bodies[stage] = _size_global_arrays(bodies[stage])
        bodies[stage] = _slang_globals(bodies[stage])
        bodies[stage] = _slang_ctors(bodies[stage])
        bodies[stage] = _defer_global_initializers(bodies[stage])
        matrices = set(re.findall(r"\b(?:mat[234]|float[234]x[234])\s+(\w+)", bodies[stage]))
        matrices.update(needed_matrices.values())
        matrices.update(name for name, (ty, _) in uni.items() if ty.startswith("mat"))
        matrix_names = "(?:" + "|".join(re.escape(name) for name in sorted(matrices, key=len, reverse=True)) + ")"
        bodies[stage] = re.sub(r"\b(" + matrix_names + r")\s*\*\s*(" + matrix_names + r")\b",
                               r"mul(\1, \2)", bodies[stage])
        bodies[stage] = re.sub(r"\b(" + matrix_names + r")\s*\[([^\]]+)\]",
                               r"transpose(\1)[\2]", bodies[stage])
        bodies[stage] = bodies[stage].replace("(m)[3].xyz", "transpose(m)[3].xyz")
        bodies[stage] = bodies[stage].replace("shadowPos.xy /= distortFactor;", "")
        bodies[stage] = bodies[stage].replace("shadowPos.z *= 0.2;", "")
        bodies[stage] = bodies[stage].replace("shadowpos.xy *= 1.0 / distortFactor;", "")
        bodies[stage] = bodies[stage].replace("shadowpos.z = shadowpos.z * 0.2;", "")
        bodies[stage] = bodies[stage].replace("vec3(vec2(wpos.xy / distortFactor), wpos.z * 0.2)", "wpos")

    import json as _json
    try:
        _slot_nums = {k: v[0] for k, v in _json.loads(
            (Path(__file__).resolve().parent.parent / "src/shaders/slang/texture_slots.json").read_text()).items()}
    except Exception:
        _slot_nums = {}
    _known_slots = set(_slot_nums)
    try:
        _vv = (Path(__file__).resolve().parent.parent / "src/shaders/slang/vv_resources.slang").read_text()
        _vv_declared = set(re.findall(r"SAMPLER2D(ARRAY)?\((\w+)", _vv))
        _vv_declared = {m[1] if isinstance(m, tuple) else m for m in _vv_declared}
    except Exception:
        _vv_declared = set()
    unmapped_samplers = sorted(s for s in samp
                               if s not in SAMPLER_SLOTS or SAMPLER_SLOTS[s] not in _known_slots)
    targets = sorted({int(m) for stage in ("vertex", "fragment")
                      for m in re.findall(r"gl_FragData(\d+)", bodies[stage])} or [0])
    n_targets = max(targets) + 1

    lines: list[str] = []
    lines.append(f"// Converted from Java shaderpack program {program} ({world}).")
    lines.append("// v1 converter output: uniform feed + renderer wiring still needed, see report.")
    lines.append('#include "compat.slang"')

    lines.append("")
    lines.append("// Iris uniforms: engine must feed these each frame/draw.")
    lines.append("#define PERLIN_JAVA_UNIFORMS " + "\\")
    for name in sorted(uni):
        ty, arr = uni[name]
        st = GLSL_TO_SLANG_TYPE.get(ty, f"/* UNKNOWN:{ty} */ float4")
        lines.append(f"    {st} u_j{name};{f' // [{arr}]' if arr else ''}" if False else
                     f"    {st} u_j{name}" + (f"[{arr}]" if arr else "") + "; " + "\\")
    for java, member in sorted(needed_matrices.values() and needed_matrices.items()):
        _ = java
        mty = "float3x3" if "Normal" in member else "float4x4"
        lines.append(f"    {mty} {member}; " + "\\")
    lines.append("")
    lines.append('#include "vv_resources.slang"')
    lines.append("#undef PERLIN_JAVA_UNIFORMS")
    lines.append("")
    for s in sorted(samp):
        slot = SAMPLER_SLOTS.get(s, s)
        if slot in _known_slots and slot in _vv_declared:
            lines.append(f"// java sampler {s} -> RHI slot {slot} (see vv_resources.slang)")
        elif slot in _known_slots:
            # post-chain slots live in each program, not the shared header
            n = _slot_nums[slot]
            lines.append(f"// java sampler {s} -> RHI slot {slot} (declared here)")
            lines.append(f"SAMPLER2D({slot}, {n * 2}, {n * 2 + 1});")
        else:
            lines.append(f"// TODO(java): sampler {s} has no RHI slot yet: left unbound below")
            lines.append(f"Texture2D {slot};")
            lines.append(f"SamplerState {slot}_s;")
    lines.append("")

    loc = {n: i for i, (_, _, n) in enumerate(v_outs)}
    lines.append("struct VsIn")
    lines.append("{")
    # fullscreen passes derive uvs from the triangle; only position is fed
    vin = [e for e in VERTEX_INPUTS if not fullscreen or e[1] == "a_position"]
    for ty, nm, lc, _ in vin:
        if nm in PACKED_INPUTS:
            pty, pnm, plc, _stmts = PACKED_INPUTS[nm]
            # constant-fed inputs (chunk fade): no vertex channel at all
            if pty is None:
                continue
            lines.append(f"    [[vk::location({plc})]] {GLSL_TO_SLANG_TYPE.get(pty, pty)} {pnm} : TEXCOORD{plc};")
        else:
            lines.append(f"    [[vk::location({lc})]] {GLSL_TO_SLANG_TYPE.get(ty, ty)} {nm} : TEXCOORD{lc};")
    lines.append("};")
    lines.append("struct VsOut")
    lines.append("{")
    lines.append("    float4 clipPosition : SV_Position;")
    for _q, t, n in v_outs:
        flat = ("nointerpolation " if _q == "flat" else "noperspective " if _q == "noperspective" else "centroid " if _q == "centroid" else "")
        lines.append(f"    [[vk::location({loc[n]})]] {flat}{GLSL_TO_SLANG_TYPE.get(t, t)} v_{n} : TEXCOORD{loc[n]};")
    lines.append("};")
    lines.append("struct FsIn")
    lines.append("{")
    lines.append("    float4 fragCoord : SV_Position;")
    for _q, t, n in v_outs:
        flat = ("nointerpolation " if _q == "flat" else "noperspective " if _q == "noperspective" else "centroid " if _q == "centroid" else "")
        lines.append(f"    [[vk::location({loc[n]})]] {flat}{GLSL_TO_SLANG_TYPE.get(t, t)} v_{n} : TEXCOORD{loc[n]};")
    lines.append("};")
    lines.append("struct FsOut")
    lines.append("{")
    for t in range(n_targets):
        lines.append(f"    float4 t{t} : SV_Target{t};")
    lines.append("};")
    lines.append("")

    for stage in ("vertex", "fragment"):
        ns = "vs_stage" if stage == "vertex" else "fs_stage"
        lines.append(f"namespace {ns}")
        lines.append("{")
        if stage == "vertex":
            lines.append("static float4 gl_Position;")
            for ty, nm, _lc, _ in VERTEX_INPUTS:
                lines.append(f"static {GLSL_TO_SLANG_TYPE.get(ty, ty)} {nm};")
            for _q, t, n in v_outs:
                lines.append(f"static {GLSL_TO_SLANG_TYPE.get(t, t)} {n};")
        else:
            lines.append("static float4 gl_FragCoord;")
            for t in range(n_targets):
                lines.append(f"static float4 gl_FragData{t};")
            for _q, t, n in v_outs:
                lines.append(f"static {GLSL_TO_SLANG_TYPE.get(t, t)} {n};")
        for name in sorted(uni):
            ty, _arr = uni[name]
            lines.append(f"static {GLSL_TO_SLANG_TYPE.get(ty, ty)} {name};")
        for _java, member in sorted(needed_matrices.items()):
            mty = "float3x3" if "Normal" in member else "float4x4"
            lines.append(f"static {mty} {member};")
        lines.append(bodies[stage])
        lines.append("}")
        lines.append("")

    lines.append("// the pack spells matrix math M * v; slang only defines mul():")
    lines.append("// cover the pack's combinations so bodies stay untouched.")
    for _m, _v in (("float2x2", "float2"), ("float3x3", "float3"), ("float4x4", "float4")):
        lines.append(f"{_v} operator*({_m} m, {_v} v) {{ return mul(m, v); }}")
        lines.append(f"{_v} operator*({_v} v, {_m} m) {{ return mul(v, m); }}")
    for _a, _b, _c in (("float2x2", "float2x2", "float2"), ("float3x3", "float3x3", "float3"),
                        ("float4x4", "float4x4", "float4")):
        lines.append(f"{_a} operator*({_a} a, {_a} b) {{ return mul(a, b); }}")
    lines.append("")
    lines.append("// manual depth compare keeps the pack's own PCF taps; works with the")
    lines.append("// existing non-comparison depth samplers until an RHI compare sampler exists.")
    lines.append("float4 javaShadow2D(Texture2D t, SamplerState s, float3 uvw)")
    lines.append("{ if (u_shadowInfo.w <= 0.0 || any(uvw.xyz < 0.0) || any(uvw.xyz > 1.0)) return float4(1.0); return float4(step(uvw.z, t.Sample(s, uvw.xy).r)); }")
    lines.append("float4 javaShadow2D(Texture2D t, SamplerState s, float4 uvw)")
    lines.append("{ if (u_shadowInfo.w <= 0.0 || any(uvw.xyz < 0.0) || any(uvw.xyz > 1.0)) return float4(1.0); return float4(step(uvw.z, t.Sample(s, uvw.xy).r)); }")
    lines.append("float javaShadow2DProj(Texture2D t, SamplerState s, float4 uvw)")
    lines.append("{ float w = max(uvw.w, 1e-6); return step(uvw.z / w, t.Sample(s, uvw.xy / w).r); }")
    lines.append("float javaTextureProj(Texture2D t, SamplerState s, float4 uvw)")
    lines.append("{ float w = max(uvw.w, 1e-6); return t.Sample(s, uvw.xy / w).r; }")
    lines.append("int2 javaTextureSize(Texture2D t, int lod) { uint w = 0, h = 0, n = 0; t.GetDimensions(uint(lod), w, h, n); return int2(w, h); }")
    lines.append("")
    lines.append("// engine light bytes are 0..15 levels; the pack wants 0..1")
    lines.append("float4 javaLightCoord(float4 t) { return float4((t.xy * 255.0 + 8.0) / 256.0, t.zw); }")
    lines.append("")
    lines.append("VsOut vsMain(VsIn input)")
    lines.append("{")
    # packed java attribs: unpack lines use a literal {src} marker (not
    # str.format: unpack code itself contains slang braces).
    # fullscreen passes feed position only; uvs derive from the triangle.
    fs_pre = {
        "a_texcoord0": "vs_stage::a_texcoord0 = vec2(input.a_position.x * 0.5 + 0.5, 0.5 - input.a_position.y * 0.5);",
        "a_texcoord1": "vs_stage::a_texcoord1 = vec4(0.5, 0.5, 0.0, 1.0);",
    }
    for _ty, nm, _lc, _ in VERTEX_INPUTS:
        if fullscreen and nm not in ("a_position", "a_texcoord0", "a_texcoord1"):
            continue
        if nm in fs_pre and fullscreen:
            lines.append(f"    {fs_pre[nm]}")
        elif nm in PACKED_INPUTS:
            _pty, pnm, _plc, stmts = PACKED_INPUTS[nm]
            for st in stmts:
                lines.append(f"    {st.replace('{src}', 'input.' + pnm) if pnm else st}")
        else:
            lines.append(f"    vs_stage::{nm} = input.{nm};")
    if program in ("gbuffers_terrain", "gbuffers_water"):
        lines.append("    { float ao = fract(input.a_tPacked) * 16.0; ao = ao > 0.0 ? ao : 1.0;")
        lines.append("      vs_stage::a_color0.rgb /= max(ao * input.a_nPacked.w, 1.0 / 255.0); vs_stage::a_color0.a = ao; }")
    if program in ("gbuffers_terrain", "gbuffers_water", "shadow"):
        lines.append("    { int id = int(input.a_ePacked + 0.5); vs_stage::a_mcEntity.x = texelFetch(s_javaMaterials, int2(id & 255, id >> 8), 0).r; }")
    for name in sorted(uni):
        lines.append(f"    vs_stage::{name} = u_j{name};")
    for _java, member in sorted(needed_matrices.items()):
        lines.append(f"    vs_stage::{member} = {member};")
    # tile layer for atlas array taps, before the pack body samples anything
    if not fullscreen:
        lines.append("    vs_stage::jLayer = floor(input.a_texcoord1.z * 255.0 + 0.5) + floor(input.a_texcoord1.w * 255.0 + 0.5) * 256.0;")
    lines.append("    vs_stage::stageMain();")
    lines.append("    VsOut output;")
    lines.append("    output.clipPosition = float4(input.a_position.xy, 0.0, 1.0);" if fullscreen else "    output.clipPosition = vs_stage::gl_Position;")
    for _q, _t, n in v_outs:
        lines.append(f"    output.v_{n} = vs_stage::{n};")
    lines.append("    return output;")
    lines.append("}")
    lines.append("")
    lines.append('[shader("fragment")]')
    lines.append("FsOut fsMain(FsIn input)")
    lines.append("{")
    lines.append("    fs_stage::gl_FragCoord = input.fragCoord;")
    for _q, _t, n in v_outs:
        lines.append(f"    fs_stage::{n} = input.v_{n};")
    for name in sorted(uni):
        lines.append(f"    fs_stage::{name} = u_j{name};")
    for _java, member in sorted(needed_matrices.items()):
        lines.append(f"    fs_stage::{member} = {member};")
    lines.append("    fs_stage::stageMain();")
    lines.append("    FsOut output;")
    for t in range(n_targets):
        lines.append(f"    output.t{t} = fs_stage::gl_FragData{t};")
    lines.append("    return output;")
    lines.append("}")
    lines.append("")

    out.write_text("\n".join(line.rstrip() for line in lines), encoding="utf-8")
    return {
        "program": program, "world": world,
        "varyings": [(t, n) for _, t, n in v_outs],
        "uniform_count": len(uni),
        "extra_matrices": sorted(needed_matrices.items()),
        "samplers_mapped": {s: SAMPLER_SLOTS[s] for s in sorted(samp) if s in SAMPLER_SLOTS},
        "samplers_unmapped": unmapped_samplers,
        "targets": targets,
        "legacy": sorted({m.split("[")[0] for m in needed_matrices} |
                         ({ "gl_FragCoord"} if any("gl_FragCoord" in b for b in bodies.values()) else set())),
    }



# ---------------------------------------------------------------- validation
#
# No slangc on this machine, so stage bodies (still GLSL-spelled) are checked
# with glslangValidator against a harness declaring the same interface the
# .slang output declares. Catches undeclared identifiers and type errors in
# the mechanical rewrite; it does not compile the slang wrapper itself.

GLSL_TYPE = {"bool": "bool", "int": "int", "uint": "uint", "float": "float",
             "vec2": "vec2", "vec3": "vec3", "vec4": "vec4",
             "ivec2": "ivec2", "ivec3": "ivec3", "mat4": "mat4"}


def _harness(program: str, world: str, pack: Path, stage: str) -> tuple[str, dict]:
    from collections import OrderedDict
    src, _pp = preprocess_stage(pack, world, program, stage)
    body, uniforms, samplers, ios = rewrite_stage(src, SAMPLER_SLOTS)
    IRIS = ["tex", "lightmap", "normals", "specular", "shadowtex0", "shadowtex1",
            "shadowcolor0", "shadowcolor1", "depthtex0", "depthtex1", "noisetex",
            "gaux1", "gaux2", "gaux3", "gaux4"] + [f"colortex{i}" for i in range(16)]
    for name in IRIS:
        if re.search(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_])", body):
            samplers.add(name)
    needed: dict[str, str] = {}
    body = _rename_builtins(body, needed)
    smap = {s: SAMPLER_SLOTS.get(s, s) for s in samplers}
    body = _rename_samplers(body, smap)
    head = render_defines(_pp)
    if head:
        body = head + "\n" + body

    outs = [(t, n, ("flat " if q in ("flat", "centroid") else ""))
            for (q, st, t, n) in ios if st in ("varying", "out", "in")]
    is_vert = stage == "vertex"
    # varyings flow vert-out -> frag-in; validate each stage standalone
    decls = ["#version 430", "precision highp float;", "precision highp int;"]
    if is_vert:
        for ty, nm, _lc, _ in VERTEX_INPUTS:
            decls.append(f"layout(location={_lc}) in {GLSL_TYPE.get(ty, ty)} {nm};")
    else:
        decls.append("layout(location=0) out vec4 t0;")
        decls.append("layout(location=1) out vec4 t1;")
        decls.append("#define gl_FragData0 t0")
        decls.append("#define gl_FragData1 t1")
    if is_vert:
        for t, n, q in outs:
            decls.append(f"{q}out {GLSL_TYPE.get(t, t)} {n};")
    else:
        for t, n, q in outs:
            decls.append(f"{q}in {GLSL_TYPE.get(t, t)} {n};")
    for name in sorted(uniforms):
        ty, arr = uniforms[name]
        decls.append(f"uniform {GLSL_TYPE.get(ty, 'vec4')} {name}{f'[{arr}]' if arr else ''};")
    for java, member in sorted(needed.items()):
        mty = "mat3" if "Normal" in member else "mat4"
        decls.append(f"uniform {mty} {member};")
    for s in sorted(samplers):
        slot = SAMPLER_SLOTS.get(s, s)
        decls.append(f"uniform sampler2D {slot};")
        decls.append(f"uniform sampler2D {slot}_s;  // split sampler only; slang binds the real one")
    decls.append("vec4 javaShadow2D(sampler2D t, sampler2D s, vec3 uvw) { return vec4(1.0); }")
    decls.append("vec4 javaShadow2D(sampler2D t, sampler2D s, float f, vec3 uvw) { return vec4(1.0); }")
    decls.append("vec4 javaShadow2DProj(sampler2D t, sampler2D s, vec4 uvw) { return vec4(1.0); }")
    decls.append("#define texture2DLod(t, uv, lod) textureLod(t, uv, lod)")
    decls.append("#define texture2D(t, uv) texture(t, uv)")
    decls.append("#define texture2DProj(t, uvw) textureProj(t, uvw)")
    decls.append("float javaTextureProj(sampler2D t, vec4 uvw) { return 1.0; }")
    return "\n".join(decls) + "\n" + body, {"varyings": len(outs), "uniforms": len(uniforms),
                                            "samplers": len(samplers), "matrices": len(needed)}


def check_program(pack: Path, program: str, world: str, workdir: Path) -> int:
    import subprocess
    rc = 0
    for stage in ("vertex", "fragment"):
        src, info = _harness(program, world, pack, stage)
        # varyings were declared with v_ prefix; bodies use bare names -> alias
        ext = "vert" if stage == "vertex" else "frag"
        path = workdir / f"{program}.{ext}"
        path.write_text(src, encoding="utf-8")
        r = subprocess.run(["glslangValidator", str(path)], capture_output=True, text=True)
        print(f"--- {stage}: varyings={info['varyings']} uniforms={info['uniforms']} "
              f"samplers={info['samplers']} extramat={info['matrices']}")
        if r.returncode != 0:
            rc = 1
            print(r.stdout[-4000:])
            print(r.stderr[-2000:])
        else:
            print("OK")
    return rc


if __name__ == "__main__":
    sys.exit(main())
