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
    v_fog = vec2(length(a_position), max(length(a_position.xz), abs(a_position.y)));
    v_dir = a_position;
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
    vec3 c;
    if (u_skyParams.z < 0.5)
    {
        c = mix(u_fogColor.rgb, u_modulator.rgb, smoothstep(-0.05, 0.6, dir.y));
    }
    else if (u_modulator.a > 0.5 && dot(u_modulator.rgb, vec3_splat(1.0)) < 0.001 && dir.y < 0.0)
    {
        c = vvSkyGradient(vec3(dir.x, 0.0, dir.z)) * mix(0.85, 1.0, smoothstep(-0.4, -0.08, dir.y));
        c += vvSunGlow(normalize(vec3(dir.x, 0.0, dir.z) + vec3(0.0, 1.0e-4, 0.0)));
    }
    else
    {
        c = vvSky(dir, true);
        vec4 cloud = vvClouds(u_camera.xyz, dir, 6000.0);
        c = c * cloud.a + cloud.rgb;
    }
    float underwater = vvUnderwater();
    vec3 waterCol = vvUnderwaterColor();
    c = mix(c, mix(waterCol, c * vec3(0.55, 0.85, 0.95), smoothstep(0.55, 0.8, dir.y)), underwater);
    gl_FragColor = vec4(c, u_modulator.a);
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
