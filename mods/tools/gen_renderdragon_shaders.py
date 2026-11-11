"""Adapt pinned RenderDragon GLSL to Perlin's vertex and texture interfaces, as slang programs.

Upstream pixel programs are read verbatim, with interface declarations removed,
texture/uniform bindings renamed, and scalar constructors made portable. The
small vertex bridges unpack Perlin's meshes, which use different vertex formats
and CPU skinning from Bedrock. Each vertex/fragment pair is then turned into one
slang program by bgfx_to_slang. All output lives in the build directory.
"""
import argparse
import json
import re
from pathlib import Path

import bgfx_to_slang

DEFINES = '''#define INSTANCING__OFF 1
#define RENDER_AS_BILLBOARDS__OFF 1
#define SEASONS__OFF 1
#define DITHERING__OFF 1
#define SOURCE_INPUT_TYPE0__SAMPLED 1
#define SOURCE_INPUT_TYPE1__CONSTANT 1
#define SOURCE_INPUT_TYPE2__CONSTANT 1
#define MULTI_COLOR_TINT__OFF 1
#define ALPHA_TEST__OFF 1
#define FONT_TYPE__TRUE_TYPE 1
#define NO_OCCLUSION__ON 1
#define FLIP_OCCLUSION__OFF 1
'''

COMMON = '#include "rd_common.slang"\n'

TERRAIN_VARYING = '''vec3 a_position : POSITION;
vec4 a_color0 : COLOR0;
vec2 a_texcoord0 : TEXCOORD0;
vec4 a_texcoord1 : TEXCOORD1;
vec4 v_color0 : COLOR0;
vec2 v_texcoord0 : TEXCOORD0;
vec2 v_lightmapUV : TEXCOORD1;
vec2 v_ditheringAndMaskTinting : TEXCOORD2;
vec4 v_fog : COLOR2;
float v_layer : TEXCOORD3;
'''
TERRAIN_IO = 'v_color0, v_texcoord0, v_lightmapUV, v_ditheringAndMaskTinting, v_fog, v_layer'
TERRAIN_ARGS = 'vec4 v_color0, vec2 v_texcoord0, vec2 v_lightmapUV, vec2 v_ditheringAndMaskTinting, vec4 v_fog, float v_layer'
TERRAIN_VERTEX = '''void main()
{
    gl_Position = mul(u_mvp, vec4(a_position, 1.0));
    vec3 relative = a_position - u_camera.xyz;
    vec2 light = floor(a_texcoord1.xy * 255.0 + 0.5);
    if (u_dynParams.x > 0.5)
        light.x = max(light.x, dynamicLevel(relative) * 16.0);
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
    v_layer = floor(a_texcoord1.z * 255.0 + 0.5) + floor(a_texcoord1.w * 255.0 + 0.5) * 256.0;
    v_lightmapUV = clamp(light / 256.0 + vec2_splat(0.5 / 16.0), vec2_splat(0.5 / 16.0), vec2_splat(15.5 / 16.0));
    v_ditheringAndMaskTinting = vec2_splat(0.0);
    float visibility = mix(1.0, u_chunk.x, clamp((length(relative) - 16.0) / 16.0, 0.0, 1.0));
    v_fog = vec4(u_fogColor.rgb, max(rdFog(relative), 1.0 - visibility));
}
'''
UI_VARYING = '''vec3 a_position : POSITION;
vec4 a_color0 : COLOR0;
vec2 a_texcoord0 : TEXCOORD0;
vec4 v_color0 : COLOR0;
vec2 v_texcoord0 : TEXCOORD0;
vec4 v_fog : COLOR1;
vec4 v_light : COLOR2;
vec4 v_layerUv : TEXCOORD1;
'''
UI_IO = 'v_color0, v_texcoord0, v_fog, v_light, v_layerUv'
UI_ARGS = 'vec4 v_color0, vec2 v_texcoord0, vec4 v_fog, vec4 v_light, vec4 v_layerUv'
UI_UNIFORMS = '#include "rd_ui.slang"\n'


def portable(text):
    """bgfx's HLSL targets require explicit scalar splats."""
    scalars = set(re.findall(r'\b(?:float|int|uint)\s+(\w+)', text))
    def splat(match):
        typ, arg = match.groups()
        if re.fullmatch(r'[\d.eE+\-]+[fu]?', arg) or arg in scalars or re.fullmatch(r'\w+\.[xyzwrgba]', arg):
            return typ + '_splat(' + arg + ')'
        return match.group(0)
    return re.sub(r'\b(vec[234]|uvec[234])\(([^(),]+)\)', splat, text)


