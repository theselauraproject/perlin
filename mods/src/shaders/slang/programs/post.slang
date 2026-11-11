#include "compat.slang"

UNIFORMS_BEGIN
    vec4 u_post;
    vec4 u_bloom;
    vec4 u_postFx;
    vec4 u_viewRect;
UNIFORMS_END

SAMPLER2D(s_cur, 28, 29);
SAMPLER2D(s_hist, 30, 31);
SAMPLER2D(s_bloom, 32, 33);

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

void stageMain()
{
    if (u_post.z > 0.01)
    {
        vec2 texel = vec2(1.0 / u_viewRect.z, 1.0 / u_viewRect.w);
        vec2 step = (u_post.w < 0.5 ? vec2(texel.x, 0.0) : vec2(0.0, texel.y)) * u_post.z * 0.25;
        vec3 sum = texture2D(s_cur, v_texcoord0).rgb * 0.2270270270;
        sum += (texture2D(s_cur, v_texcoord0 + step).rgb + texture2D(s_cur, v_texcoord0 - step).rgb) * 0.1945945946;
        sum += (texture2D(s_cur, v_texcoord0 + step * 2.0).rgb + texture2D(s_cur, v_texcoord0 - step * 2.0).rgb) * 0.1216216216;
        sum += (texture2D(s_cur, v_texcoord0 + step * 3.0).rgb + texture2D(s_cur, v_texcoord0 - step * 3.0).rgb) * 0.0540540541;
        sum += (texture2D(s_cur, v_texcoord0 + step * 4.0).rgb + texture2D(s_cur, v_texcoord0 - step * 4.0).rgb) * 0.0162162162;
        gl_FragColor = vec4(sum, 1.0);
        return;
    }
    vec2 uv = v_texcoord0;
    if (u_postFx.x > 0.0)
    {
        float t = u_postFx.y;
        vec2 wobble = vec2(sin(uv.y * 23.0 + t * 1.9) + 0.6 * sin(uv.y * 41.0 - t * 2.7 + uv.x * 9.0),
                           cos(uv.x * 19.0 + t * 1.6) + 0.6 * cos(uv.x * 37.0 + t * 2.3 - uv.y * 7.0));
        vec2 edge = min(uv, vec2_splat(1.0) - uv);
        float fade = smoothstep(0.0, 0.04, min(edge.x, edge.y));
        uv += wobble * 0.0021 * u_postFx.x * fade;
    }
    vec3 cur = texture2D(s_cur, uv).rgb;
    vec3 hist = texture2D(s_hist, uv).rgb;
    vec3 color = mix(cur, hist, u_post.x);
    if (u_bloom.x > 0.0)
    {
        vec3 glow = texture2D(s_bloom, uv).rgb * u_bloom.x;
        color = vec3_splat(1.0) - (vec3_splat(1.0) - color) * (vec3_splat(1.0) - clamp(glow, 0.0, 1.0));
    }
    // 8-bit framebuffer banding on smooth sky gradients: dither last
    color += (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * (1.5 / 255.0);
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
