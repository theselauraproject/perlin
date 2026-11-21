#include "compat.slang"

#include "vv_resources.slang"
SAMPLER2D(s_sceneColor, 8, 9);
SAMPLER2D(s_sceneDepth, 10, 11);
#include "vv_common.slang"
#include "vv_terrain_common.slang"

struct VsIn
{
    [[vk::location(0)]] vec3 a_position : TEXCOORD0;
    [[vk::location(2)]] vec4 a_color0 : TEXCOORD2;
    [[vk::location(4)]] vec2 a_texcoord0 : TEXCOORD4;
    [[vk::location(5)]] vec4 a_texcoord1 : TEXCOORD5;
};
struct VsOut
{
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] vec4 v_color0 : TEXCOORD0;
    [[vk::location(1)]] vec2 v_texcoord0 : TEXCOORD1;
    [[vk::location(2)]] vec2 v_fog : TEXCOORD2;
    [[vk::location(3)]] float v_layer : TEXCOORD3;
    [[vk::location(4)]] float v_visibility : TEXCOORD4;
    [[vk::location(5)]] vec3 v_rel : TEXCOORD5;
    [[vk::location(6)]] vec2 v_light : TEXCOORD6;
    [[vk::location(7)]] vec3 v_tint : TEXCOORD7;
    [[vk::location(8)]] float v_foliage : TEXCOORD8;
    [[vk::location(9)]] vec3 v_sunLight : TEXCOORD9;
    [[vk::location(10)]] vec3 v_skyLight : TEXCOORD10;
    [[vk::location(11)]] vec4 v_haze : TEXCOORD11;
};
struct FsIn
{
    float4 fragCoord : SV_Position;
    [[vk::location(0)]] vec4 v_color0 : TEXCOORD0;
    [[vk::location(1)]] vec2 v_texcoord0 : TEXCOORD1;
    [[vk::location(2)]] vec2 v_fog : TEXCOORD2;
    [[vk::location(3)]] float v_layer : TEXCOORD3;
    [[vk::location(4)]] float v_visibility : TEXCOORD4;
    [[vk::location(5)]] vec3 v_rel : TEXCOORD5;
    [[vk::location(6)]] vec2 v_light : TEXCOORD6;
    [[vk::location(7)]] vec3 v_tint : TEXCOORD7;
    [[vk::location(8)]] float v_foliage : TEXCOORD8;
    [[vk::location(9)]] vec3 v_sunLight : TEXCOORD9;
    [[vk::location(10)]] vec3 v_skyLight : TEXCOORD10;
    [[vk::location(11)]] vec4 v_haze : TEXCOORD11;
};

namespace vs_stage
{
static vec4 gl_Position;
static vec3 a_position;
static vec4 a_color0;
static vec2 a_texcoord0;
static vec4 a_texcoord1;
static vec4 v_color0;
static vec2 v_texcoord0;
static vec2 v_fog;
static float v_layer;
static float v_visibility;
static vec3 v_rel;
static vec2 v_light;
static vec3 v_tint;
static float v_foliage;
static vec3 v_sunLight;
static vec3 v_skyLight;
static vec4 v_haze;

void stageMain()
{
    float layer = floor(a_texcoord1.z * 255.0 + 0.5) + floor(a_texcoord1.w * 255.0 + 0.5) * 256.0;
    float foliage = vvFoliageKind(layer);
    vec3 position = a_position;
    if (S_WAVING > 0.001 && foliage > 0.5)
        position += vvWave(a_position, foliage, a_texcoord0);
    vec3 relative = position - u_camera.xyz;
    gl_Position = mul(u_mvp, vec4(position, 1.0));
    v_foliage = foliage;

    v_fog = vec2(length(relative), max(length(relative.xz), abs(relative.y)));
    vec2 light = floor(a_texcoord1.xy * 255.0 + 0.5);
    if (u_dynParams.x > 0.5)
    {
        vec3 cell = floor(a_position) + vec3_splat(0.5) - u_camera.xyz;
        light.x = max(light.x, floor(dynamicLevel(cell)) * 16.0);
    }
    v_rel = relative;
    v_light = clamp(light / 240.0, 0.0, 1.0);
    v_tint = a_color0.rgb;
    v_color0 = vec4(a_color0.rgb, a_color0.a);
    v_texcoord0 = a_texcoord0;
    v_layer = layer;

    v_sunLight = vvMainLight();
    v_skyLight = vvSkyAmbient();
    v_haze = vvHaze(relative);

    float dist = length(relative);
    v_visibility = mix(1.0, u_chunk.x, clamp((dist - 16.0) / 16.0, 0.0, 1.0));
}
}