def upstream(root, family, render_pass, args, name, samples=None, replacements=None):
    path = root / 'materials' / family / (render_pass + '.Fragment.glsl')
    text = path.read_text(encoding='utf-8')
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'^#version[^\n]*\n|^precision[^\n]*\n', '', text, flags=re.M)
    text = re.sub(r'^\s*(?:(?:centroid|flat)\s+)?(?:in|out|uniform)\s+[^;]+;\s*$', '', text, flags=re.M)
    text = re.sub(r'^\s*layout\([^\n]+\)\s+out[^;]+;', '', text, flags=re.M)
    text = re.sub(r'\b(?:highp|mediump|lowp)\s+', '', text)
    for sampler, call in (samples or {}).items():
        text = re.sub(r'\btexture\(\s*' + re.escape(sampler) + r'\s*,', call + '(', text)
    text = text.replace('texture(', 'texture2D(')
    for before, after in (replacements or {}).items():
        text = re.sub(r'\b' + re.escape(before) + r'\b', after, text)
    text = re.sub(r'void main\(\)', 'vec4 ' + name + '(' + args + ')', text)
    text = text.replace('bgfx_FragData0 =', 'return')
    if 'bgfx_FragData0' in text or 'sampler2D' in text or 'layout(' in text:
        raise ValueError(f'Unadapted shader interface: {path}')
    return '// Upstream: ' + path.relative_to(root).as_posix() + '\n' + portable(text)


# program -> (vertex, fragment, varying definition)
PROGRAMS = {
    'terrain_solid': ('vs_terrain', 'fs_terrain_solid', 'varying_terrain'),
    'terrain_cutout': ('vs_terrain', 'fs_terrain_cutout', 'varying_terrain'),
    'terrain_translucent': ('vs_terrain', 'fs_terrain_translucent', 'varying_terrain'),
    'sky': ('vs_sky', 'fs_sky', 'varying_sky'),
    'star': ('vs_star', 'fs_star', 'varying_star'),
    'sunrise': ('vs_sunrise', 'fs_sunrise', 'varying_sunrise'),
    'celestial': ('vs_celestial', 'fs_celestial', 'varying_celestial'),
    'cloud': ('vs_cloud', 'fs_cloud', 'varying_cloud'),
    'screen': ('vs_screen', 'fs_screen', 'varying_screen'),
    'ui': ('vs_ui', 'fs_ui', 'varying_ui'),
    'entity': ('vs_ui', 'fs_entity', 'varying_ui'),
    'entity_mask': ('vs_ui', 'fs_entity_mask', 'varying_ui'),
    'entity_emissive': ('vs_ui', 'fs_entity_emissive', 'varying_ui'),
    'entity_glint': ('vs_ui', 'fs_entity_glint', 'varying_ui'),
    'particle': ('vs_ui', 'fs_particle', 'varying_ui'),
    'particle_cutout': ('vs_ui', 'fs_particle_cutout', 'varying_ui'),
    'weather': ('vs_ui', 'fs_weather', 'varying_ui'),
    'cracks': ('vs_ui', 'fs_cracks', 'varying_ui'),
    'nametag': ('vs_ui', 'fs_nametag', 'varying_ui'),
}


