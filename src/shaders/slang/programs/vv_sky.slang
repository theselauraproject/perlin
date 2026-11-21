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
    vec3 p = a_position;
    float r = length(p.xz);
    if (p.y > 0.0 && r > 1.0)
        p.y = -r * 0.3;
    gl_Position = mul(u_mvp, vec4(p, 1.0));
    gl_Position.z = 0.0;
    v_fog = vec2(length(p), max(length(p.xz), abs(p.y)));
    v_dir = p;
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
        vec3 flatDir = normalize(vec3(dir.x, 1.0e-4, dir.z));
        c = vvSky(flatDir, false) * mix(0.72, 1.0, smoothstep(-0.5, -0.04, dir.y));
    }
    else
    {
        c = vvSky(dir, true) * mix(0.72, 1.0, smoothstep(-0.5, -0.04, dir.y));
        float dither = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
        float steps = 6.0 + 4.0 * floor(S_CLOUD_QUALITY + 0.5);
        vec4 cloud = vvCloudMarch(u_camera.xyz, dir, 6000.0, steps, dither);
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
