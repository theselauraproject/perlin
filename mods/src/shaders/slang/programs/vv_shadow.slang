// Visual Vibrance shader (see LICENSE-VisualVibrance.txt)
#include "compat.slang"

#include "vv_resources.slang"
#include "vv_common.slang"

struct VsIn
{
    [[vk::location(0)]] vec3 a_position : TEXCOORD0;
    [[vk::location(4)]] vec2 a_texcoord0 : TEXCOORD4;
    [[vk::location(5)]] vec4 a_texcoord1 : TEXCOORD5;
};
struct VsOut
{
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] vec2 v_texcoord0 : TEXCOORD0;
    [[vk::location(1)]] float v_layer : TEXCOORD1;
    [[vk::location(2)]] float v_depth : TEXCOORD2;
};
struct FsIn
{
    float4 fragCoord : SV_Position;
    [[vk::location(0)]] vec2 v_texcoord0 : TEXCOORD0;
    [[vk::location(1)]] float v_layer : TEXCOORD1;
    [[vk::location(2)]] float v_depth : TEXCOORD2;
};

namespace vs_stage
{
static vec4 gl_Position;
static vec3 a_position;
static vec2 a_texcoord0;
static vec4 a_texcoord1;
static vec2 v_texcoord0;
static float v_layer;
static float v_depth;

void stageMain()
{
    float layer = floor(a_texcoord1.z * 255.0 + 0.5) + floor(a_texcoord1.w * 255.0 + 0.5) * 256.0;
    vec3 position = a_position;
    gl_Position = mul(u_mvp, vec4(position, 1.0));
    v_texcoord0 = a_texcoord0;
    v_layer = layer;
    v_depth = mul(u_shadowMtx, vec4(position, 1.0)).z;
}
}

namespace fs_stage
{
static vec4 gl_FragColor;
static vec4 gl_FragCoord;
static vec2 v_texcoord0;
static float v_layer;
static float v_depth;

void stageMain()
{
    gl_FragColor = vec4(v_depth, 0.0, 0.0, 1.0);
}
}

[shader("vertex")]
VsOut vsMain(VsIn input)
{
    vs_stage::a_position = input.a_position;
    vs_stage::a_texcoord0 = input.a_texcoord0;
    vs_stage::a_texcoord1 = input.a_texcoord1;
    vs_stage::stageMain();
    VsOut output;
    output.clipPosition = vs_stage::gl_Position;
    output.v_texcoord0 = vs_stage::v_texcoord0;
    output.v_layer = vs_stage::v_layer;
    output.v_depth = vs_stage::v_depth;
    return output;
}

[shader("fragment")]
float4 fsMain(FsIn input) : SV_Target
{
    fs_stage::gl_FragCoord = input.fragCoord;
    fs_stage::v_texcoord0 = input.v_texcoord0;
    fs_stage::v_layer = input.v_layer;
    fs_stage::v_depth = input.v_depth;
    fs_stage::stageMain();
    return fs_stage::gl_FragColor;
}
