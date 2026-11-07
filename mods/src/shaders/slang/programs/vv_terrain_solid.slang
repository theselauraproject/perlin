// Visual Vibrance shader (see LICENSE-VisualVibrance.txt)
#include "compat.slang"

#include "vv_resources.slang"
#include "vv_common.slang"
#include "vv_terrain_common.slang"

struct VsIn
{
    [[vk::location(0)]] vec3 a_position : TEXCOORD0;
    [[vk::location(2)]] vec4 a_color0 : TEXCOORD2;
    [[vk::location(4)]] vec2 a_texcoord0 : TEXCOORD4;
    [[vk::location(5)]] vec4 a_texcoord1 : TEXCOORD5;
};
struct VsOut
{
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] vec4 v_color0 : TEXCOORD0;
    [[vk::location(1)]] vec2 v_texcoord0 : TEXCOORD1;
    [[vk::location(2)]] vec2 v_fog : TEXCOORD2;
    [[vk::location(3)]] float v_layer : TEXCOORD3;
    [[vk::location(4)]] float v_visibility : TEXCOORD4;
    [[vk::location(5)]] vec3 v_rel : TEXCOORD5;
    [[vk::location(6)]] vec2 v_light : TEXCOORD6;
    [[vk::location(7)]] vec3 v_tint : TEXCOORD7;
    [[vk::location(8)]] float v_foliage : TEXCOORD8;
    [[vk::location(9)]] vec3 v_sunLight : TEXCOORD9;
    [[vk::location(10)]] vec3 v_skyLight : TEXCOORD10;
    [[vk::location(11)]] vec4 v_haze : TEXCOORD11;
};
struct FsIn
{
    float4 fragCoord : SV_Position;
    [[vk::location(0)]] vec4 v_color0 : TEXCOORD0;
    [[vk::location(1)]] vec2 v_texcoord0 : TEXCOORD1;
    [[vk::location(2)]] vec2 v_fog : TEXCOORD2;
    [[vk::location(3)]] float v_layer : TEXCOORD3;
    [[vk::location(4)]] float v_visibility : TEXCOORD4;
    [[vk::location(5)]] vec3 v_rel : TEXCOORD5;
    [[vk::location(6)]] vec2 v_light : TEXCOORD6;
    [[vk::location(7)]] vec3 v_tint : TEXCOORD7;
    [[vk::location(8)]] float v_foliage : TEXCOORD8;
    [[vk::location(9)]] vec3 v_sunLight : TEXCOORD9;
    [[vk::location(10)]] vec3 v_skyLight : TEXCOORD10;
    [[vk::location(11)]] vec4 v_haze : TEXCOORD11;
};

namespace vs_stage
{
static vec4 gl_Position;
static vec3 a_position;
static vec4 a_color0;
static vec2 a_texcoord0;
static vec4 a_texcoord1;
static vec4 v_color0;
static vec2 v_texcoord0;
static vec2 v_fog;
static float v_layer;
static float v_visibility;
static vec3 v_rel;
static vec2 v_light;
static vec3 v_tint;
static float v_foliage;
static vec3 v_sunLight;
static vec3 v_skyLight;
static vec4 v_haze;

void stageMain()
{
    float layer = floor(a_texcoord1.z * 255.0 + 0.5) + floor(a_texcoord1.w * 255.0 + 0.5) * 256.0;
    float foliage = vvFoliageKind(layer);
    vec3 position = a_position;
    if (S_WAVING > 0.001 && foliage > 0.5)
        position += vvWave(a_position, foliage, a_texcoord0);
    vec3 relative = position - u_camera.xyz;
    gl_Position = mul(u_mvp, vec4(position, 1.0));
    v_foliage = foliage;

    v_fog = vec2(length(relative), max(length(relative.xz), abs(relative.y)));
    vec2 light = floor(a_texcoord1.xy * 255.0 + 0.5);
    if (u_dynParams.x > 0.5)
    {
        vec3 cell = floor(a_position) + vec3_splat(0.5) - u_camera.xyz;
        light.x = max(light.x, floor(dynamicLevel(cell)) * 16.0);
    }
    v_rel = relative;
    v_light = clamp(light / 240.0, 0.0, 1.0);
    v_tint = a_color0.rgb;
    v_color0 = vec4(a_color0.rgb, a_color0.a);
    v_texcoord0 = a_texcoord0;
    v_layer = layer;

    v_sunLight = vvMainLight();
    v_skyLight = vvSkyAmbient();
    v_haze = vvHaze(relative);

    float dist = length(relative);
    v_visibility = mix(1.0, u_chunk.x, clamp((dist - 16.0) / 16.0, 0.0, 1.0));
}
}

