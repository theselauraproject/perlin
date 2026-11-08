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
    vec4 u_sunDir;
    vec4 u_time;
    vec4 u_heldLight;
    vec4 u_tiles;
    vec4 u_skyParams;
    vec4 u_dynParams;
    vec4 u_dynLights[8];
    vec4 u_packSettings[8];
    mat4 u_shadowMtx;
    vec4 u_shadowInfo;
    mat4 u_shadowMtxB;
    vec4 u_shadowInfoB;
    mat4 u_shadowMtxE;
    vec4 u_shadowInfoE;
    vec4 u_cloudMap;
    vec4 u_sceneInfo;
    mat4 u_invMvp;
    vec4 u_viewTexel;
#ifdef PERLIN_JAVA_UNIFORMS
    PERLIN_JAVA_UNIFORMS
#endif
UNIFORMS_END

SAMPLER2DARRAY(s_atlas, 0, 1);
SAMPLER2D(s_lightmap, 2, 3);
SAMPLER2D(s_shadow, 4, 5);
SAMPLER2D(s_clouds, 6, 7);
SAMPLER2D(s_tileFlags, 12, 13);
SAMPLER2D(s_shadowTint, 14, 15);
SAMPLER2D(s_shadowB, 16, 17);
SAMPLER2D(s_shadowTintB, 18, 19);
SAMPLER2D(s_shadowEnt, 20, 21);

SAMPLER2D(s_javaAtlas, 40, 41);
SAMPLER2D(s_noise, 42, 43);
SAMPLER2D(s_normals, 44, 45);
SAMPLER2D(s_specular, 46, 47);
SAMPLER2D(s_colortex0, 48, 49);
SAMPLER2D(s_colortex1, 50, 51);
SAMPLER2D(s_colortex2, 52, 53);
SAMPLER2D(s_colortex3, 54, 55);
SAMPLER2D(s_colortex4, 56, 57);
SAMPLER2D(s_colortex5, 58, 59);
SAMPLER2D(s_colortex6, 60, 61);
SAMPLER2D(s_colortex7, 62, 63);
SAMPLER2D(s_colortex8, 64, 65);
SAMPLER2D(s_gaux2, 66, 67);
SAMPLER2D(s_gaux4, 68, 69);
SAMPLER2D(s_colortex9, 70, 71);
SAMPLER2D(s_colortex10, 72, 73);
SAMPLER2D(s_colortex11, 74, 75);
SAMPLER2D(s_colortex12, 76, 77);
SAMPLER2D(s_colortex13, 78, 79);
SAMPLER2D(s_colortex14, 80, 81);
SAMPLER2D(s_colortex15, 82, 83);

SAMPLER2D(s_javaMaterials, 84, 85);

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