namespace fs_stage
{
static vec4 gl_FragColor;
static vec4 gl_FragCoord;
static vec4 v_color0;
static vec2 v_texcoord0;
static vec2 v_fog;
static float v_layer;
static float v_visibility;
static vec3 v_rel;
static vec2 v_light;
static vec3 v_tint;
static float v_foliage;
static vec3 v_sunLight;
static vec3 v_skyLight;
static vec4 v_haze;

#define WATER_EXTINCTION (vec3(0.46, 0.075, 0.11) / max(S_WATER_CLARITY, 0.1))
#define WATER_DEFAULT_TINT vec3(0.247, 0.463, 0.894)

vec2 ndcToUv(vec2 ndc)
{
    return vec2(ndc.x * 0.5 + 0.5, u_sceneInfo.z > 0.5 ? ndc.y * 0.5 + 0.5 : 0.5 - ndc.y * 0.5);
}

vec2 uvToNdc(vec2 uv)
{
    return vec2(uv.x * 2.0 - 1.0, u_sceneInfo.z > 0.5 ? uv.y * 2.0 - 1.0 : 1.0 - uv.y * 2.0);
}

float clipDepthToTexture(float z)
{
    return u_sceneInfo.y > 0.5 ? z * 0.5 + 0.5 : z;
}

vec3 sceneWorld(vec2 uv, float depth)
{
    float z = u_sceneInfo.y > 0.5 ? depth * 2.0 - 1.0 : depth;
    vec4 p = mul(u_invMvp, vec4(uvToNdc(uv), z, 1.0));
    return p.xyz / p.w;
}

float sceneDepthAt(vec2 uv)
{
    return texture2DLod(s_sceneDepth, uv, 0.0).r;
}

vec3 sceneColorAt(vec2 uv)
{
    return texture2DLod(s_sceneColor, uv, 0.0).rgb;
}

float edgeFade(vec2 uv)
{
    vec2 e = min(uv, vec2_splat(1.0) - uv);
    return smoothstep(0.0, 0.07, min(e.x, e.y));
}

vec4 traceReflection(vec3 origin, vec3 dir, float jitter, float quality)
{
    vec3 cam = u_camera.xyz;
    float maxSteps = quality < 1.5 ? 12.0 : (quality < 2.5 ? 20.0 : 30.0);
    float stepLen = quality < 1.5 ? 0.6 : (quality < 2.5 ? 0.45 : 0.35);
    float growth = quality < 1.5 ? 1.32 : (quality < 2.5 ? 1.25 : 1.2);
    float t = 0.15 + jitter * stepLen;
    float prevT = 0.0;
    float hitT = -1.0;
    VV_LOOP for (int i = 0; i < 30; i++)
    {
        if (float(i) >= maxSteps)
            break;
        vec3 q = origin + dir * t;
        vec4 clip = mul(u_mvp, vec4(q, 1.0));
        vec3 ndc = clip.xyz / max(clip.w, 0.05);
        vec2 uv = ndcToUv(ndc.xy);
        if (clip.w <= 0.05 || uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)
            break;
        float sceneD = sceneDepthAt(uv);
        float rayD = clipDepthToTexture(ndc.z);
        if (rayD < sceneD && sceneD > 0.00001)
        {
            float behind = length(q - cam) - length(sceneWorld(uv, sceneD) - cam);
            if (behind < stepLen * 2.0 + 0.6)
            {
                hitT = t;
                break;
            }
        }
        prevT = t;
        t += stepLen;
        stepLen *= growth;
    }

    vec4 result = vec4(0.0, 0.0, 0.0, 0.0);
    VV_BRANCH if (hitT > 0.0)
    {
        float lo = prevT;
        float hi = hitT;
        VV_LOOP for (int k = 0; k < 5; k++)
        {
            float mid = (lo + hi) * 0.5;
            vec4 c2 = mul(u_mvp, vec4(origin + dir * mid, 1.0));
            vec3 n2 = c2.xyz / max(c2.w, 0.05);
            vec2 uv2 = ndcToUv(n2.xy);
            if (clipDepthToTexture(n2.z) < sceneDepthAt(uv2))
                hi = mid;
            else
                lo = mid;
        }
        vec4 cf = mul(u_mvp, vec4(origin + dir * hi, 1.0));
        vec2 uvf = ndcToUv(cf.xy / max(cf.w, 0.05));
        result = vec4(sceneColorAt(uvf), edgeFade(uvf));
    }
    else
    {
        vec4 far = mul(u_mvp, vec4(origin + dir * 3000.0, 1.0));
        vec2 uv = ndcToUv(far.xy / max(far.w, 0.05));
        if (far.w > 0.05 && uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0 && sceneDepthAt(uv) <= 0.00001)
            result = vec4(sceneColorAt(uv), edgeFade(uv) * 0.999);
    }
    return result;
}

vec2 waveSet(vec2 p, float t, vec2 d0, vec2 d1, float k, float a, float w)
{
    float p0 = dot(p, d0) * k + t * w;
    float p1 = dot(p, d1) * k * 1.37 - t * w * 1.21;
    return d0 * (cos(p0) * a * k) + d1 * (cos(p1) * a * 0.7 * k * 1.37);
}

vec2 waveSlope(vec2 p, float t, float detail)
{
    vec2 q = p + vec2(sin(p.y * 0.21 + t * 0.30), sin(p.x * 0.17 - t * 0.25)) * 1.3;
    vec2 g = waveSet(q, t, vec2(0.80, 0.60), vec2(-0.45, 0.89), 0.42, 0.55, 0.9) * 2.0;
    g += waveSet(q * 1.0 + vec2(3.1, 7.7), t, vec2(-0.96, 0.28), vec2(0.31, -0.95), 1.35, 0.17, 1.7) * 1.7;
    g += waveSet(q + vec2(11.3, 2.9), t, vec2(0.55, -0.83), vec2(0.99, 0.12), 3.6, 0.045, 2.9) * 0.75 * detail;
    return g;
}

void stageMain()
{
    vec4 tex = sampleBlock(v_texcoord0, v_layer);
    vec4 color = vec4(tex.rgb * v_color0.rgb, tex.a * v_color0.a);
    vec3 viewDir = normalize(v_rel);
    vec3 n = faceNormal(v_rel);
    color = mix(vec4(vvFogColor(v_haze), color.a), color, v_visibility);
    if (color.a < 0.1)
        discard;

    float up = smoothstep(0.85, 0.98, abs(n.y));
    float tileWater = max(isTile(v_layer, u_tiles.x), isTile(v_layer, u_tiles.z));
    float isWater = u_tiles.x >= 0.0 ? tileWater : up * step(v_tint.r * 1.25, v_tint.b);

    vec4 outColor;
    VV_BRANCH if (isWater < 0.5)
    {
#ifndef VV_DX_NO_LIT
        vec3 lit = vvLit(tex.rgb, v_tint, n, v_rel, v_light, 0.0, 0.0, v_sunLight, v_skyLight);
#else
        vec3 litAlbedo = vvPow(clamp(tex.rgb * v_tint, 0.0, 1.0), vec3_splat(2.2));
        vec3 lit = vvTonemap(litAlbedo * (v_sunLight * 0.5 * v_light.y + v_skyLight * v_light.y * v_light.y + vec3_splat(0.02)));
#endif
        outColor = vec4(vvFog(lit, v_fog, v_haze), color.a);
    }
    else
    {
        vec3 world = v_rel + u_camera.xyz;
        float dist = v_fog.x;
        float t = u_time.x;
        bool top = abs(n.y) > 0.7;
        float envUnderwater = vvUnderwater();
        vec3 N = dot(n, viewDir) > 0.0 ? -n : n;
        vec3 T = top ? vec3(1.0, 0.0, 0.0) : normalize(cross(vec3(0.0, 1.0, 0.0), N));
        vec3 B = top ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
        bool fromAbove = top ? viewDir.y < 0.0 : envUnderwater < 0.5;
        vec2 pw = top ? world.xz : vec2(dot(world, T) * 1.6, world.y * 0.45 + t * 1.8);

        float skyl = v_light.y;
        float detail = 1.0 - smoothstep(10.0, 40.0, dist);
        vec2 slope = waveSlope(pw, t, detail);
        float cosN = clamp(dot(-viewDir, N), 0.0, 1.0);
        float flatten = 1.0 - 0.7 * vvPow(1.0 - cosN, 5.0);
        float strength = (top ? 0.17 : 0.22) * S_WATER_WAVES * flatten * mix(0.35, 1.0, smoothstep(0.3, 0.8, skyl)) * (1.0 - smoothstep(24.0, 128.0, dist) * 0.8);
        vec3 wn = normalize(N - (T * slope.x + B * slope.y) * strength);

        float cosV = clamp(dot(-viewDir, wn), 0.0, 1.0);
        float fresnel = clamp(0.03 + 0.97 * vvPow(1.0 - cosV, 4.0), 0.0, 1.0);
        vec3 calmN = normalize(mix(N, wn, 0.45));
        vec3 R = reflect(viewDir, calmN);
        if (top && fromAbove)
            R.y = max(abs(R.y), 0.002);

        float skyVisible = mix(0.15, 1.0, smoothstep(0.35, 0.9, skyl));
        bool scene = u_sceneInfo.x > 0.5;

        bool wantReflection = fromAbove && fresnel > 0.05;
        vec4 hit = vec4(0.0, 0.0, 0.0, 0.0);
#ifndef VV_DX_NO_SSR
        float reflQuality = floor(S_REFLECTIONS + 0.5);
        VV_BRANCH if (wantReflection && top && scene && reflQuality > 0.5 && dist < 96.0)
        {
            float jitter = hash12(gl_FragCoord.xy + vec2(fract(t * 7.0) * 61.0, 0.0));
            hit = traceReflection(world, R, jitter, reflQuality);
        }
#endif
        vec3 reflection = vec3(0.0, 0.0, 0.0);
        VV_BRANCH if (wantReflection && hit.a < 0.999)
        {
#ifndef VV_DX_NO_SKY
            reflection = vvSky(R, false) * skyVisible;
#else
            reflection = vvTonemap(v_skyLight * 1.6) * skyVisible;
#endif
#ifndef VV_DX_NO_CLOUDS
            if (S_REFLECTED_CLOUDS > 0.5 && R.y > 0.01 && skyl > 0.3)
            {
                vec4 cloud = vvCloudMarch(world, R, 4000.0, 4.0, 0.5);
                reflection = reflection * cloud.a + cloud.rgb * skyVisible;
            }
#endif
        }
        reflection = mix(reflection, hit.rgb, hit.a);

        vec3 L = lightDirection();
        vec3 Rg = reflect(viewDir, normalize(mix(N, wn, 0.7)));
        float rl = max(dot(Rg, L), 0.0);
        float lowSun = 1.0 - smoothstep(0.08, 0.4, L.y);
        float core = vvPow(rl, 260.0) * 2.4 + vvPow(rl, 36.0) * 0.22 + vvPow(rl, 10.0) * 0.10 * lowSun;
        vec2 cell = floor(world.xz * 8.0);
        float seed = hash12(cell);
        float twinkle = 0.5 + 0.5 * sin(t * (1.5 + seed * 2.0) + seed * 6.2831853);
        twinkle = smoothstep(0.7, 1.0, twinkle) * step(0.88, hash12(cell + 19.7));
        float sparkle = twinkle * vvPow(rl, 24.0) * S_WATER_SPARKLE * (1.0 - smoothstep(12.0, 48.0, dist));
        vec3 lightCol = u_sunDir.y >= 0.0 ? vec3(1.0, 0.95, 0.85) : vec3(0.70, 0.78, 1.0) * 0.5;
        float glintVisible = u_time.y * smoothstep(0.4, 0.8, skyl) * smoothstep(0.0, 0.06, L.y) * (fromAbove ? 1.0 : 0.0);
        float g = (core + sparkle * 0.5) * glintVisible;
        vec3 glint = lightCol * (1.0 - exp(-g * 1.5)) * 1.15;

        vec3 body;
        float alpha;
        vec3 lightLevel = v_skyLight * skyl * skyl + v_sunLight * 0.25 * skyl;
        vec3 biomeTint = clamp(v_tint, vec3_splat(0.02), vec3_splat(1.0));
        vec3 tintM = sqrt(biomeTint) * vec3(1.0, 0.85, 0.8);
        tintM.g = max(tintM.g, 0.39);
        vec3 biomeExtinction = WATER_EXTINCTION * clamp(log(biomeTint) / log(WATER_DEFAULT_TINT), vec3_splat(0.3), vec3_splat(3.0));
        float variation = valueNoise(world.xz * 0.045 + vec2(t * 0.01, 0.0)) * 0.5 + valueNoise(world.xz * 0.19) * 0.25;
        vec3 deep = tintM * tintM * vec3(0.20, 0.52, 0.70) * (0.88 + variation * 0.3);
        vec3 scatterColor = vvTonemap(deep * clamp(luma(lightLevel) * 1.6, 0.03, 1.2) * 1.3) * 0.9;
        float foam = 0.0;
        VV_BRANCH if (scene)
        {
            vec2 uv = gl_FragCoord.xy * u_viewTexel.xy;
            float surfaceDist = length(v_rel);
            vec3 bend = wn - N;
            vec2 offset = vec2(dot(bend, T), top ? dot(bend, B) : -bend.y) * 0.03 * S_REFRACTION * clamp(6.0 / max(surfaceDist, 1.0), 0.15, 1.0);
            vec2 uvR = clamp(uv + offset, vec2_splat(0.001), vec2_splat(0.999));
            float dR = sceneDepthAt(uvR);
            vec3 behindR = sceneWorld(uvR, dR);
            if (dR > 0.00001 && length(behindR - u_camera.xyz) < surfaceDist)
            {
                uvR = uv;
                dR = sceneDepthAt(uv);
                behindR = sceneWorld(uv, dR);
            }
            float thickness = dR <= 0.00001 ? 48.0 : max(length(behindR - u_camera.xyz) - surfaceDist, 0.0);
            float vertical = dR <= 0.00001 ? 48.0 : max(world.y - behindR.y, 0.0);
            if (!fromAbove)
                thickness = min(thickness, 6.0) * 0.4;
            if (!top)
                thickness = min(thickness, 1.5);
            vec3 trans = exp(-biomeExtinction * thickness * 0.75);
            float murk = 1.0 - exp(-0.11 * thickness / max(S_WATER_CLARITY, 0.1));
            murk = top && fromAbove ? 0.28 + 0.72 * murk : murk;
            vec3 below = sceneColorAt(uvR) * mix(vec3(0.86, 1.0, 0.96), tintM * tintM * 1.9, 0.6);
            body = mix(below * trans, scatterColor, murk);
            alpha = 1.0;
            VV_BRANCH if (top && fromAbove && vertical < 0.9)
            {
                float edge = 1.0 - smoothstep(0.05, 0.9, vertical);
                float n1 = valueNoise(world.xz * 3.1 + vec2(t * 0.6, -t * 0.4));
                float n2 = valueNoise(world.xz * 7.3 - vec2(t * 0.9, t * 0.3));
                float breakup = n1 * 0.65 + n2 * 0.35;
                foam = smoothstep(0.35, 0.75, edge * 0.9 + breakup * 0.6 - 0.25) * edge * (1.0 - smoothstep(16.0, 48.0, dist));
            }
        }
        else
        {
            vec3 albedo = vvPow(clamp(tex.rgb * v_tint, 0.0, 1.0), vec3_splat(2.2));
            vec3 base = vvTonemap(albedo * (v_sunLight * 0.5 * skyl + v_skyLight * skyl * skyl + vec3_splat(0.02)));
            body = mix(saturateColor(base, 0.9), scatterColor, 0.55);
            alpha = clamp(mix(color.a, 1.0, fresnel * 0.7), 0.0, 1.0);
        }

        if (!fromAbove)
        {
            fresnel = cosV < 0.66 ? 1.0 : fresnel * 0.3;
            reflection = v_haze.rgb;
        }
        else if (fresnel <= 0.05)
            fresnel = 0.0;

        vec3 result = mix(body, reflection, fresnel) + glint;
        vec3 foamCol = vvTonemap((v_sunLight * 0.55 + v_skyLight) * skyl * 1.4) * 0.9;
        result = mix(result, foamCol, foam * 0.55);
        alpha = clamp(alpha + luma(glint), 0.0, 1.0);
        result = vvFog(result, v_fog, v_haze);
        outColor = vec4(result, alpha);
    }
    gl_FragColor = outColor;
}
}

