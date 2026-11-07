// Visual Vibrance shader (see LICENSE-VisualVibrance.txt)
#include "compat.slang"

#include "vv_resources.slang"
SAMPLER2D(s_sky, 22, 23);
#include "vv_common.slang"

struct VsIn
{
    [[vk::location(0)]] vec3 a_position : TEXCOORD0;
    [[vk::location(4)]] vec2 a_texcoord0 : TEXCOORD4;
};
struct VsOut
{
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] vec2 v_texcoord0 : TEXCOORD0;
};
struct FsIn
{
    float4 fragCoord : SV_Position;
    [[vk::location(0)]] vec2 v_texcoord0 : TEXCOORD0;
};

namespace vs_stage
{
static vec4 gl_Position;
static vec3 a_position;
static vec2 a_texcoord0;
static vec2 v_texcoord0;

void stageMain()
{
    gl_Position = mul(u_mvp, vec4(a_position, 1.0));
    v_texcoord0 = a_texcoord0;
}
}

namespace fs_stage
{
static vec4 gl_FragColor;
static vec4 gl_FragCoord;
static vec2 v_texcoord0;

void stageMain()
{
    if (u_skyParams.z > 0.5)
        discard;
    vec4 color = texture2D(s_sky, v_texcoord0);
    if (color.a == 0.0)
        discard;
    gl_FragColor = color * u_modulator;
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
    output.v_texcoord0 = vs_stage::v_texcoord0;
    return output;
}

[shader("fragment")]
float4 fsMain(FsIn input) : SV_Target
{
    fs_stage::gl_FragCoord = input.fragCoord;
    fs_stage::v_texcoord0 = input.v_texcoord0;
    fs_stage::stageMain();
    return fs_stage::gl_FragColor;
}
