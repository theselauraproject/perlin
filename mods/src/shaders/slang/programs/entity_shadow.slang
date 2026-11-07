#include "compat.slang"

UNIFORMS_BEGIN
    mat4 u_mvp;
    mat4 u_shadowMtx;
    mat4 u_model[1];
UNIFORMS_END

SAMPLER2D(s_uiTex, 24, 25);

struct VsIn
{
    [[vk::location(0)]] vec3 a_position : TEXCOORD0;
    [[vk::location(2)]] vec4 a_color0 : TEXCOORD2;
    [[vk::location(4)]] vec2 a_texcoord0 : TEXCOORD4;
};
struct VsOut
{
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] vec2 v_texcoord0 : TEXCOORD0;
    [[vk::location(1)]] float v_depth : TEXCOORD1;
};
struct FsIn
{
    float4 fragCoord : SV_Position;
    [[vk::location(0)]] vec2 v_texcoord0 : TEXCOORD0;
    [[vk::location(1)]] float v_depth : TEXCOORD1;
};

namespace vs_stage
{
static vec4 gl_Position;
static vec3 a_position;
static vec4 a_color0;
static vec2 a_texcoord0;
static vec2 v_texcoord0;
static float v_depth;

void stageMain()
{
    vec4 world = mul(u_model[0], vec4(a_position, 1.0));
    gl_Position = mul(u_mvp, world);
    v_texcoord0 = a_texcoord0;
    v_depth = mul(u_shadowMtx, world).z;
}
}

namespace fs_stage
{
static vec4 gl_FragColor;
static vec4 gl_FragCoord;
static vec2 v_texcoord0;
static float v_depth;

void stageMain()
{
    if (texture2D(s_uiTex, v_texcoord0).a < 0.5)
        discard;
    gl_FragColor = vec4(v_depth, 0.0, 0.0, 1.0);
}
}

[shader("vertex")]
VsOut vsMain(VsIn input)
{
    vs_stage::a_position = input.a_position;
    vs_stage::a_color0 = input.a_color0;
    vs_stage::a_texcoord0 = input.a_texcoord0;
    vs_stage::stageMain();
    VsOut output;
    output.clipPosition = vs_stage::gl_Position;
    output.v_texcoord0 = vs_stage::v_texcoord0;
    output.v_depth = vs_stage::v_depth;
    return output;
}

[shader("fragment")]
float4 fsMain(FsIn input) : SV_Target
{
    fs_stage::gl_FragCoord = input.fragCoord;
    fs_stage::v_texcoord0 = input.v_texcoord0;
    fs_stage::v_depth = input.v_depth;
    fs_stage::stageMain();
    return fs_stage::gl_FragColor;
}