[shader("vertex")]
VsOut vsMain(VsIn input)
{
    vs_stage::a_position = input.a_position;
    vs_stage::a_color0 = input.a_color0;
    vs_stage::a_texcoord0 = input.a_texcoord0;
    vs_stage::a_texcoord1 = input.a_texcoord1;
    vs_stage::stageMain();
    VsOut output;
    output.clipPosition = vs_stage::gl_Position;
    output.v_color0 = vs_stage::v_color0;
    output.v_texcoord0 = vs_stage::v_texcoord0;
    output.v_fog = vs_stage::v_fog;
    output.v_layer = vs_stage::v_layer;
    output.v_visibility = vs_stage::v_visibility;
    output.v_rel = vs_stage::v_rel;
    output.v_light = vs_stage::v_light;
    output.v_tint = vs_stage::v_tint;
    output.v_foliage = vs_stage::v_foliage;
    output.v_sunLight = vs_stage::v_sunLight;
    output.v_skyLight = vs_stage::v_skyLight;
    output.v_haze = vs_stage::v_haze;
    return output;
}

[shader("fragment")]
float4 fsMain(FsIn input) : SV_Target
{
    fs_stage::gl_FragCoord = input.fragCoord;
    fs_stage::v_color0 = input.v_color0;
    fs_stage::v_texcoord0 = input.v_texcoord0;
    fs_stage::v_fog = input.v_fog;
    fs_stage::v_layer = input.v_layer;
    fs_stage::v_visibility = input.v_visibility;
    fs_stage::v_rel = input.v_rel;
    fs_stage::v_light = input.v_light;
    fs_stage::v_tint = input.v_tint;
    fs_stage::v_foliage = input.v_foliage;
    fs_stage::v_sunLight = input.v_sunLight;
    fs_stage::v_skyLight = input.v_skyLight;
    fs_stage::v_haze = input.v_haze;
    fs_stage::stageMain();
    return fs_stage::gl_FragColor;
}
