#include "compat.slang"

UNIFORMS_BEGIN
    vec4 u_post;
    vec4 u_taa;
    vec4 u_taaSize;
    vec4 u_taaJit;
    mat4 u_taaInvVp;
    mat4 u_taaPrevVp;
UNIFORMS_END

SAMPLER2D(s_cur, 28, 29);
SAMPLER2D(s_hist, 30, 31);
SAMPLER2D(s_depth, 34, 35);

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

vec3 fetchCur(vec2 texel)
{
    vec2 uv = (texel + vec2_splat(0.5)) / u_taaSize.xy;
    return texture2DLod(s_cur, clamp(uv, vec2_splat(0.0), vec2_splat(1.0)), 0.0).rgb;
}

float fetchDepth(vec2 texel)
{
    vec2 uv = (texel + vec2_splat(0.5)) / u_taaSize.xy;
    return texture2DLod(s_depth, clamp(uv, vec2_splat(0.0), vec2_splat(1.0)), 0.0).x;
}

void stageMain()
{
    vec2 uv = v_texcoord0;
    vec2 low = u_taaSize.xy;
    vec2 centre = (uv + u_taaJit.xy) * low - vec2_splat(0.5);
    vec2 base = floor(centre);

    vec3 sum = vec3_splat(0.0);
    float wsum = 0.0;
    vec3 m1 = vec3_splat(0.0);
    vec3 m2 = vec3_splat(0.0);
    float nearest = 0.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec2 texel = base + vec2(float(x), float(y));
            vec3 c = fetchCur(texel);
            vec2 d = texel - centre;
            float w = exp(-1.6 * dot(d, d));
            sum += c * w;
            wsum += w;
            m1 += c;
            m2 += c * c;
            nearest = max(nearest, fetchDepth(texel));
        }
    }
    vec3 resolved = sum / max(wsum, 1.0e-4);
    // sky has no valid motion vector: history smears it into lines
    if (nearest > 0.9995) {
        gl_FragColor = vec4(resolved, 1.0);
        return;
    }
    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3_splat(0.0)));

    float ySign = u_taa.z;
    vec3 ndc = vec3(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * ySign, u_taa.w > 0.5 ? nearest * 2.0 - 1.0 : nearest);
    vec4 world = mul(u_taaInvVp, vec4(ndc, 1.0));
    world /= world.w;
    vec4 prev = mul(u_taaPrevVp, world);
    vec2 pn = prev.xy / prev.w;
    vec2 puv = vec2(pn.x * 0.5 + 0.5, 0.5 + ySign * pn.y * 0.5);

    bool ok = u_taa.y > 0.5 && prev.w > 0.0 && puv.x > 0.0 && puv.x < 1.0 && puv.y > 0.0 && puv.y < 1.0;
    if (!ok)
    {
        gl_FragColor = vec4(resolved, 1.0);
        return;
    }
    vec3 hist = texture2DLod(s_hist, puv, 0.0).rgb;
    vec3 lo = mean - 1.3 * sigma - vec3_splat(0.004);
    vec3 hi = mean + 1.3 * sigma + vec3_splat(0.004);
    vec3 clamped = clamp(hist, lo, hi);
    float rejected = clamp(length(hist - clamped) * 6.0, 0.0, 0.7);
    float alpha = clamp(u_taa.x + rejected, 0.0, 1.0);
    gl_FragColor = vec4(mix(clamped, resolved, alpha), 1.0);
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
