// Visual Vibrance shader (see LICENSE-VisualVibrance.txt)
#include "compat.slang"

#include "vv_resources.slang"
#include "vv_common.slang"

struct VsIn
{
    [[vk::location(0)]] vec3 a_position : TEXCOORD0;
    [[vk::location(4)]] vec2 a_texcoord0 : TEXCOORD4;
};
struct VsOut
{
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] vec4 v_color0 : TEXCOORD0;
    [[vk::location(1)]] float v_dist : TEXCOORD1;
    [[vk::location(2)]] vec3 v_dir : TEXCOORD2;
};
struct FsIn
{
    float4 fragCoord : SV_Position;
    [[vk::location(0)]] vec4 v_color0 : TEXCOORD0;
    [[vk::location(1)]] float v_dist : TEXCOORD1;
    [[vk::location(2)]] vec3 v_dir : TEXCOORD2;
};

namespace vs_stage
{
static vec4 gl_Position;
static vec3 a_position;
static vec2 a_texcoord0;
static vec4 v_color0;
static float v_dist;
static vec3 v_dir;

void stageMain()
{
    vec3 pos = a_position + u_cloudOffset.xyz;
    gl_Position = mul(u_mvp, vec4(pos, 1.0));
    v_dist = length(pos);
    v_dir = pos / max(length(pos), 0.0001);
    v_color0 = vec4(a_texcoord0.x * u_cloudColor.rgb, u_cloudColor.a);
}
}

namespace fs_stage
{
static vec4 gl_FragColor;
static vec4 gl_FragCoord;
static vec4 v_color0;
static float v_dist;
static vec3 v_dir;

void stageMain()
{
    vec3 body, rim;
    vvCloudColors(body, rim);
    vec3 c = body * mix(0.85, 1.0, v_color0.r) + vvSunGlow(normalize(v_dir)) * 0.5;
    gl_FragColor = vec4(c, v_color0.a * (1.0 - linearFog(v_dist, 0.0, max(u_fogSky.y, 0.001))));
}
}

[shader("vertex")]
VsOut vsMain(VsIn input)
{
    vs_stage::a_position = input.a_position;
    vs_stage::a_texcoord0 = input.a_texcoord0;
    vs_stage::stageMain();
    VsOut output;
    output.clipPosition = vs_stage::gl_Position;
    output.v_color0 = vs_stage::v_color0;
    output.v_dist = vs_stage::v_dist;
    output.v_dir = vs_stage::v_dir;
    return output;
}

[shader("fragment")]
float4 fsMain(FsIn input) : SV_Target
{
    fs_stage::gl_FragCoord = input.fragCoord;
    fs_stage::v_color0 = input.v_color0;
    fs_stage::v_dist = input.v_dist;
    fs_stage::v_dir = input.v_dir;
    fs_stage::stageMain();
    return fs_stage::gl_FragColor;
}
