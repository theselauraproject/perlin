#pragma once
// glsl/bgfx spellings on top of slang, so the shader bodies ported from bgfx stay close to their sources.
// resource layout (shared by every program, mirrored by the engine's bind layouts):
//   set 0 binding 0  Uniforms, dynamic uniform buffer, one per program
//   set 1 binding 2n texture n, binding 2n+1 its sampler
// vertex attributes: location = semantic index (POSITION 0, NORMAL 1, COLOR0 2, COLOR1 3, TEXCOORD0 4 .. TEXCOORD3 7),
// per-instance attributes start at 8; the d3d semantic is always TEXCOORD<location>

typealias vec2 = float2;
typealias vec3 = float3;
typealias vec4 = float4;
typealias ivec2 = int2;
typealias ivec3 = int3;
typealias ivec4 = int4;
typealias uvec2 = uint2;
typealias uvec3 = uint3;
typealias uvec4 = uint4;
typealias mat3 = float3x3;
typealias mat4 = float4x4;

float2 vec2_splat(float x) { return float2(x, x); }
float3 vec3_splat(float x) { return float3(x, x, x); }
float4 vec4_splat(float x) { return float4(x, x, x, x); }

#define mix lerp
#define fract frac
#define inversesqrt rsqrt
#define dFdx ddx
// framebuffer y points down on every backend here: flip so derivative-based normals match the original orientation
#define dFdy(x) ddy(-(x))
#define atan atan2
#define UNIFORMS_BEGIN [[vk::binding(0, 0)]] cbuffer Uniforms : register(b0, space0) {
#define UNIFORMS_END }

float mod(float x, float y) { return x - y * floor(x / y); }
float2 mod(float2 x, float2 y) { return x - y * floor(x / y); }
float3 mod(float3 x, float3 y) { return x - y * floor(x / y); }
float4 mod(float4 x, float4 y) { return x - y * floor(x / y); }
float2 mod(float2 x, float y) { return x - y * floor(x / y); }
float3 mod(float3 x, float y) { return x - y * floor(x / y); }
float4 mod(float4 x, float y) { return x - y * floor(x / y); }

#define SAMPLER2D(name, texBinding, samplerBinding)                                                         \
    [[vk::binding(texBinding, 1)]] Texture2D name : register(t##texBinding, space1);                       \
    [[vk::binding(samplerBinding, 1)]] SamplerState name##_s : register(s##samplerBinding, space1)
#define SAMPLER2DARRAY(name, texBinding, samplerBinding)                                                    \
    [[vk::binding(texBinding, 1)]] Texture2DArray name : register(t##texBinding, space1);                  \
    [[vk::binding(samplerBinding, 1)]] SamplerState name##_s : register(s##samplerBinding, space1)

#define texture2D(tex, uv) tex.Sample(tex##_s, uv)
#define texture2DLod(tex, uv, lod) tex.SampleLevel(tex##_s, uv, lod)
#define textureLod(tex, uv, lod) tex.SampleLevel(tex##_s, uv, lod)
#define texelFetch(tex, px, lod) tex.Load(int3(px, lod))
#define texture2DArray(tex, uvl) tex.Sample(tex##_s, uvl)
#define texture2DArrayLod(tex, uvl, lod) tex.SampleLevel(tex##_s, uvl, lod)

#define BGFX_SHADER_LANGUAGE_HLSL 1
#define BGFX_SHADER_LANGUAGE_GLSL 0
#define BGFX_SHADER_LANGUAGE_METAL 0
#define BGFX_SHADER_LANGUAGE_SPIRV 0