def generate(root, out):
    out.mkdir(parents=True, exist_ok=True)
    manifest = {}
    sources = {}
    def write(name, text, source=None):
        sources[name] = text
        if source:
            manifest[name] = source
    def fragment(name, family, render_pass, io, args, prefix, call_args, suffix='', samples=None, replacements=None):
        body = upstream(root, family, render_pass, args, 'rdMain', samples, replacements)
        write(name + '.sc', '$input ' + io + '\n' + COMMON + DEFINES + prefix + body +
              '\nvoid main() { vec4 color = rdMain(' + call_args + ');\n' + suffix + '\ngl_FragColor = color; }\n',
              f'{family}.{render_pass}')

    write('varying_terrain.def.sc', TERRAIN_VARYING)
    write('vs_terrain.sc', '$input a_position, a_color0, a_texcoord0, a_texcoord1\n$output ' + TERRAIN_IO + '\n' + COMMON + TERRAIN_VERTEX)
    for name, render_pass in [('fs_terrain_solid', 'Opaque'), ('fs_terrain_cutout', 'AlphaTest'), ('fs_terrain_translucent', 'Transparent')]:
        body = upstream(root, 'RenderChunk', render_pass, TERRAIN_ARGS, 'rdMain',
                        {'s_LightMapTexture': 'rdLightmap'}, {'FogColor': 'u_fogColor'})
        body = re.sub(r'texture2D\(s_MatTexture,\s*v_texcoord0\)', 'sampleBlock(v_texcoord0, v_layer)', body)
        prefix = '#include "rd_block_sampling.slang"\nvec4 rdLightmap(vec2 uv) { return texture2D(s_lightmap, uv); }\n'
        write(name + '.sc', '$input ' + TERRAIN_IO + '\n' + COMMON + DEFINES + prefix + body +
              '\nvoid main() { gl_FragColor = rdMain(' + TERRAIN_IO + '); }\n', 'RenderChunk.' + render_pass)

    write('varying_ui.def.sc', UI_VARYING)
    write('vs_ui.sc', '$input a_position, a_color0, a_texcoord0\n$output ' + UI_IO + '\n' + COMMON + UI_UNIFORMS + '''
void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    vec3 world = mul(u_model[0], vec4(a_position, 1.0)).xyz;
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
    v_light = vec4(a_color0.rgb, 1.0);
    v_fog = vec4(u_fogColor.rgb, rdFog(world - u_camera.xyz) * u_rdMaterial.x);
    // Bedrock foil: two layers rotated -20 and 80 degrees about the texture centre, scaled by
    // u_uiGlintA.zw and scrolled along U by u_uiGlintA.xy.
    vec2 glintC = a_texcoord0 - vec2(0.5, 0.5);
    vec2 glint0 = vec2(dot(glintC, vec2(0.9396926, -0.3420201)), dot(glintC, vec2(0.3420201, 0.9396926))) + vec2(0.5, 0.5);
    vec2 glint1 = vec2(dot(glintC, vec2(0.1736482, 0.9848078)), dot(glintC, vec2(-0.9848078, 0.1736482))) + vec2(0.5, 0.5);
    v_layerUv = vec4(glint0 * u_uiGlintA.zw + vec2(u_uiGlintA.x, 0.0), glint1 * u_uiGlintA.zw + vec2(u_uiGlintA.y, 0.0));
}
''')
    samples = {'s_MatTexture0': 'rdBase', 's_MatTexture1': 'rdGlint', 's_GlyphTexture': 'rdBase', 's_ParticleTexture': 'rdBase', 's_CracksTexture': 'rdBase'}
    functions = ''
    fragment('fs_ui', 'UIText', 'Transparent', UI_IO, UI_ARGS, UI_UNIFORMS + functions,
             UI_IO, 'if (u_uiParams.x > 0.0 && color.a < u_uiParams.x) discard;', samples)
    for name, render_pass in [('fs_entity', 'AlphaTest'), ('fs_entity_mask', 'AlphaTestColorMask'), ('fs_entity_emissive', 'Emissive')]:
        fragment(name, 'Entity', render_pass, UI_IO, UI_ARGS, UI_UNIFORMS + functions, UI_IO,
                 'color.a *= v_color0.a;\nif (u_uiParams.x > 0.0 && color.a < u_uiParams.x) discard;', samples)
    prefix = UI_UNIFORMS + functions + '#undef SOURCE_INPUT_TYPE0__SAMPLED\n#define SOURCE_INPUT_TYPE0__CONSTANT 1\n#define MatColor0 vec4_splat(0.0)\n#undef OverlayColor\n#define OverlayColor vec4_splat(0.0)\n'
    fragment('fs_entity_glint', 'Entity', 'Glint', UI_IO, UI_ARGS, prefix,
             'v_color0, v_texcoord0, vec4_splat(0.0), v_light, v_layerUv',
             'color.rgb *= texture2D(s_uiTex, v_texcoord0).a; color.a = 0.0;', samples)
    for name, render_pass in [('fs_particle', 'Transparent'), ('fs_particle_cutout', 'AlphaTest')]:
        fragment(name, 'Particle', render_pass, UI_IO, UI_ARGS, UI_UNIFORMS + functions, UI_IO,
                 'if (u_uiParams.x > 0.0 && color.a < u_uiParams.x) discard;', samples)
    fragment('fs_cracks', 'Cracks', 'Transparent', UI_IO, UI_ARGS, UI_UNIFORMS + functions, UI_IO, samples=samples)
    fragment('fs_nametag', 'Nametag', 'Transparent', UI_IO, UI_ARGS, UI_UNIFORMS + functions + '#define CurrentColor vec4_splat(1.0)\n', UI_IO,
             replacements={'v_color': 'v_color0'})
    weather_prefix = UI_UNIFORMS + functions + '''vec4 rdWeather(vec2 uv) { return rdBase(uv); }
vec4 rdOcclusion(vec2 uv) { return vec4(0.0, 0.0, 15.0 / 255.0, 0.0); }
'''
    body = upstream(root, 'Weather', 'Transparent', UI_ARGS, 'rdMain',
                    {'s_WeatherTexture': 'rdWeather', 's_OcclusionTexture': 'rdOcclusion'},
                    {'v_occlusionUV': 'vec2_splat(0.5)', 'v_occlusionHeight': '(-1.0)'})
    body = re.sub(r'texture2D\(s_LightingTexture,\s*var_17770\)', 'vec4(v_color0.rgb, 1.0)', body)
    write('fs_weather.sc', '$input ' + UI_IO + '\n' + COMMON + DEFINES + weather_prefix + body +
          '\nvoid main() { vec4 color = rdMain(' + UI_IO + '); color.a *= v_color0.a; gl_FragColor = color; }\n', 'Weather.Transparent')

    sky_varying = '''vec3 a_position : POSITION;
vec4 a_color0 : COLOR0;
vec2 a_texcoord0 : TEXCOORD0;
vec4 v_color0 : COLOR0;
vec2 v_texcoord0 : TEXCOORD0;
'''
    sky_io = 'v_color0, v_texcoord0'
    sky_args = 'vec4 v_color0, vec2 v_texcoord0'
    sky_uniforms = '''#define SunMoonColor u_modulator
#define StarsColor u_modulator
'''
    for base, family, render_pass, inputs, color, position, texcoord in [
        # the sky disc is a handful of huge triangles: the position travels to the fragment stage so the fog blend is exact
        # per pixel instead of piecewise linear (which draws faint spokes and rings across the sky)
        ('sky', 'Sky', 'Opaque', 'a_position', 'vec4(a_position, 0.0)', 'a_position', 'vec2_splat(0.0)'),
        ('star', 'Stars', 'Transparent', 'a_position', 'vec4_splat(1.0)', 'a_position', 'vec2_splat(0.0)'),
        ('celestial', 'SunMoon', 'Transparent', 'a_position, a_texcoord0', 'vec4_splat(1.0)', 'a_position', 'a_texcoord0'),
        ('cloud', 'Clouds', 'Transparent', 'a_position, a_texcoord0', 'vec4(a_texcoord0.x * u_cloudColor.rgb, u_cloudColor.a * clamp(1.0 - max(length(a_position + u_cloudOffset.xyz) / max(u_fogSky.y, 1.0) - 0.9, 0.0), 0.0, 1.0))', 'a_position + u_cloudOffset.xyz', 'vec2_splat(0.0)'),
    ]:
        write('varying_' + base + '.def.sc', sky_varying)
        write('vs_' + base + '.sc', '$input ' + inputs + '\n$output ' + sky_io + '\n' + COMMON + '\nvoid main() {\ngl_Position = mul(u_mvp, vec4(' + position + ', 1.0));\nv_color0 = ' + color + ';\nv_texcoord0 = ' + texcoord + ';\n}\n')
        prefix = sky_uniforms
        if base == 'celestial':
            prefix += 'vec4 rdSky(vec2 uv) { return texture2D(s_sky, uv); }\n'
        call = sky_io
        suffix = ''
        if base == 'sky':
            call = 'mix(u_modulator, u_fogColor, linearFog(length(v_color0.xyz), 0.0, u_fogSky.x) * u_fogColor.a), v_texcoord0'
            # dark gradients quantise into visible contour lines in 8 bits: triangular dither of one step hides them
            suffix = ('vec2 ditherPos = gl_FragCoord.xy;\n'
                      'float ditherA = fract(52.9829189 * fract(dot(ditherPos, vec2(0.06711056, 0.00583715))));\n'
                      'float ditherB = fract(52.9829189 * fract(dot(ditherPos + vec2(17.0, 31.0), vec2(0.06711056, 0.00583715))));\n'
                      'color.rgb += vec3_splat((ditherA + ditherB - 1.0) / 255.0);')
        fragment('fs_' + base, family, render_pass, sky_io, sky_args, prefix, call, suffix,
                 samples={'s_SunMoonTexture': 'rdSky'})
    for base, inputs, color in [('sunrise', 'a_position, a_color0', 'a_color0 * u_modulator'), ('screen', 'a_position', 'u_modulator')]:
        write('varying_' + base + '.def.sc', sky_varying)
        write('vs_' + base + '.sc', '$input ' + inputs + '\n$output ' + sky_io + '\n' + COMMON +
              '\nvoid main() { gl_Position = mul(u_mvp, vec4(a_position, 1.0)); v_color0 = ' + color + '; v_texcoord0 = vec2_splat(0.0); }\n')
        fragment('fs_' + base, 'Sky', 'Opaque', sky_io, sky_args, sky_uniforms, sky_io)
    programs = {}
    for program, (vs, fs, varying) in PROGRAMS.items():
        text = bgfx_to_slang.convert(sources[vs + '.sc'], sources[fs + '.sc'], sources[varying + '.def.sc'], [],
                                     header='// Generated RenderDragon adapter; do not edit.\n',
                                     resources='rd_resources.slang', inline=False, shared=set())
        target = out / (program + '.slang')
        if not target.exists() or target.read_text() != text:
            target.write_text(text, encoding='utf-8')
        programs[program] = {'file': target.name, 'upstream': manifest.get(fs + '.sc')}
    (out / 'manifest.json').write_text(json.dumps(programs, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    generate(args.source, args.output)
