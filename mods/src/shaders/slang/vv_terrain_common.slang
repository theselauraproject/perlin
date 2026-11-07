#pragma once
#include "vv_common.slang"
#include "vv_block_sampling.slang"

#define VV_TERRAIN_EXPOSURE 0.45

vec3 faceNormal(vec3 rel)
{
    vec3 n = cross(dFdx(rel), dFdy(rel));
    float l = length(n);
    return l > 1.0e-8 ? n / l : vec3(0.0, 1.0, 0.0);
}

float vanillaShade(vec3 n)
{
    float up = max(n.y, 0.0);
    float down = max(-n.y, 0.0);
    float z = abs(n.z);
    float x = abs(n.x);
    return clamp(up * 1.0 + down * 0.5 + z * 0.8 + x * 0.6, 0.4, 1.0);
}

float shadowTap(vec3 p, vec3 sc, float ndl, vec4 info, float which, float taps)
{
    float texelWorld = info.y * info.x;
    vec2 edge = min(sc.xy, vec2(1.0, 1.0) - sc.xy);
    float inside = smoothstep(0.0, 0.06, min(edge.x, edge.y));
    float lit = 1.0;
    VV_BRANCH if (inside > 0.0)
    {
        float bias = (texelWorld * 1.8 * (1.0 - ndl) + 0.05) / info.z;
        float radius = clamp(0.07 * S_SHADOW_SOFTNESS / max(texelWorld, 1.0e-4), 1.0, 5.0) * info.x * step(1.5, taps);
        float spin = hash12(floor(p.xz * 16.0) + floor(p.y * 16.0) * 7.31) * 6.2831853;
        float sum = 0.0;
        VV_LOOP for (int i = 0; i < 20; i++)
        {
            if (float(i) >= taps)
                break;
            float fi = float(i) + 0.5;
            float r = sqrt(fi / taps);
            float a = fi * 2.3999632 + spin;
            vec2 uv = sc.xy + vec2(cos(a), sin(a)) * r * radius;
            float stored;
            if (which < 0.5)
                stored = texture2DLod(s_shadow, uv, 0.0).r;
            else if (which < 1.5)
                stored = texture2DLod(s_shadowB, uv, 0.0).r;
            else
                stored = texture2DLod(s_shadowEnt, uv, 0.0).r;
            sum += (sc.z - bias <= stored) ? 1.0 : 0.0;
        }
        lit = mix(1.0, sum / taps, inside);
    }
    return lit;
}

vec3 shadowGlass(vec3 sc, float ndl, vec4 info, float second)
{
    float texelWorld = info.y * info.x;
    vec2 edge = min(sc.xy, vec2(1.0, 1.0) - sc.xy);
    float inside = smoothstep(0.0, 0.06, min(edge.x, edge.y));
    float bias = (texelWorld * 1.8 * (1.0 - ndl) + 0.05) / info.z;
    vec4 glass = second > 0.5 ? texture2DLod(s_shadowTintB, sc.xy, 0.0) : texture2DLod(s_shadowTint, sc.xy, 0.0);
    return (sc.z - bias > glass.a && inside > 0.5) ? glass.rgb : vec3_splat(1.0);
}

float sunShadow(vec3 world, vec3 n, float ndl, out vec3 tint)
{
    float texelWorld = u_shadowInfo.y * u_shadowInfo.x;
    vec3 p = world + n * (texelWorld * 1.5 + 0.02);
    vec3 sc = mul(u_shadowMtx, vec4(p, 1.0)).xyz;
    float fade = u_shadowInfoB.w;
    tint = shadowGlass(sc, ndl, u_shadowInfo, 0.0);
    vec3 scB = vec3_splat(0.0);
    VV_BRANCH if (fade > 0.0)
    {
        scB = mul(u_shadowMtxB, vec4(p, 1.0)).xyz;
        tint = mix(tint, shadowGlass(scB, ndl, u_shadowInfoB, 1.0), fade);
    }
    if (u_shadowInfo.w < 0.5)
        return -1.0;
    float filterLevel = floor(S_SHADOW_FILTER + 0.5);
    float taps = filterLevel < 0.5 ? 1.0 : (filterLevel < 1.5 ? 6.0 : (filterLevel < 2.5 ? 12.0 : 20.0));
    float viewDist = length(world - u_camera.xyz);
    taps = viewDist > 96.0 ? 1.0 : (viewDist > 48.0 ? min(taps, 6.0) : taps);
    float lit = shadowTap(p, sc, ndl, u_shadowInfo, 0.0, taps);
    VV_BRANCH if (fade > 0.0)
        lit = mix(lit, shadowTap(p, scB, ndl, u_shadowInfoB, 1.0, taps), fade);
    VV_BRANCH if (u_shadowInfoE.w > 0.5)
    {
        vec3 scE = mul(u_shadowMtxE, vec4(p, 1.0)).xyz;
        lit = min(lit, shadowTap(p, scE, ndl, u_shadowInfoE, 2.0, taps));
    }
    return lit;
}

