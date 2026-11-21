#pragma once
#include "vv_resources.slang"
#if VV_HLSL
#define VV_LOOP [loop]
#define VV_BRANCH [branch]
#else
#define VV_LOOP
#define VV_BRANCH
#endif

#define S_EXPOSURE u_packSettings[0].x
#define S_SATURATION u_packSettings[0].y
#define S_CONTRAST u_packSettings[0].z
#define S_SUN u_packSettings[0].w
#define S_AMBIENT u_packSettings[1].x
#define S_BLOCK u_packSettings[1].y
#define S_FOLIAGE u_packSettings[1].z
#define S_FOG u_packSettings[1].w
#define S_REFLECTIONS u_packSettings[2].x
#define S_CLOUDS u_packSettings[2].y
#define S_CLOUD_SHADOWS u_packSettings[2].z
#define S_WAVING u_packSettings[2].w
#define S_SHADOW_FILTER u_packSettings[3].x
#define S_SHADOW_SOFTNESS u_packSettings[3].y
#define S_SHADOW_STRENGTH u_packSettings[3].z
#define S_CLOUD_QUALITY u_packSettings[3].w
#define S_CLOUD_OPACITY u_packSettings[4].x
#define S_CLOUD_THICKNESS u_packSettings[4].y
#define S_CLOUD_GLOW u_packSettings[4].z
#define S_WATER_CLARITY u_packSettings[4].w
#define S_WATER_WAVES u_packSettings[5].x
#define S_WATER_SPARKLE u_packSettings[5].y
#define S_REFRACTION u_packSettings[5].z
#define S_REFLECTED_CLOUDS u_packSettings[5].w
#define S_SUN_SIZE u_packSettings[6].x
#define S_SUN_GLOW u_packSettings[6].y
#define S_STARS u_packSettings[6].z
#define S_NIGHT u_packSettings[6].w
#define S_TONEMAP u_packSettings[7].x
#define S_TEMPERATURE u_packSettings[7].y
#define S_SKY_BRIGHTNESS u_packSettings[7].z
#define S_CLOUD_SHADOW_STRENGTH u_packSettings[7].w

#define CLOUD_THICKNESS max(S_CLOUD_THICKNESS, 1.0)

float dynamicLevel(vec3 rel)
{
    float best = 0.0;
    VV_LOOP for (int i = 0; i < 8; i++)
    {
        float on = step(float(i) + 0.5, u_dynParams.y);
        best = max(best, on * (u_dynLights[i].w - length(rel - u_dynLights[i].xyz)));
    }
    return clamp(best, 0.0, 15.0);
}

float linearFog(float distance, float fogStart, float fogEnd)
{
    float f = clamp((distance - fogStart) / max(fogEnd - fogStart, 0.00001), 0.0, 1.0);
    return distance >= fogEnd ? 1.0 : f;
}

float fogAmount(float spherical, float cylindrical)
{
    return max(linearFog(spherical, u_fogEnv.x, u_fogEnv.y), linearFog(cylindrical, u_fogEnv.z, u_fogEnv.w)) *
           u_fogColor.a;
}

vec3 sampleLightmap(vec2 lightBytes)
{
    vec2 uv = clamp(lightBytes / 256.0 + vec2_splat(0.5 / 16.0), vec2_splat(0.5 / 16.0), vec2_splat(15.5 / 16.0));
    return texture2DLod(s_lightmap, uv, 0.0).rgb;
}

float vvPow(float x, float e)
{
    return pow(max(abs(x), 1.0e-6), e);
}

vec3 vvPow(vec3 x, vec3 e)
{
    return pow(max(abs(x), vec3_splat(1.0e-6)), e);
}

