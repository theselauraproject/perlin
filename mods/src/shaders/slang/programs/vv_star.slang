// Visual Vibrance shader (see LICENSE-VisualVibrance.txt)
#include "compat.slang"

#include "vv_resources.slang"
#include "vv_common.slang"

struct VsIn
{
    [[vk::location(0)]] vec3 a_position : TEXCOORD0;
};
struct VsOut
{
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] vec2 v_fog : TEXCOORD0;
    [[vk::location(1)]] vec3 v_dir : TEXCOORD1;
};
struct FsIn
{
    float4 fragCoord : SV_Position;
    [[vk::location(0)]] vec2 v_fog : TEXCOORD0;
    [[vk::location(1)]] vec3 v_dir : TEXCOORD1;
};

namespace vs_stage
{
static vec4 gl_Position;
static vec3 a_position;
static vec2 v_fog;
static vec3 v_dir;

void stageMain()
{
    gl_Position = mul(u_mvp, vec4(a_position, 1.0));
    v_fog = vec2(0.0, 0.0);
    v_dir = a_position / max(length(a_position), 0.0001);
}
}

namespace fs_stage
{
static vec4 gl_FragColor;
static vec4 gl_FragCoord;
static vec2 v_fog;
static vec3 v_dir;

void stageMain()
{
    vec3 dir = normalize(v_dir);
    vec2 cell = floor(vec2(atan2(dir.x, dir.z) * 90.0, dir.y * 180.0));
    float seed = hash12(cell);
    float twinkle = 0.8 + 0.2 * sin(u_time.x * (1.2 + seed * 3.0) + seed * 40.0);
    float dark = 1.0 - smoothstep(-0.12, 0.05, u_sunDir.y);
    vec3 tint = mix(vec3(0.85, 0.90, 1.0), vec3(1.0, 0.95, 0.85), step(0.7, hash12(cell + 7.0)));
    float b = clamp(u_modulator.a * dark * twinkle * u_time.y * S_STARS, 0.0, 1.0);
    gl_FragColor = vec4(tint * b * 1.1, b);
}
}

[shader("vertex")]
VsOut vsMain(VsIn input)
{
    vs_stage::a_position = input.a_position;
    vs_stage::stageMain();
    VsOut output;
    output.clipPosition = vs_stage::gl_Position;
    output.v_fog = vs_stage::v_fog;
    output.v_dir = vs_stage::v_dir;
    return output;
}

[shader("fragment")]
float4 fsMain(FsIn input) : SV_Target
{
    fs_stage::gl_FragCoord = input.fragCoord;
    fs_stage::v_fog = input.v_fog;
    fs_stage::v_dir = input.v_dir;
    fs_stage::stageMain();
    return fs_stage::gl_FragColor;
}