// the colormaps give Java-style olive greens; squared into linear light the red/blue gap widens and grass goes yellow and muddy.
// saturated biome tints are eased toward a clean green, grey and white tints (stone, planks, most blocks) pass through untouched
vec3 vvVibrantTint(vec3 tint)
{
    float hi = max(tint.r, max(tint.g, tint.b));
    float lo = min(tint.r, min(tint.g, tint.b));
    float sat = clamp((hi - lo) / max(hi, 0.001) * 2.5, 0.0, 1.0);
    vec3 vivid = vec3(tint.r * 0.80, tint.g * 1.04, tint.b * 1.10);
    return mix(tint, vivid, sat * 0.85);
}

vec3 vvLit(vec3 texRgb, vec3 tint, vec3 n, vec3 rel, vec2 light, float backlight, float emissive, vec3 sunLight, vec3 skyLight)
{
    float shade = vanillaShade(n);
    vec3 tintAo = vvVibrantTint(clamp(tint / max(shade, 0.01), 0.0, 1.5));
    vec3 albedo = vvPow(clamp(texRgb * tintAo, 0.0, 1.0), vec3_splat(2.2));
    vec3 hdr;
    {
        vec3 viewDir = normalize(rel);
        vec3 world = rel + u_camera.xyz;
        float skyl = light.y;
        vec3 L = lightDirection();
        float ndl = dot(n, L);
        float sky = smoothstep(0.30, 0.95, skyl);
        float sunUp = smoothstep(0.0, 0.08, L.y);

        float direct = 0.0;
        vec3 sunTint = vec3_splat(1.0);
        VV_BRANCH if ((ndl > 0.0 || backlight > 0.0) && sky > 0.0 && sunUp > 0.0)
        {
            vec3 glassTint;
            float s = sunShadow(world, n, max(ndl, 0.0), glassTint);
            s = s < 0.0 ? 1.0 : mix(1.0, s, S_SHADOW_STRENGTH);
            s *= sky * vvCloudShadow(world);
            float VoL = clamp(dot(viewDir, L), 0.0, 1.0);
            float scatter = vvPow(VoL, 10.0) * backlight * S_FOLIAGE * (1.0 - vvRain());
            float wrap = mix(max(ndl, 0.0), clamp(abs(ndl) * 0.6 + 0.35, 0.0, 1.0), backlight * clamp(S_FOLIAGE, 0.0, 1.0));
            direct = (wrap + scatter * 1.5) * s * sunUp;
            sunTint = glassTint;
        }
        float depthBelow = (u_camera.y + u_dynParams.z) - world.y;
        float submerged = vvUnderwater() * step(0.0, depthBelow);
        VV_BRANCH if (submerged > 0.5)
        {
            float caustic = vvCaustics(world, u_time.x) * max(n.y, 0.0);
            direct *= exp(-0.07 * max(depthBelow, 0.0)) * (0.55 + caustic * 1.9);
        }

        float skyView = 0.78 + 0.22 * n.y;
        vec3 ambient = skyLight * skyl * skyl * skyView * 1.2;
        ambient += sunLight * 0.06 * skyl * clamp(-n.y * 0.5 + 0.5, 0.0, 1.0) * smoothstep(0.0, 0.2, L.y);

        float bl = light.x;
        float held = u_heldLight.x / 15.0;
        if (held > 0.001)
        {
            float reach = 3.0 + held * 11.0;
            float att = clamp(1.0 - length(rel) / reach, 0.0, 1.0);
            bl = max(bl, att * att * held * (0.35 + 0.65 * max(dot(n, -viewDir), 0.0)));
        }
        vec3 block = vec3(1.0, 0.70, 0.42) * (bl * bl * bl * 2.2 + bl * bl * 0.25) * S_BLOCK;

        vec3 minLight = vec3(0.008, 0.010, 0.016) * (1.0 - skyl * skyl);
        hdr = albedo * ((sunLight * sunTint * direct + ambient) * VV_TERRAIN_EXPOSURE + block + minLight) + albedo * emissive * 3.0;
    }
    vec3 lm = vvPow(sampleLightmap(light * 240.0), vec3_splat(2.2));
    vec3 hdrOther = albedo * (lm * 2.4 + vec3(0.05, 0.035, 0.04) * S_AMBIENT) * mix(shade, 1.0, 0.4) + albedo * emissive * 2.5;
    return vvTonemap(u_skyParams.z > 0.5 ? hdr : hdrOther);
}

vec3 vvFogColor(vec4 haze)
{
    return mix(u_fogColor.rgb, haze.rgb, step(0.5, u_skyParams.z) * (1.0 - vvUnderwater()));
}

vec3 vvFog(vec3 color, vec2 fogDistance, vec4 haze)
{
    color = mix(color, haze.rgb, haze.a);
    float f = fogAmount(fogDistance.x, fogDistance.y);
    vec3 fogged = mix(color, vvFogColor(haze), f);
    float range = max(u_fogEnv.y, 6.0);
    vec3 trans = exp(-vec3(3.4, 1.7, 1.4) * fogDistance.x / range);
    vec3 under = color * trans + haze.rgb * (vec3_splat(1.0) - trans);
    return mix(fogged, under, vvUnderwater());
}

float isTile(float layer, float tile)
{
    return abs(layer - tile) < 0.5 ? 1.0 : 0.0;
}
