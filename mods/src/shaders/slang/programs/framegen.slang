#include "compat.slang"

UNIFORMS_BEGIN
    vec4 u_post;
    vec4 u_gen;
    mat4 u_genInvVp;
    mat4 u_genCurVp;
UNIFORMS_END

SAMPLER2D(s_depth, 34, 35);
SAMPLER2D(s_world, 36, 37);
SAMPLER2D(s_final, 38, 39);

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

vec2 uvOf(vec3 ndc)
{
    return vec2(ndc.x * 0.5 + 0.5, 0.5 + u_gen.z * ndc.y * 0.5);
}

float rawDepth(vec2 uv)
{
    return texture2DLod(s_depth, clamp(uv, vec2_splat(0.0), vec2_splat(1.0)), 0.0).x;
}

float depthAt(vec2 uv)
{
    float d = rawDepth(uv);
    return u_gen.w > 0.5 ? d * 2.0 - 1.0 : d;
}

float linearDepth(float d)
{
    float n = u_gen.x;
    float f = u_gen.y;
    return n * f / (n + d * (f - n));
}

void stageMain()
{
    vec2 uv = v_texcoord0;
    vec3 shown = texture2DLod(s_final, uv, 0.0).rgb;
    vec3 world = texture2DLod(s_world, uv, 0.0).rgb;
    vec3 diff = abs(shown - world);
    if (max(diff.x, max(diff.y, diff.z)) > 0.006)
    {
        gl_FragColor = vec4(shown, 1.0);
        return;
    }
    if (linearDepth(rawDepth(uv)) < 0.13)
    {
        gl_FragColor = vec4(shown, 1.0);
        return;
    }
    vec2 ndcXY = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * u_gen.z);
    vec2 src = uv;
    float expected = 1.0;
    for (int i = 0; i < 4; i++)
    {
        vec4 w = mul(u_genInvVp, vec4(ndcXY, depthAt(src), 1.0));
        w /= w.w;
        vec4 c = mul(u_genCurVp, w);
        expected = c.w;
        src = uvOf(vec3(c.xy / max(abs(c.w), 1.0e-5) * sign(c.w), 0.0));
    }
    bool inside = src.x > 0.002 && src.x < 0.998 && src.y > 0.002 && src.y < 0.998;
    float actual = linearDepth(rawDepth(src));
    bool consistent = inside && expected > 0.0 && abs(actual - expected) < 0.05 * expected + 0.12;
    vec2 pick = consistent ? src : uv;
    if (!consistent && inside)
    {
        vec2 motion = src - uv;
        vec2 ahead = clamp(src + motion * 0.5, vec2_splat(0.0), vec2_splat(1.0));
        vec2 behind = clamp(src - motion * 0.5, vec2_splat(0.0), vec2_splat(1.0));
        float best = actual;
        pick = src;
        float aheadDepth = linearDepth(rawDepth(ahead));
        if (aheadDepth > best)
        {
            best = aheadDepth;
            pick = ahead;
        }
        float behindDepth = linearDepth(rawDepth(behind));
        if (behindDepth > best)
        {
            pick = behind;
        }
    }
    gl_FragColor = vec4(texture2DLod(s_world, pick, 0.0).rgb, 1.0);
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