float luma(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

vec3 saturateColor(vec3 c, float amount)
{
    return mix(vec3_splat(luma(c)), c, amount);
}

float vvRain()
{
    return clamp((1.0 - u_time.y) * 2.0, 0.0, 1.0);
}

float vvDay()
{
    return smoothstep(-0.18, 0.12, u_sunDir.y);
}

float vvTwilight()
{
    float h = u_sunDir.y;
    return smoothstep(-0.24, 0.0, h) * (1.0 - smoothstep(0.02, 0.40, h));
}

vec3 lightDirection()
{
    return u_sunDir.y >= 0.0 ? u_sunDir.xyz : -u_sunDir.xyz;
}

vec3 vvSunRadiance()
{
    float h = u_sunDir.y;
    float airmass = 1.0 / (max(h, 0.0) + 0.075);
    vec3 trans = exp(-vec3(0.19, 0.46, 1.05) * airmass * 0.17);
    return trans * 2.7 * smoothstep(-0.04, 0.10, h) * S_SUN;
}

vec3 vvMoonRadiance()
{
    return vec3(0.42, 0.55, 0.95) * 0.30 * smoothstep(-0.02, 0.12, -u_sunDir.y) * S_SUN * S_NIGHT;
}

vec3 vvMainLight()
{
    return u_lightMain.xyz;
}

vec3 vvSkyAmbient()
{
    return u_lightSky.xyz;
}

float vvFoliageKind(float layer)
{
    vec2 uv = vec2((mod(layer, 256.0) + 0.5) / 256.0, (floor(layer / 256.0) + 0.5) / 64.0);
    return floor(texture2DLod(s_tileFlags, uv, 0.0).r * 255.0 + 0.5);
}

vec3 vvWave(vec3 pos, float kind, vec2 uv)
{
    float t = u_time.x;
    float wind = S_WAVING * mix(1.0, 1.9, vvRain());
    float gust = 0.65 + 0.35 * sin(t * 0.35 + pos.x * 0.045 + pos.z * 0.03);
    vec3 offset = vec3(sin(t * 1.9 + pos.x * 0.71 + pos.z * 0.33 + pos.y * 0.21),
                       sin(t * 1.4 + pos.x * 0.37 + pos.z * 0.61) * 0.45,
                       sin(t * 1.7 + pos.z * 0.83 + pos.x * 0.29 + pos.y * 0.17));
    float top = clamp(1.0 - uv.y, 0.0, 1.0);
    float weight = 0.0;
    vec3 amp = vec3(0.0, 0.0, 0.0);
    if (kind > 0.5 && kind < 1.5)
    {
        weight = 1.0;
        amp = vec3(0.045, 0.03, 0.045);
    }
    else if (kind > 1.5)
    {
        weight = kind > 3.5 ? 0.5 + 0.5 * top : kind > 2.5 ? 0.5 * top : top;
        amp = vec3(0.13, 0.02, 0.13);
    }
    return offset * amp * weight * wind * gust;
}

vec3 vvSunColor()
{
    float h = u_sunDir.y;
    vec3 low = vec3(1.0, 0.46, 0.16);
    vec3 mid = vec3(1.0, 0.74, 0.44);
    vec3 high = vec3(1.0, 0.96, 0.88);
    vec3 c = mix(low, mid, smoothstep(-0.05, 0.12, h));
    return mix(c, high, smoothstep(0.10, 0.45, h));
}

vec3 vvHorizonColor(vec3 dir)
{
    float day = vvDay();
    float tw = vvTwilight();
    vec2 flatDir = dir.xz / max(length(dir.xz), 0.0001);
    vec2 sunFlat = u_sunDir.xz / max(length(u_sunDir.xz), 0.0001);
    float sunward = clamp(dot(flatDir, sunFlat) * 0.5 + 0.5, 0.0, 1.0);
    vec3 dayHorizon = vec3(0.70, 0.83, 0.97);
    vec3 twHorizon = mix(vec3(0.58, 0.44, 0.66), mix(vec3(1.0, 0.62, 0.36), vec3(1.0, 0.48, 0.22), sunward), sunward * sunward);
    vec3 nightHorizon = vec3(0.095, 0.120, 0.200) * S_NIGHT;
    vec3 c = mix(nightHorizon, dayHorizon, day);
    return mix(c, twHorizon, tw * mix(0.45, 1.0, day));
}

vec3 vvSkyGradient(vec3 dir)
{
    float up = clamp(dir.y, 0.0, 1.0);
    float day = vvDay();
    float tw = vvTwilight();

    vec3 dayZenith = vec3(0.26, 0.47, 0.86);
    vec3 twZenith = vec3(0.24, 0.27, 0.52);
    vec3 nightZenith = vec3(0.028, 0.040, 0.085) * S_NIGHT;
    vec3 zenith = mix(nightZenith, dayZenith, day);
    zenith = mix(zenith, twZenith, tw * mix(0.4, 1.0, day));

    vec3 horizon = vvHorizonColor(dir);
    float band = exp(-up * mix(4.2, 7.5, tw));
    vec3 c = mix(zenith, horizon, band);

    float toSun = max(dot(dir, u_sunDir.xyz), 0.0);
    float spread = vvPow(toSun * toSun, up * 2.0 + 1.0);
    vec3 sunTint = vvSunColor();
    c += sunTint * spread * (0.10 + 0.55 * tw) * smoothstep(-0.20, 0.0, u_sunDir.y) * u_time.y;

    float toMoon = max(-dot(dir, u_sunDir.xyz), 0.0);
    c += vec3(0.10, 0.13, 0.22) * vvPow(toMoon, 6.0) * (1.0 - day) * 0.35 * S_NIGHT;

    float rain = vvRain();
    c = mix(c, vec3_splat(luma(c)) * vec3(0.86, 0.89, 0.93) * 0.78, rain);

    return min(c * S_SKY_BRIGHTNESS, vec3_splat(1.2));
}


vec3 vvSunDisc(vec3 dir, out float coverage)
{
    coverage = 0.0;
    vec3 s = u_sunDir.xyz;
    float facing = dot(dir, s);
    vec3 axisA = normalize(cross(s, vec3(0.0, 0.0, 1.0)));
    vec2 q = vec2(dot(dir, axisA), dir.z) / max(facing, 1.0e-4);
    float d = max(abs(q.x), abs(q.y));
    float size = 0.052 * S_SUN_SIZE;
    float visible = u_time.y * smoothstep(-0.05, 0.02, s.y) * step(0.0, facing);
    vec3 warm = mix(vec3(1.0, 0.80, 0.55), vec3(1.0, 0.98, 0.92), smoothstep(0.0, 0.35, s.y));
    coverage = (1.0 - smoothstep(size - 0.002, size + 0.002, d)) * visible;
    float bloom = (exp(-max(d - size, 0.0) * 30.0) * 0.38 + exp(-max(d - size, 0.0) * 7.0) * 0.10) * S_SUN_GLOW;
    return warm * bloom * visible * (1.0 - coverage);
}

vec3 vvMoonDisc(vec3 dir, out float coverage, out vec3 moonColor)
{
    coverage = 0.0;
    moonColor = vec3_splat(0.0);
    vec3 m = -u_sunDir.xyz;
    float facing = dot(dir, m);
    vec3 axisA = normalize(cross(m, vec3(0.0, 0.0, 1.0)));
    vec2 q = vec2(dot(dir, axisA), dir.z) / max(facing, 1.0e-4);
    float size = 0.034 * S_SUN_SIZE;
    float d = max(abs(q.x), abs(q.y));
    float visible = u_time.y * (1.0 - smoothstep(0.0, 0.25, u_sunDir.y)) * step(0.0, facing);
    if (d < size && facing > 0.0)
    {
        vec2 cell = floor((q / size * 0.5 + 0.5) * 8.0);
        float crater = step(0.72, hash12(cell + vec2(3.0, 7.0))) * 0.22 + hash12(cell) * 0.08;
        float phase = u_skyParams.y;
        float k = abs(4.0 - mod(phase, 8.0)) / 4.0;
        float side = mod(phase, 8.0) < 4.0 ? 1.0 : -1.0;
        float col = (cell.x + 0.5) / 8.0 * 2.0 - 1.0;
        float lit = step(1.0 - 2.0 * k, col * side);
        moonColor = mix(vec3(0.16, 0.18, 0.24), vec3(0.93, 0.94, 0.98) - crater, lit);
        coverage = visible * mix(0.10, 1.0, lit);
    }
    float halo = exp(-max(d - size, 0.0) * 16.0) * 0.22 + exp(-max(d - size, 0.0) * 4.0) * 0.05;
    return vec3(0.70, 0.78, 1.0) * halo * visible * (1.0 - coverage);
}

vec3 vvSunGlow(vec3 dir)
{
    float toSun = max(dot(dir, u_sunDir.xyz), 0.0);
    float low = 1.0 - smoothstep(0.0, 0.5, u_sunDir.y);
    float glow = vvPow(toSun, 5.0) * (0.10 + 0.22 * low) + vvPow(toSun, 36.0) * 0.30 + vvPow(toSun, 400.0) * 0.6;
    return vvSunColor() * glow * u_time.y * smoothstep(-0.14, 0.03, u_sunDir.y) * (1.0 - vvRain()) * S_SUN_GLOW;
}


vec3 vvSky(vec3 dir, bool bodies)
{
    vec3 c = vvSkyGradient(dir);
    if (u_skyParams.z > 0.5)
        c += vvSunGlow(dir);
    float toSun = dot(dir, u_sunDir.xyz);
    VV_BRANCH if (bodies && u_skyParams.z > 0.5 && toSun > 0.82)
    {
        float sunCover;
        vec3 sunBloom = vvSunDisc(dir, sunCover);
        vec3 sunCol = mix(vec3(1.0, 0.88, 0.70), vec3(1.0, 1.0, 0.98), smoothstep(0.0, 0.3, u_sunDir.y));
        c = mix(c, sunCol, sunCover) + sunBloom;
    }
    VV_BRANCH if (bodies && u_skyParams.z > 0.5 && toSun < -0.82 && u_sunDir.y < 0.25)
    {
        float moonCover;
        vec3 moonCol;
        vec3 moonHalo = vvMoonDisc(dir, moonCover, moonCol);
        c = mix(c, moonCol, moonCover) + moonHalo;
    }
    return c;
}

float cloudCell(vec2 cell)
{
    vec2 size = 1.0 / (12.0 * u_cloudMap.zw);
    vec2 uv = (mod(cell, size) + 0.5) / size;
    return step(0.04, texture2DLod(s_clouds, uv, 0.0).a);
}

vec2 cloudCoord(vec2 xz)
{
    return (xz + u_cloudMap.xy) / 12.0;
}

void vvCloudColors(out vec3 body, out vec3 rim)
{
    float day = vvDay();
    float tw = vvTwilight();
    vec3 sun = vvSunColor();
    body = mix(vec3(0.15, 0.17, 0.27), vec3(0.80, 0.84, 0.94), day);
    rim = mix(vec3(0.26, 0.28, 0.40), vec3(1.0, 1.0, 1.0), day);
    body = mix(body, vec3(0.62, 0.52, 0.66), tw * day);
    rim = mix(rim, sun * vec3(1.0, 0.86, 0.80), tw * day);
    float rain = vvRain();
    body = mix(body, vec3_splat(luma(body)) * 0.62, rain);
    rim = mix(rim, vec3_splat(luma(rim)) * 0.68, rain);
}

vec4 vvCloudMarch(vec3 origin, vec3 dir, float maxDistance, float steps, float dither)
{
    vec4 result = vec4(0.0, 0.0, 0.0, 1.0);
    float base = u_skyParams.x;
    float thick = CLOUD_THICKNESS;
    float top = base + thick;
    float dy = abs(dir.y) < 1.0e-4 ? 1.0e-4 : dir.y;
    float tA = (base - origin.y) / dy;
    float tB = (top - origin.y) / dy;
    float t0 = max(min(tA, tB), 0.0);
    float t1 = min(min(max(tA, tB), maxDistance), t0 + 240.0);
    VV_BRANCH if (u_skyParams.z > 0.5 && S_CLOUDS > 0.5 && t1 > t0 && t0 < 2600.0)
    {
        vec3 L = lightDirection();
        float Ly = max(abs(L.y), 0.12);
        vec2 lightShift = L.xz / Ly;
        vec3 body, rim;
        vvCloudColors(body, rim);
        float day = vvDay();
        vec3 sunCol = mix(vec3(0.42, 0.50, 0.72) * S_NIGHT, vvSunColor() * 1.35, day);
        float low = (1.0 - smoothstep(0.0, 0.35, u_sunDir.y)) * smoothstep(-0.28, -0.02, u_sunDir.y) * u_time.y;
        vec2 flatDir = dir.xz / max(length(dir.xz), 1.0e-4);
        vec2 sunFlat = u_sunDir.xz / max(length(u_sunDir.xz), 1.0e-4);
        float sunward = clamp(dot(flatDir, sunFlat) * 0.5 + 0.5, 0.0, 1.0);
        vec3 ambientTop = mix(body, vvSkyGradient(vec3(0.0, 1.0, 0.0)) * 1.25 + body * 0.35, 0.35 * day);
        vec3 ambientBottom = body * mix(0.42, 0.55, vvTwilight()) + vvSunColor() * low * (0.25 + 0.55 * sunward * sunward);
        float VoL = dot(dir, L);
        float phase = 0.55 + 1.6 * S_CLOUD_GLOW * vvPow(max(VoL, 0.0), 6.0) + 0.25 * vvPow(max(-VoL, 0.0), 2.0);
        float rain = vvRain();
        float sigma = mix(0.22, 0.36, rain) * S_CLOUD_OPACITY;
        float dt = (t1 - t0) / steps;
        float trans = 1.0;
        vec3 scatter = vec3_splat(0.0);
        float firstHit = -1.0;
        VV_LOOP for (int i = 0; i < 16; i++)
        {
            if (float(i) >= steps || trans < 0.03)
                break;
            float t = t0 + (float(i) + dither) * dt;
            vec3 p = origin + dir * t;
            vec2 c = cloudCoord(p.xz);
            float filled = cloudCell(floor(c));
            VV_BRANCH if (filled > 0.5)
            {
                if (firstHit < 0.0)
                    firstHit = t;
                float h = clamp((p.y - base) / thick, 0.0, 1.0);
                float toTop = (1.0 - h) * thick;
                float shade = cloudCell(floor(cloudCoord(p.xz + lightShift * (toTop + 2.0))));
                float lit = mix(1.0, 0.28, shade) * mix(0.42, 1.0, h * h);
                vec3 light = sunCol * lit * phase * 0.62 + mix(ambientBottom, ambientTop, h);
                float a = 1.0 - exp(-sigma * dt);
                scatter += trans * a * light;
                trans *= 1.0 - a;
            }
        }
        float hitDist = firstHit >= 0.0 ? firstHit : t0;
        float fade = (1.0 - smoothstep(900.0, 2400.0, hitDist)) * smoothstep(0.0, 0.03, abs(dir.y));
        vec3 haze = vvHorizonColor(dir) + vvSunGlow(dir) * 0.5;
        float aerial = smoothstep(120.0, 1800.0, hitDist) * 0.75;
        scatter = mix(scatter, haze * (1.0 - trans), aerial);
        result = vec4(scatter * fade, mix(1.0, trans, fade));
    }
    return result;
}

vec4 vvClouds(vec3 origin, vec3 dir, float maxDistance)
{
    float steps = 6.0 + 4.0 * floor(S_CLOUD_QUALITY + 0.5);
    return vvCloudMarch(origin, dir, maxDistance, steps, 0.5);
}


float vvCloudShadow(vec3 world)
{
    vec3 L = lightDirection();
    float mid = u_skyParams.x + CLOUD_THICKNESS * 0.5;
    float enabled = step(0.5, u_skyParams.z) * step(0.5, S_CLOUDS) * step(0.5, S_CLOUD_SHADOWS) * step(world.y, mid);
    vec3 p = world + L * ((mid - world.y) / max(L.y, 0.05));
    vec2 cc = cloudCoord(p.xz) - 0.5;
    vec2 i = floor(cc);
    vec2 f = smoothstep(0.0, 1.0, cc - i);
    float c = mix(mix(cloudCell(i), cloudCell(i + vec2(1.0, 0.0)), f.x),
                  mix(cloudCell(i + vec2(0.0, 1.0)), cloudCell(i + vec2(1.0, 1.0)), f.x), f.y);
    return max(1.0 - 0.3 * c * smoothstep(0.05, 0.25, L.y) * enabled * S_CLOUD_SHADOW_STRENGTH, 0.2);
}

vec3 vvHable(vec3 x)
{
    return ((x * (0.15 * x + 0.05) + 0.004) / (x * (0.15 * x + 0.5) + 0.06)) - 0.0667;
}

vec3 vvTonemap(vec3 hdr)
{
    vec3 x = max(hdr, vec3_splat(0.0)) * 0.70 * exp2(S_EXPOSURE);
    vec3 acesChannels = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    float lx = luma(x);
    float acesLum = clamp((lx * (2.51 * lx + 0.03)) / (lx * (2.43 * lx + 0.59) + 0.14), 0.0, 1.0);
    vec3 acesHue = x * (acesLum / max(lx, 1.0e-5));
    vec3 aces = clamp(mix(acesHue, acesChannels, clamp((acesLum - 0.6) / 0.4, 0.0, 1.0)), 0.0, 1.0);
    vec3 filmic = clamp(vvHable(x * 2.2) / 0.725, 0.0, 1.0);
    float l = luma(x);
    vec3 soft = clamp(x * (1.0 + l / 4.0) / (1.0 + l) * 1.25, 0.0, 1.0);
    float mode = floor(S_TONEMAP + 0.5);
    vec3 mapped = mode < 0.5 ? aces : (mode < 1.5 ? filmic : soft);
    vec3 display = vvPow(mapped, vec3_splat(1.0 / 2.2));
    display = saturateColor(display, 1.05 * S_SATURATION);
    display = (display - 0.5) * S_CONTRAST + 0.5;
    display *= vec3(1.0 + 0.06 * S_TEMPERATURE, 1.0, 1.0 - 0.08 * S_TEMPERATURE);
    return clamp(display, 0.0, 1.0);
}

float vvUnderwater()
{
    return step(0.5, u_skyParams.w) * step(u_skyParams.w, 1.5);
}

vec3 vvUnderwaterColor()
{
    vec3 light = vvSkyAmbient() * 0.85 + vvMainLight() * 0.3;
    vec3 scatter = vvTonemap(vec3(0.02, 0.17, 0.24) * clamp(luma(light) * 1.6, 0.03, 1.2) * 3.0) * 0.8;
    return mix(scatter, u_fogColor.rgb * 1.6, 0.3);
}

float vvCaustics(vec3 p, float t)
{
    vec2 q = p.xz * 0.85 + vec2(p.y * 0.3, -p.y * 0.2);
    float a = sin(q.x * 2.1 + sin(q.y * 1.7 + t * 0.9) * 1.3 + t * 0.6);
    float b = sin(q.y * 2.3 + sin(q.x * 1.5 - t * 0.7) * 1.2 - t * 0.5);
    return vvPow(1.0 - abs(a * b), 6.0);
}

vec3 vvHazeColor(vec3 dir)
{
    float tw = vvTwilight();
    vec3 c = mix(vvHorizonColor(dir), mix(vec3(0.028, 0.040, 0.085) * S_NIGHT, vec3(0.26, 0.47, 0.86), vvDay()), 0.12);
    float t = max(dot(dir, u_sunDir.xyz), 0.0);
    float t2 = t * t;
    float t4 = t2 * t2;
    float t8 = t4 * t4;
    float t32 = t8 * t8 * t8 * t8;
    float low = 1.0 - smoothstep(0.0, 0.5, u_sunDir.y);
    float sunUp = smoothstep(-0.14, 0.03, u_sunDir.y) * u_time.y;
    float glow = t2 * (0.10 + 0.55 * tw) + (t4 * t * (0.10 + 0.22 * low) + t32 * 0.30) * 0.85 * (1.0 - vvRain()) * S_SUN_GLOW;
    c += vvSunColor() * glow * sunUp;
    float rain = vvRain();
    c = mix(c, vec3_splat(luma(c)) * vec3(0.86, 0.89, 0.93) * 0.78, rain);
    return min(c * S_SKY_BRIGHTNESS, vec3_splat(1.2));
}

vec4 vvHaze(vec3 rel)
{
    float dist = length(rel);
    vec3 viewDir = rel / max(dist, 1.0e-4);
    float underwater = vvUnderwater();
    vec3 hazeDir = normalize(vec3(viewDir.x, max(viewDir.y, 0.0), viewDir.z) + vec3(0.0, 1.0e-4, 0.0));
    float tw = vvTwilight();
    float day = vvDay();
    float rain = vvRain();
    vec3 hazeColor = vvHazeColor(hazeDir);
    float falloff = mix(0.030, 0.055, tw) * mix(1.0, 0.55, rain);
    float camH = u_camera.y - 62.0;
    float k = falloff;
    float dyk = max(viewDir.y * dist * k, -6.0);
    float column = abs(dyk) > 1.0e-3 ? (1.0 - exp(-dyk)) / dyk : 1.0 - 0.5 * dyk;
    float baseDensity = 0.0050 * mix(0.80, 1.0, day) * (1.0 + tw * 2.2) * (1.0 + rain * 2.5) * S_FOG;
    float optical = baseDensity * exp(-k * clamp(camH, -40.0, 400.0)) * dist * column;
    float aerial = dist * 0.00055 * S_FOG;
    float amount = (1.0 - exp(-(optical + aerial))) * step(0.5, u_skyParams.z) * (1.0 - underwater);
    return vec4(mix(hazeColor, vvUnderwaterColor(), underwater), clamp(amount, 0.0, 0.85));
}
