#pragma once
#include "compat.slang"

UNIFORMS_BEGIN
    mat4 u_mvp;
    vec4 u_camera;
    vec4 u_fogColor;
    vec4 u_fogEnv;
    vec4 u_fogSky;
    vec4 u_modulator;
    vec4 u_textureInfo;
    vec4 u_chunk;
    vec4 u_cloudOffset;
    vec4 u_cloudColor;
    vec4 u_dynParams;
    vec4 u_dynLights[8];
    vec4 u_uiParams;
    vec4 u_uiOverlay;
    vec4 u_uiGlintA;
    vec4 u_uiGlintB;
    vec4 u_rdMaterial;
    mat4 u_modelViewProj;
    mat4 u_model[1];
UNIFORMS_END

SAMPLER2DARRAY(s_atlas, 0, 1);
SAMPLER2D(s_lightmap, 2, 3);
SAMPLER2D(s_sky, 22, 23);
SAMPLER2D(s_uiTex, 24, 25);
SAMPLER2D(s_uiGlint, 26, 27);

#define PERLIN_ATLAS_PAGE_LAYERS 2048.0
SAMPLER2DARRAY(s_atlas2, 86, 87);
SAMPLER2DARRAY(s_atlas3, 88, 89);
SAMPLER2DARRAY(s_atlas4, 90, 91);

float4 perlinAtlasLod(float3 uvl, float lod)
{

    float tile = floor(uvl.z + 0.5);
    float page = floor(tile * (1.0 / PERLIN_ATLAS_PAGE_LAYERS));
    float3 local = float3(uvl.xy, tile - page * PERLIN_ATLAS_PAGE_LAYERS);
    if (page < 0.5)
        return s_atlas.SampleLevel(s_atlas_s, local, lod);
    if (page < 1.5)
        return s_atlas2.SampleLevel(s_atlas2_s, local, lod);
    if (page < 2.5)
        return s_atlas3.SampleLevel(s_atlas3_s, local, lod);
    return s_atlas4.SampleLevel(s_atlas4_s, local, lod);
}

float4 perlinAtlas(float3 uvl)
{

    float2 gx = ddx(uvl.xy);
    float2 gy = ddy(uvl.xy);
    float tile = floor(uvl.z + 0.5);
    float page = floor(tile * (1.0 / PERLIN_ATLAS_PAGE_LAYERS));
    float3 local = float3(uvl.xy, tile - page * PERLIN_ATLAS_PAGE_LAYERS);
    if (page < 0.5)
        return s_atlas.SampleGrad(s_atlas_s, local, gx, gy);
    if (page < 1.5)
        return s_atlas2.SampleGrad(s_atlas2_s, local, gx, gy);
    if (page < 2.5)
        return s_atlas3.SampleGrad(s_atlas3_s, local, gx, gy);
    return s_atlas4.SampleGrad(s_atlas4_s, local, gx, gy);
}
