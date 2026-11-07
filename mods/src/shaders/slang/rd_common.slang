#pragma once
#include "rd_resources.slang"

float dynamicLevel(vec3 rel)
{
    float best = 0.0;
    for (int i = 0; i < 8; i++)
    {
        float on = step(float(i) + 0.5, u_dynParams.y);
        best = max(best, on * (u_dynLights[i].w - length(rel - u_dynLights[i].xyz)));
    }
    return clamp(best, 0.0, 15.0);
}

float linearFog(float distance, float fogStart, float fogEnd)
{
    if (distance <= fogStart)
        return 0.0;
    if (distance >= fogEnd)
        return 1.0;
    return (distance - fogStart) / max(fogEnd - fogStart, 0.00001);
}

vec3 applyFog(vec3 color, float spherical, float cylindrical, float envStart, float envEnd, float rdStart,
              float rdEnd, vec4 fogColor)
{
    float fogValue = max(linearFog(spherical, envStart, envEnd), linearFog(cylindrical, rdStart, rdEnd));
    return mix(color, fogColor.rgb, fogValue * fogColor.a);
}

vec3 sampleLightmap(vec2 lightBytes)
{
    vec2 uv = clamp(lightBytes / 256.0 + vec2_splat(0.5 / 16.0), vec2_splat(0.5 / 16.0), vec2_splat(15.5 / 16.0));
    return texture2DLod(s_lightmap, uv, 0.0).rgb;
}

float rdFog(vec3 relative)
{
    return max(linearFog(length(relative), u_fogEnv.x, u_fogEnv.y),
               linearFog(max(length(relative.xz), abs(relative.y)), u_fogEnv.z, u_fogEnv.w)) * u_fogColor.a;
}
