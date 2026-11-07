#include "compat.slang"

UNIFORMS_BEGIN
    vec4 u_post;
    vec4 u_up;
    vec4 u_upSize;
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

vec3 fetchPx(vec2 px)
{
    vec2 size = u_upSize.xy;
    vec2 uv = clamp((px + vec2_splat(0.5)) / size, vec2_splat(0.0), vec2_splat(1.0));
    return texture2DLod(s_cur, uv, 0.0).rgb;
}

float lumaOf(vec3 c)
{
    return dot(c, vec3(0.5, 1.0, 0.5));
}

float pick(vec4 v, int i)
{
    if (i == 0) return v.x;
    if (i == 1) return v.y;
    if (i == 2) return v.z;
    return v.w;
}

vec3 bicubic(vec2 uv)
{
    vec2 ip = uv * u_upSize.xy - vec2_splat(0.5);
    vec2 base = floor(ip);
    vec2 f = ip - base;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec4 wx = vec4(w0.x, w1.x, w2.x, w3.x);
    vec4 wy = vec4(w0.y, w1.y, w2.y, w3.y);
    vec3 col = vec3_splat(0.0);
    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            vec2 px = base + vec2(float(x) - 1.0, float(y) - 1.0);
            col += fetchPx(px) * (pick(wx, x) * pick(wy, y));
        }
    }
    return max(col, vec3_splat(0.0));
}

vec4 easuTap(vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c)
{
    vec2 v;
    v.x = dot(off, dir);
    v.y = dot(off, vec2(-dir.y, dir.x));
    v *= len;
    float d2 = min(dot(v, v), clp);
    float wB = 2.0 / 5.0 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    return vec4(c * w, w);
}

vec3 easu(vec2 uv)
{
    vec2 ip = uv * u_upSize.xy;
    vec2 base = floor(ip - 0.5) + 0.5;
    vec2 f = ip - base;

    vec3 b = fetchPx(base + vec2(0.0, -1.0));
    vec3 c = fetchPx(base + vec2(1.0, -1.0));
    vec3 e = fetchPx(base + vec2(-1.0, 0.0));
    vec3 f0 = fetchPx(base + vec2(0.0, 0.0));
    vec3 g = fetchPx(base + vec2(1.0, 0.0));
    vec3 h = fetchPx(base + vec2(2.0, 0.0));
    vec3 i = fetchPx(base + vec2(-1.0, 1.0));
    vec3 j = fetchPx(base + vec2(0.0, 1.0));
    vec3 k = fetchPx(base + vec2(1.0, 1.0));
    vec3 l = fetchPx(base + vec2(2.0, 1.0));
    vec3 n = fetchPx(base + vec2(0.0, 2.0));
    vec3 o = fetchPx(base + vec2(1.0, 2.0));

    float bL = lumaOf(b), cL = lumaOf(c), eL = lumaOf(e), fL = lumaOf(f0);
    float gL = lumaOf(g), hL = lumaOf(h), iL = lumaOf(i), jL = lumaOf(j);
    float kL = lumaOf(k), lL = lumaOf(l), nL = lumaOf(n), oL = lumaOf(o);

    vec2 dir = vec2_splat(0.0);
    float len = 0.0;
    {
        float w = (1.0 - f.x) * (1.0 - f.y);
        dir += vec2(eL - gL, bL - jL) * w;
        len += max(max(abs(fL - eL), abs(fL - gL)), max(abs(fL - bL), abs(fL - jL))) * w;
    }
    {
        float w = f.x * (1.0 - f.y);
        dir += vec2(fL - hL, cL - kL) * w;
        len += max(max(abs(gL - fL), abs(gL - hL)), max(abs(gL - cL), abs(gL - kL))) * w;
    }
    {
        float w = (1.0 - f.x) * f.y;
        dir += vec2(iL - kL, fL - nL) * w;
        len += max(max(abs(jL - iL), abs(jL - kL)), max(abs(jL - fL), abs(jL - nL))) * w;
    }
    {
        float w = f.x * f.y;
        dir += vec2(jL - lL, gL - oL) * w;
        len += max(max(abs(kL - jL), abs(kL - lL)), max(abs(kL - gL), abs(kL - oL))) * w;
    }

    float dirR = max(abs(dir.x), abs(dir.y));
    if (dirR < 1.0 / 32768.0)
        dir = vec2(1.0, 0.0);
    else
        dir /= dirR;
    float dirLen = length(dir);
    dir = dirLen > 1.0e-5 ? dir / dirLen : vec2(1.0, 0.0);

    len = clamp(len * 2.0, 0.0, 1.0);
    len = len * len;

    vec2 len2 = vec2(1.0 + len, 1.0 - 0.5 * len);
    float lob = 0.5 - 0.29 * len;
    float clp = 1.0 / lob;

    vec4 acc = easuTap(vec2(0.0, -1.0) - f, dir, len2, lob, clp, b);
    acc += easuTap(vec2(1.0, -1.0) - f, dir, len2, lob, clp, c);
    acc += easuTap(vec2(-1.0, 0.0) - f, dir, len2, lob, clp, e);
    acc += easuTap(vec2(0.0, 0.0) - f, dir, len2, lob, clp, f0);
    acc += easuTap(vec2(1.0, 0.0) - f, dir, len2, lob, clp, g);
    acc += easuTap(vec2(2.0, 0.0) - f, dir, len2, lob, clp, h);
    acc += easuTap(vec2(-1.0, 1.0) - f, dir, len2, lob, clp, i);
    acc += easuTap(vec2(0.0, 1.0) - f, dir, len2, lob, clp, j);
    acc += easuTap(vec2(1.0, 1.0) - f, dir, len2, lob, clp, k);
    acc += easuTap(vec2(2.0, 1.0) - f, dir, len2, lob, clp, l);
    acc += easuTap(vec2(0.0, 2.0) - f, dir, len2, lob, clp, n);
    acc += easuTap(vec2(1.0, 2.0) - f, dir, len2, lob, clp, o);

    vec3 color = acc.w > 1.0e-5 ? acc.xyz / acc.w : f0;
    vec3 mn = min(min(f0, g), min(j, k));
    vec3 mx = max(max(f0, g), max(j, k));
    return clamp(color, mn, mx);
}