namespace fs_stage
{
static vec4 gl_FragColor;
static vec4 gl_FragCoord;
static vec4 v_color0;
static vec2 v_texcoord0;
static vec2 v_fog;
static float v_layer;
static float v_visibility;
static vec3 v_rel;
static vec2 v_light;
static vec3 v_tint;
static float v_foliage;
static vec3 v_sunLight;
static vec3 v_skyLight;
static vec4 v_haze;

void stageMain()
{
    vec4 tex = sampleBlock(v_texcoord0, v_layer);
    vec3 viewDir = normalize(v_rel);
    vec3 n = faceNormal(v_rel);
    if (tex.a * v_color0.a < 0.5)
        discard;
    float lava = isTile(v_layer, u_tiles.y);
    float leaves = step(0.5, v_foliage) * step(v_foliage, 1.5);
    vec4 color = vec4(vvLit(tex.rgb, v_tint, n, v_rel, v_light, leaves, lava * 0.9, v_sunLight, v_skyLight), tex.a * v_color0.a);
    color = mix(vec4(vvFogColor(v_haze), color.a), color, v_visibility);
    color.rgb = vvFog(color.rgb, v_fog, v_haze);
    gl_FragColor = color;
}
}

[shader("vertex")]
VsOut vsMain(VsIn input)
{
    vs_stage::a_position = input.a_position;
    vs_stage::a_color0 = input.a_color0;
    vs_stage::a_texcoord0 = input.a_texcoord0;
    vs_stage::a_texcoord1 = input.a_texcoord1;
    vs_stage::stageMain();
    VsOut output;
    output.clipPosition = vs_stage::gl_Position;
    output.v_color0 = vs_stage::v_color0;
    output.v_texcoord0 = vs_stage::v_texcoord0;
    output.v_fog = vs_stage::v_fog;
    output.v_layer = vs_stage::v_layer;
    output.v_visibility = vs_stage::v_visibility;
    output.v_rel = vs_stage::v_rel;
    output.v_light = vs_stage::v_light;
    output.v_tint = vs_stage::v_tint;
    output.v_foliage = vs_stage::v_foliage;
    output.v_sunLight = vs_stage::v_sunLight;
    output.v_skyLight = vs_stage::v_skyLight;
    output.v_haze = vs_stage::v_haze;
    return output;
}

[shader("fragment")]
float4 fsMain(FsIn input) : SV_Target
{
    fs_stage::gl_FragCoord = input.fragCoord;
    fs_stage::v_color0 = input.v_color0;
    fs_stage::v_texcoord0 = input.v_texcoord0;
    fs_stage::v_fog = input.v_fog;
    fs_stage::v_layer = input.v_layer;
    fs_stage::v_visibility = input.v_visibility;
    fs_stage::v_rel = input.v_rel;
    fs_stage::v_light = input.v_light;
    fs_stage::v_tint = input.v_tint;
    fs_stage::v_foliage = input.v_foliage;
    fs_stage::v_sunLight = input.v_sunLight;
    fs_stage::v_skyLight = input.v_skyLight;
    fs_stage::v_haze = input.v_haze;
    fs_stage::stageMain();
    return fs_stage::gl_FragColor;
}
