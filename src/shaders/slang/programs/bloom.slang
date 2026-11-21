#include "compat.slang"

UNIFORMS_BEGIN
    vec4 u_post;
    vec4 u_bloomParams;
    vec4 u_bloomTexel;
    vec4 u_rays;
    vec4 u_raysColor;
UNIFORMS_END

SAMPLER2D(s_cur, 28, 29);

struct VsIn
{
    [[vk::location(0)]] vec3 a_position : TEXCOORD0;
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
static vec2 v_texcoord0;

void stageMain()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    vec2 uv = a_position.xy * 0.5 + 0.5;
    uv.y = u_post.y > 0.5 ? uv.y : 1.0 - uv.y;
    v_texcoord0 = uv;
}
}

namespace fs_stage
{
static vec4 gl_FragColor;
static vec4 gl_FragCoord;
static vec2 v_texcoord0;

vec3 sampleSrc(vec2 uv)
{
    return texture2DLod(s_cur, uv, 0.0).rgb;
}

vec3 downsample(vec2 uv, vec2 t)
{
    vec3 a = sampleSrc(uv + t * vec2(-2.0, -2.0));
    vec3 b = sampleSrc(uv + t * vec2(0.0, -2.0));
    vec3 c = sampleSrc(uv + t * vec2(2.0, -2.0));
    vec3 d = sampleSrc(uv + t * vec2(-1.0, -1.0));
    vec3 e = sampleSrc(uv + t * vec2(1.0, -1.0));
    vec3 f = sampleSrc(uv + t * vec2(-2.0, 0.0));
    vec3 g = sampleSrc(uv);
    vec3 h = sampleSrc(uv + t * vec2(2.0, 0.0));
    vec3 i = sampleSrc(uv + t * vec2(-1.0, 1.0));
    vec3 j = sampleSrc(uv + t * vec2(1.0, 1.0));
    vec3 k = sampleSrc(uv + t * vec2(-2.0, 2.0));
    vec3 l = sampleSrc(uv + t * vec2(0.0, 2.0));
    vec3 m = sampleSrc(uv + t * vec2(2.0, 2.0));
    vec3 o = (d + e + i + j) * 0.125;
    o += (a + b + g + f) * 0.03125;
    o += (b + c + h + g) * 0.03125;
    o += (f + g + l + k) * 0.03125;
    o += (g + h + m + l) * 0.03125;
    return o;
}

vec3 upsample(vec2 uv, vec2 t)
{
    vec3 s = sampleSrc(uv) * 4.0;
    s += (sampleSrc(uv + t * vec2(-1.0, 0.0)) + sampleSrc(uv + t * vec2(1.0, 0.0)) + sampleSrc(uv + t * vec2(0.0, -1.0)) +
          sampleSrc(uv + t * vec2(0.0, 1.0))) * 2.0;
    s += sampleSrc(uv + t * vec2(-1.0, -1.0)) + sampleSrc(uv + t * vec2(1.0, -1.0)) + sampleSrc(uv + t * vec2(-1.0, 1.0)) +
         sampleSrc(uv + t * vec2(1.0, 1.0));
    return s / 16.0;
}

void stageMain()
{
    vec2 t = u_bloomTexel.xy;
    vec3 color;
    if (u_bloomParams.x < 0.5)
    {
        color = downsample(v_texcoord0, t);
        float br = max(color.r, max(color.g, color.b));
        float knee = max(u_bloomParams.z, 1.0e-4);
        float soft = clamp(br - u_bloomParams.y + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee);
        float contrib = max(soft, br - u_bloomParams.y) / max(br, 1.0e-4);
        color *= contrib;
    }
    else if (u_bloomParams.x < 1.5)
    {
        color = downsample(v_texcoord0, t);
    }
    else if (u_bloomParams.x < 2.5)
    {
        color = upsample(v_texcoord0, t);
    }
    else
    {
        vec2 toSun = u_rays.xy - v_texcoord0;
        float aspect = u_bloomTexel.z;
        float reach = length(vec2(toSun.x * aspect, toSun.y));
        float dither = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
        vec2 stepUv = toSun * (0.92 / 16.0);
        vec2 uv = v_texcoord0 + stepUv * dither;
        vec3 sum = vec3_splat(0.0);
        float w = 1.0;
        float total = 0.0;
        for (int i = 0; i < 16; i++)
        {
            vec3 s = sampleSrc(uv);
            sum += s * w;
            total += w;
            w *= 1.06;
            uv += stepUv;
        }
        float falloff = exp(-reach * u_rays.w);
        color = sum / total * u_raysColor.rgb * u_rays.z * falloff;
    }
    gl_FragColor = vec4(color, 1.0);
}
}

[shader("vertex")]
VsOut vsMain(VsIn input)
{
    vs_stage::a_position = input.a_position;
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