vec3 sharpen(vec2 uv)
{
    vec2 texel = vec2(1.0, 1.0) / u_upSize.zw;
    vec3 e = texture2DLod(s_cur, uv, 0.0).rgb;
    float sharpness = u_up.y;
    if (sharpness <= 0.0001)
        return e;
    vec3 b = texture2DLod(s_cur, uv + vec2(0.0, -texel.y), 0.0).rgb;
    vec3 d = texture2DLod(s_cur, uv + vec2(-texel.x, 0.0), 0.0).rgb;
    vec3 f = texture2DLod(s_cur, uv + vec2(texel.x, 0.0), 0.0).rgb;
    vec3 h = texture2DLod(s_cur, uv + vec2(0.0, texel.y), 0.0).rgb;
    float peak = 0.8 * clamp(sharpness, 0.0, 1.0);
    vec3 mn = min(min(b, d), min(f, h));
    vec3 mx = max(max(b, d), max(f, h));
    float contrast = lumaOf(max(mx, e)) - lumaOf(min(mn, e));
    vec3 limit = min(e - mn, mx - e) / max(vec3_splat(contrast), vec3_splat(1.0e-4));
    float w = clamp(min(min(limit.x, limit.y), limit.z), 0.0, 1.0) * peak;
    vec3 sharp = (b + d + f + h) * (-w) + e * (1.0 + 4.0 * w);
    return clamp(sharp, min(mn, e), max(mx, e));
}

void stageMain()
{
    vec3 color;
    if (u_up.x < 0.5)
        color = bicubic(v_texcoord0);
    else if (u_up.x < 1.5)
        color = easu(v_texcoord0);
    else if (u_up.x < 2.5)
        color = sharpen(v_texcoord0);
    else
        color = texture2DLod(s_cur, v_texcoord0, 0.0).rgb;
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
