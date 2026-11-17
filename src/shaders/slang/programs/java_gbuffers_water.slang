#include "compat.slang"

#define PERLIN_JAVA_UNIFORMS \
    float u_jFogScale; \
    float u_jFogStart; \
    float u_jaspectRatio; \
    int2 u_jatlasSize; \
    float u_jblindness; \
    int u_jblockEntityId; \
    float3 u_jcameraPosition; \
    float3 u_jcameraPositionFract; \
    int3 u_jcameraPositionInt; \
    float u_jcloudHeight; \
    int u_jcurrentRenderedItemId; \
    float u_jdarknessFactor; \
    float u_jdarknessLightFactor; \
    float4 u_jentityColor; \
    int u_jentityId; \
    float u_jeyeAltitude; \
    int2 u_jeyeBrightness; \
    float u_jeyeBrightnessM; \
    float u_jeyeBrightnessM2; \
    float3 u_jeyePosition; \
    float u_jfar; \
    float3 u_jfogColor; \
    int u_jframeCounter; \
    float u_jframeTime; \
    float u_jframeTimeCounter; \
    float u_jframeTimeSmooth; \
    float u_jframemod2; \
    float u_jframemod4; \
    float u_jframemod600; \
    float u_jframemod8; \
    float4x4 u_jgbufferModelView; \
    float4x4 u_jgbufferModelViewInverse; \
    float4x4 u_jgbufferPreviousModelView; \
    float4x4 u_jgbufferPreviousProjection; \
    float4x4 u_jgbufferProjection; \
    float4x4 u_jgbufferProjectionInverse; \
    bool u_jheavyFog; \
    int u_jheldBlockLightValue; \
    int u_jheldBlockLightValue2; \
    int u_jheldItemId; \
    int u_jheldItemId2; \
    float u_jinBasaltDeltas; \
    float u_jinCrimsonForest; \
    float u_jinDry; \
    float u_jinNetherWastes; \
    float u_jinPaleGarden; \
    float u_jinRainy; \
    float u_jinSnowy; \
    float u_jinSoulValley; \
    float u_jinSulfurCaves; \
    float u_jinWarpedForest; \
    bool u_jisElytraFlying; \
    float u_jisEyeInCave; \
    int u_jisEyeInWater; \
    float4 u_jlightningBoltPosition; \
    float u_jmaxBlindnessDarkness; \
    int u_jmoonPhase; \
    float u_jnear; \
    float u_jnightVision; \
    float3 u_jplayerLookVector; \
    float u_jplayerMood; \
    float3 u_jpreviousCameraPosition; \
    float3 u_jpreviousCameraPositionFract; \
    int3 u_jpreviousCameraPositionInt; \
    float u_jrainFactor; \
    float u_jrainStrength; \
    float3 u_jrelativeEyePosition; \
    float u_jscreenBrightness; \
    float4x4 u_jshadowModelView; \
    float4x4 u_jshadowModelViewInverse; \
    float4x4 u_jshadowProjection; \
    float4x4 u_jshadowProjectionInverse; \
    float3 u_jskyColor; \
    float u_jstarter; \
    float u_jsunAngle; \
    float u_jvelocity; \
    float u_jviewHeight; \
    float u_jviewWidth; \
    float u_jwetness; \
    int u_jworldDay; \
    int u_jworldTime; \
    float4x4 u_jModelViewMatrix; \
    float3x3 u_jNormalMatrix; \
    float4x4 u_jProjectionMatrix; \
    float4x4 u_jTextureMatrix0; \
    float4x4 u_jTextureMatrix1; \

#include "vv_resources.slang"
#undef PERLIN_JAVA_UNIFORMS

SAMPLER2D(s_sceneDepth, 10, 11);

SAMPLER2D(s_depth, 34, 35);

struct VsIn
{
    [[vk::location(0)]] float3 a_position : TEXCOORD0;
    [[vk::location(1)]] float4 a_nPacked : TEXCOORD1;
    [[vk::location(2)]] float4 a_color0 : TEXCOORD2;
    [[vk::location(4)]] float2 a_texcoord0 : TEXCOORD4;
    [[vk::location(5)]] float4 a_texcoord1 : TEXCOORD5;
    [[vk::location(8)]] float a_ePacked : TEXCOORD8;
    [[vk::location(9)]] float2 a_mPacked : TEXCOORD9;
    [[vk::location(10)]] float a_tPacked : TEXCOORD10;
};
struct VsOut
{
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] nointerpolation int v_mat : TEXCOORD0;
    [[vk::location(1)]] float2 v_texCoord : TEXCOORD1;
    [[vk::location(2)]] float2 v_lmCoord : TEXCOORD2;
    [[vk::location(3)]] float2 v_signMidCoordPos : TEXCOORD3;
    [[vk::location(4)]] nointerpolation float2 v_absMidCoordPos : TEXCOORD4;
    [[vk::location(5)]] float3 v_playerPos : TEXCOORD5;
    [[vk::location(6)]] float3 v_normal : TEXCOORD6;
    [[vk::location(7)]] float3 v_viewVector : TEXCOORD7;
    [[vk::location(8)]] float4 v_glColor : TEXCOORD8;
    [[vk::location(9)]] float v_jLayer : TEXCOORD9;
};
struct FsIn
{
    float4 fragCoord : SV_Position;
    [[vk::location(0)]] nointerpolation int v_mat : TEXCOORD0;
    [[vk::location(1)]] float2 v_texCoord : TEXCOORD1;
    [[vk::location(2)]] float2 v_lmCoord : TEXCOORD2;
    [[vk::location(3)]] float2 v_signMidCoordPos : TEXCOORD3;
    [[vk::location(4)]] nointerpolation float2 v_absMidCoordPos : TEXCOORD4;
    [[vk::location(5)]] float3 v_playerPos : TEXCOORD5;
    [[vk::location(6)]] float3 v_normal : TEXCOORD6;
    [[vk::location(7)]] float3 v_viewVector : TEXCOORD7;
    [[vk::location(8)]] float4 v_glColor : TEXCOORD8;
    [[vk::location(9)]] float v_jLayer : TEXCOORD9;
};
struct FsOut
{
    float4 t0 : SV_Target0;
    float4 t1 : SV_Target1;
};

namespace vs_stage
{
static float4 gl_Position;
static float3 a_position;
static float3 a_normal;
static float4 a_color0;
static float2 a_texcoord0;
static float4 a_texcoord1;
static float4 a_mcEntity;
static float4 a_mcMidTexCoord;
static float4 a_atTangent;
static float a_mcChunkFade;
static int mat;
static float2 texCoord;
static float2 lmCoord;
static float2 signMidCoordPos;
static float2 absMidCoordPos;
static float3 playerPos;
static float3 normal;
static float3 viewVector;
static float4 glColor;
static float jLayer;
static float FogScale;
static float FogStart;
static float aspectRatio;
static int2 atlasSize;
static float blindness;
static int blockEntityId;
static float3 cameraPosition;
static float3 cameraPositionFract;
static int3 cameraPositionInt;
static float cloudHeight;
static int currentRenderedItemId;
static float darknessFactor;
static float darknessLightFactor;
static float4 entityColor;
static int entityId;
static float eyeAltitude;
static int2 eyeBrightness;
static float eyeBrightnessM;
static float eyeBrightnessM2;
static float3 eyePosition;
static float far;
static float3 fogColor;
static int frameCounter;
static float frameTime;
static float frameTimeCounter;
static float frameTimeSmooth;
static float framemod2;
static float framemod4;
static float framemod600;
static float framemod8;
static float4x4 gbufferModelView;
static float4x4 gbufferModelViewInverse;
static float4x4 gbufferPreviousModelView;
static float4x4 gbufferPreviousProjection;
static float4x4 gbufferProjection;
static float4x4 gbufferProjectionInverse;
static bool heavyFog;
static int heldBlockLightValue;
static int heldBlockLightValue2;
static int heldItemId;
static int heldItemId2;
static float inBasaltDeltas;
static float inCrimsonForest;
static float inDry;
static float inNetherWastes;
static float inPaleGarden;
static float inRainy;
static float inSnowy;
static float inSoulValley;
static float inSulfurCaves;
static float inWarpedForest;
static bool isElytraFlying;
static float isEyeInCave;
static int isEyeInWater;
static float4 lightningBoltPosition;
static float maxBlindnessDarkness;
static int moonPhase;
static float near;
static float nightVision;
static float3 playerLookVector;
static float playerMood;
static float3 previousCameraPosition;
static float3 previousCameraPositionFract;
static int3 previousCameraPositionInt;
static float rainFactor;
static float rainStrength;
static float3 relativeEyePosition;
static float screenBrightness;
static float4x4 shadowModelView;
static float4x4 shadowModelViewInverse;
static float4x4 shadowProjection;
static float4x4 shadowProjectionInverse;
static float3 skyColor;
static float starter;
static float sunAngle;
static float velocity;
static float viewHeight;
static float viewWidth;
static float wetness;
static int worldDay;
static int worldTime;
static float4x4 u_jModelViewMatrix;
static float3x3 u_jNormalMatrix;
static float4x4 u_jProjectionMatrix;
static float4x4 u_jTextureMatrix0;
static float4x4 u_jTextureMatrix1;
#define GetSunVector() normalize((gbufferModelView * vec4(vec3(-sin(overworldAngle), cos(overworldAngle) * SUN_ROTATION_DATA) * 2000.0, 1.0)).xyz)
#define SUN_ROTATION_DATA vec2(cos(sunPathRotation * 0.01745329251994), -sin(sunPathRotation * 0.01745329251994))

static const float shadowDistance = 192.0;

static const float sunPathRotation = 0.0;

static const int shadowMapResolution = 2048;

static float tAmin;
static float tAlin;
static float hA;
static float tAfrc;
static float tAfrs;
static float tAmix;
static float timeAngle;

static float overworldAngleRaw;
static float overworldAngle;

    vec2 GetLightMapCoordinates() {
        vec2 lmCoord = (u_jTextureMatrix1 * javaLightCoord(a_texcoord1)).xy;
        return clamp((lmCoord - 0.03125) * 1.06667, 0.0, 1.0);
    }

float GetLuminance(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec3 DoLuminanceCorrection(vec3 color) {
    return color / (GetLuminance(color) + 0.0001);
}

vec3 DoReducedLuminanceCorrection(vec3 color, float reduceAmount) {
    return color / mix(GetLuminance(color), 1.0, reduceAmount);
}

float GetBiasFactor(float NdotLM) {
    float NdotLM2 = NdotLM * NdotLM;
    return 1.25 * (1.0 - NdotLM2 * NdotLM2) / NdotLM;
}

float GetHorizonFactor(float XdotU) {
        float horizon = clamp((XdotU + 0.1) * 10.0, 0.0, 1.0);
        horizon *= horizon;
        return horizon * horizon * (3.0 - 2.0 * horizon);
}

bool CheckForColor(vec3 albedo, vec3 check) {
    vec3 dif = albedo - check * 0.003921568;
    return dot(dif, dif) < 0.00001;
}

bool CheckForStick(vec3 albedo) {
    return CheckForColor(albedo, vec3(40, 30, 11)) ||
            CheckForColor(albedo, vec3(73, 54, 21)) ||
            CheckForColor(albedo, vec3(104, 78, 30)) ||
            CheckForColor(albedo, vec3(137, 103, 39));
}

float GetMaxColorDif(vec3 color) {
    vec3 dif = abs(vec3(color.r - color.g, color.g - color.b, color.r - color.b));
    return max(dif.r, max(dif.g, dif.b));
}

float Noise3D(vec3 p) {
    p.z = fract(p.z) * 128.0;
    float iz = floor(p.z);
    float fz = fract(p.z);
    vec2 a_off = vec2(23.0, 29.0) * (iz) / 128.0;
    vec2 b_off = vec2(23.0, 29.0) * (iz + 1.0) / 128.0;
    float a = texture2DLod(s_noise, p.xy + a_off, 0.0).r;
    float b = texture2DLod(s_noise, p.xy + b_off, 0.0).r;
    return mix(a, b, fz);
}

float GetSkyLightFactor(vec2 lmCoordM, vec3 shadowMult) {
            float skyLightFactor = max(lmCoordM.y - 0.7, 0.0) * 3.33333;
                  skyLightFactor *= skyLightFactor;

                skyLightFactor = max(skyLightFactor, dot(shadowMult, shadowMult) * 0.333333);

    return skyLightFactor;
}

float minOf(vec3 x) {
    return min(x.x, min(x.y, x.z));
}

int minOf(ivec3 x) {
    return min(x.x, min(x.y, x.z));
}

float maxOf(vec3 x) {
    return max(x.x, max(x.y, x.z));
}

int maxOf(ivec3 x) {
    return max(x.x, max(x.y, x.z));
}

int min1(int x) {
    return min(x, 1);
}
float min1(float x) {
    return min(x, 1.0);
}
int max0(int x) {
    return max(x, 0);
}
float max0(float x) {
    return max(x, 0.0);
}
int clamp01(int x) {
    return clamp(x, 0, 1);
}
float clamp01(float x) {
    return clamp(x, 0.0, 1.0);
}
vec2 clamp01(vec2 x) {
    return clamp(x, vec2(0.0), vec2(1.0));
}
vec3 clamp01(vec3 x) {
    return clamp(x, vec3(0.0), vec3(1.0));
}

int pow2(int x) {
    return x * x;
}
float pow2(float x) {
    return x * x;
}
vec2 pow2(vec2 x) {
    return x * x;
}
vec3 pow2(vec3 x) {
    return x * x;
}
vec4 pow2(vec4 x) {
    return x * x;
}

int pow3(int x) {
    return pow2(x) * x;
}
float pow3(float x) {
    return pow2(x) * x;
}
vec2 pow3(vec2 x) {
    return pow2(x) * x;
}
vec3 pow3(vec3 x) {
    return pow2(x) * x;
}
vec4 pow3(vec4 x) {
    return pow2(x) * x;
}

float pow1_5(float x) {
    return x - x * pow2(1.0 - x);
}
vec2 pow1_5(vec2 x) {
    return x - x * pow2(1.0 - x);
}
vec3 pow1_5(vec3 x) {
    return x - x * pow2(1.0 - x);
}
vec4 pow1_5(vec4 x) {
    return x - x * pow2(1.0 - x);
}

float sqrt1(float x) {
    return x * (2.0 - x);
}
vec2 sqrt1(vec2 x) {
    return x * (2.0 - x);
}
vec3 sqrt1(vec3 x) {
    return x * (2.0 - x);
}
vec4 sqrt1(vec4 x) {
    return x * (2.0 - x);
}
float sqrt2(float x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec2 sqrt2(vec2 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec3 sqrt2(vec3 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec4 sqrt2(vec4 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
float sqrt3(float x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec2 sqrt3(vec2 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec3 sqrt3(vec3 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec4 sqrt3(vec4 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
float sqrt4(float x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec2 sqrt4(vec2 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec3 sqrt4(vec3 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec4 sqrt4(vec4 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}

float smoothstep1(float x) {
    return x * x * (3.0 - 2.0 * x);
}
vec2 smoothstep1(vec2 x) {
    return x * x * (3.0 - 2.0 * x);
}
vec3 smoothstep1(vec3 x) {
    return x * x * (3.0 - 2.0 * x);
}
vec4 smoothstep1(vec4 x) {
    return x * x * (3.0 - 2.0 * x);
}

float dot3(vec3 x) {
    return dot(x, x);
}

vec3 rgb2hsv(vec3 c)
{
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

static float renderDistance;

static const float shadowMapBias = 1.0 - 25.6 / shadowDistance;
static float noonFactorRaw;
static float noonFactor;
static float nightFactor;
static float invNightFactor;
static float rainFactor2;
static float invRainFactor;
static float invNoonFactor;
static float invNoonFactor2;

static float vsBrightness;

static float nightVisionWithAddedSupport;

static int modifiedWorldDay;
static float syncedTime;

static const float pi = 3.14159265359;
static const float goldenRatio = 1.61803398875;

static const float oceanAltitude = 61.9;

static ivec3 cameraPositionBestInt;
static vec3 cameraPositionBestFract;
static vec3 previousCameraPositionBestFract;

void AddSpecialLightDetail(inout vec3 light, vec3 albedo, float emission) {
	vec3 lightM = max(light, vec3(0.0));
	lightM /= (0.2 + 0.8 * GetLuminance(lightM));
	lightM *= (1.0 / (1.0 + emission)) * 0.22;
	light *= 0.9;
	light += pow2(lightM / (albedo + 0.1));
}

static vec3 blocklightCol;

static vec3 fireSpecialLightColorBase;
static vec3 fireSpecialLightColor;

static vec3 lavaSpecialLightColor;
static vec3 netherPortalSpecialLightColor;
static vec3 redstoneSpecialLightColor;
static vec4 soulFireSpecialColor;
static float candleColorMult;
static float candleExtraLight;

vec4 GetSpecialBlocklightColor(int mat) {

	if (mat < 50) {
		if (mat < 26) {
			if (mat < 14) {
				if (mat < 8) {
					if (mat < 5) {
						if (mat == 2) return vec4(fireSpecialLightColor, 0.0);
							if (mat == 3) return vec4(vec3(1.0, 1.0, 1.0) * 4.0, 0.0);
						if (mat == 4) return vec4(vec3(0.7, 1.5, 2.0) * 3.0, 0.0);
					} else {
						if (mat == 5) return vec4(fireSpecialLightColor, 0.0);
						if (mat == 6) return vec4(vec3(0.7, 1.5, 1.5) * 1.7, 0.0);
						if (mat == 7) return vec4(vec3(1.1, 0.85, 0.35) * 5.0, 0.0);
					}
				} else {
					if (mat < 11) {
						if (mat == 8) return vec4(vec3(0.6, 1.3, 0.6) * 4.5, 0.0);
						if (mat == 9) return vec4(vec3(1.1, 0.5, 0.9) * 4.5, 0.0);
						if (mat == 10) return vec4(vec3(1.7, 0.9, 0.4) * 4.0, 0.0);
					} else {
						if (mat == 11) return vec4(fireSpecialLightColor, 0.0);
						if (mat == 12) return vec4(fireSpecialLightColor, 0.0);
						if (mat == 13) return vec4(lavaSpecialLightColor, 0.8);
					}
				}
			} else {
				if (mat < 20) {
					if (mat < 17) {
						if (mat == 14) return vec4(lavaSpecialLightColor, 0.0);
						if (mat == 15) return vec4(fireSpecialLightColor, 0.0);
						if (mat == 16) return vec4(vec3(1.7, 0.9, 0.4) * 4.0, 0.0);
					} else {
						if (mat == 17) return vec4(vec3(1.7, 0.9, 0.4) * 2.0, 0.0);
						if (mat == 18) return vec4(vec3(1.0, 1.25, 1.5) * 3.4, 0.0);
						if (mat == 19) return vec4(vec3(3.0, 0.9, 0.2) * 3.0, 0.0);
					}
				} else {
					if (mat < 23) {
						if (mat == 20) return vec4(vec3(2.3, 0.9, 0.2) * 3.4, 0.0);
						if (mat == 21) return vec4(fireSpecialLightColor * 0.7, 0.0);
						if (mat == 22) return vec4(fireSpecialLightColor * 0.7, 0.0);
					} else {
						if (mat == 23) return vec4(fireSpecialLightColor * 0.7, 0.0);
						if (mat == 24) return vec4(fireSpecialLightColor * 0.25 * candleColorMult, candleExtraLight);
						if (mat == 25) return vec4(netherPortalSpecialLightColor * 2.0, 0.4);
					}
				}
			}
		} else {
			if (mat < 38) {
				if (mat < 32) {
					if (mat < 29) {
						if (mat == 26) return vec4(netherPortalSpecialLightColor, 0.0);
						if (mat == 27) return soulFireSpecialColor;
						if (mat == 28) return soulFireSpecialColor;
					} else {
						if (mat == 29) return soulFireSpecialColor;
						if (mat == 30) return soulFireSpecialColor;
						if (mat == 31) return vec4(redstoneSpecialLightColor * 0.5, 0.1);
					}
				} else {
					if (mat < 35) {
						if (mat == 32) return vec4(redstoneSpecialLightColor * 0.3, 0.1) * 1.00;
						if (mat == 33) return vec4(vec3(1.4, 1.1, 0.5), 0.05);
							if (mat == 34) return vec4(vec3(0.8, 1.1, 1.1), 0.05);
					} else {
						if (mat == 35) return vec4(redstoneSpecialLightColor * 0.25, 0.0);
						if (mat == 36) return vec4(vec3(0.325, 0.15, 0.425) * 2.0, 0.05);
						if (mat == 37) return vec4(lavaSpecialLightColor * 0.1, 0.1);
					}
				}
			} else {
				if (mat < 44) {
					if (mat < 41) {
						if (mat == 38) return vec4(vec3(2.0, 0.5, 1.5) * 0.3, 0.1);
						if (mat == 39) return vec4(vec3(2.0, 1.0, 1.5) * 0.25, 0.1);
						if (mat == 40) return vec4(vec3(2.5, 1.2, 0.4) * 0.1, 0.1);
					} else {
						if (mat == 41) return vec4(redstoneSpecialLightColor * 0.4, 0.15);
						if (mat == 42) return vec4(vec3(0.75, 0.75, 3.0) * 0.277, 0.15);
						if (mat == 43) return vec4(vec3(1.7, 0.9, 0.4) * 0.45, 0.05) * 1.00;
					}
				} else {
					if (mat < 47) {
						if (mat == 44) return vec4(vec3(1.7, 1.1, 0.2) * 0.45, 0.1) * 1.00;
						if (mat == 45) return vec4(vec3(1.7, 0.8, 0.4) * 0.45, 0.05) * 1.00;
						if (mat == 46) return vec4(vec3(0.75, 0.75, 3.0) * 0.2, 0.1) * 1.00;
					} else {
						if (mat == 47) return vec4(vec3(0.5, 3.5, 0.5) * 0.3, 0.1) * 1.00;
						if (mat == 48) return vec4(vec3(0.5, 2.0, 2.0) * 0.4, 0.15) * 1.00;
						if (mat == 49) return vec4(vec3(1.5, 1.5, 1.5) * 0.3, 0.05) * 1.00;
					}
				}
			}
		}
	} else {
		if (mat < 74) {
			if (mat < 62) {
				if (mat < 56) {
					if (mat < 53) {
						if (mat == 50) return vec4(vec3(1.7, 1.1, 0.2) * 0.45, 0.05) * 1.00;
						if (mat == 51) return vec4(vec3(1.7, 1.1, 0.2) * 0.45, 0.05);
						if (mat == 52) return vec4(vec3(1.8, 0.8, 0.4) * 0.6, 0.15);
					} else {
						if (mat == 53) return vec4(vec3(1.4, 0.2, 1.4) * 0.3, 0.05);
						if (mat == 54) return vec4(vec3(3.1, 1.1, 0.3) * 1.0, 0.1);
						if (mat == 55) return vec4(vec3(1.7, 0.9, 0.4) * 4.0, 0.0);
					}
				} else {
					if (mat < 59) {
						if (mat == 56) return vec4(vec3(1.7, 0.9, 0.4) * 2.0, 0.0);
						if (mat == 57) return vec4(vec3(0.1, 0.3, 0.4) * 0.5, 0.0005);
						if (mat == 58) return vec4(vec3(0.0, 1.4, 1.4) * 4.0, 0.15);
					} else {
						if (mat == 59) return vec4(0.0);
						if (mat == 60) return vec4(vec3(3.1, 1.1, 0.3) * 0.125, 0.0125);
						if (mat == 61) return vec4(vec3(3.0, 0.9, 0.2) * 0.125, 0.0125);
					}
				}
			} else {
				if (mat < 68) {
					if (mat < 65) {
						if (mat == 62) return vec4(vec3(3.5, 0.6, 0.4) * 0.3, 0.05);
						if (mat == 63) return vec4(vec3(0.3, 1.9, 1.5) * 0.3, 0.05);
						if (mat == 64) return vec4(vec3(1.0, 1.0, 1.0) * 0.45, 0.1);
					} else {
						if (mat == 65) return vec4(vec3(3.0, 0.9, 0.2) * 0.125, 0.0125);
						if (mat == 66) return vec4(redstoneSpecialLightColor * 0.05, 0.002);
						if (mat == 67) return vec4(redstoneSpecialLightColor * 0.125, 0.0125);
					}
				} else {
					if (mat < 71) {
						if (mat == 68) return vec4(vec3(0.75), 0.0);
						if (mat == 69) return vec4(vec3(1.3, 1.6, 1.6) * 1.0, 0.1);
						if (mat == 70) return vec4(vec3(1.0, 0.1, 0.1) * candleColorMult, candleExtraLight);
					} else {
						if (mat == 71) return vec4(vec3(1.0, 0.4, 0.1) * candleColorMult, candleExtraLight);
						if (mat == 72) return vec4(vec3(1.0, 1.0, 0.1) * candleColorMult, candleExtraLight);
						if (mat == 73) return vec4(vec3(0.1, 1.0, 0.1) * candleColorMult, candleExtraLight);
					}
				}
			}
		} else {
			if (mat < 86) {
				if (mat < 80) {
					if (mat < 77) {
						if (mat == 74) return vec4(vec3(0.3, 1.0, 0.3) * candleColorMult, candleExtraLight);
						if (mat == 75) return vec4(vec3(0.3, 0.8, 1.0) * candleColorMult, candleExtraLight);
						if (mat == 76) return vec4(vec3(0.5, 0.65, 1.0) * candleColorMult, candleExtraLight);
					} else {
						if (mat == 77) return vec4(vec3(0.1, 0.15, 1.0) * candleColorMult, candleExtraLight);
						if (mat == 78) return vec4(vec3(0.7, 0.3, 1.0) * candleColorMult, candleExtraLight);
						if (mat == 79) return vec4(vec3(1.0, 0.1, 1.0) * candleColorMult, candleExtraLight);
					}
				} else {
					if (mat < 83) {
						if (mat == 80) return vec4(vec3(1.0, 0.4, 1.0) * candleColorMult, candleExtraLight);
						if (mat == 81) return vec4(vec3(2.8, 1.1, 0.2) * 0.125, 0.0125);
						if (mat == 82) return vec4(vec3(2.8, 1.1, 0.2) * 0.3, 0.05);
					} else {
						if (mat == 83) return vec4(vec3(1.6, 1.6, 0.7) * 0.3, 0.05);
						if (mat == 84) return vec4(vec3(0.85, 1.3, 1.0) * 3.9, 0.0);
						if (mat == 85) return vec4(0.0);
					}
				}
			} else {
				if (mat < 92) {
					if (mat < 89) {
						if (mat == 86) return vec4(0.0);
						if (mat == 87) return vec4(0.0);
						if (mat == 88) return vec4(0.0);
					} else {
						if (mat == 89) return vec4(0.0);
						if (mat == 90) return vec4(0.0);
						if (mat == 91) return vec4(0.0);
					}
				} else {
					if (mat < 95) {
						if (mat == 92) return vec4(0.0);
						if (mat == 93) return vec4(0.0);
						if (mat == 94) return vec4(0.0);
					} else {
						if (mat == 95) return vec4(0.0);
						if (mat == 96) return vec4(0.0);
						if (mat == 97) return vec4(0.0);
					}
				}
			}
		}
	}

	return vec4(blocklightCol * 20.0, 0.0);
}

static const vec3 specialTintColor[20] = {

	vec3(1.0),

	vec3(1.0, 0.3, 0.1),

	vec3(1.0, 0.1, 1.0),

	vec3(0.5, 0.65, 1.0),

	vec3(1.0, 1.0, 0.1),

	vec3(0.1, 1.0, 0.1),

	vec3(1.0, 0.4, 1.0),

	vec3(1.0),

	vec3(1.0),

	vec3(0.3, 0.8, 1.0),

	vec3(0.7, 0.3, 1.0),

	vec3(0.1, 0.15, 1.0),

	vec3(1.0, 0.75, 0.5),

	vec3(0.3, 1.0, 0.3),

	vec3(1.0, 0.1, 0.1),

	vec3(1.0),

	vec3(0.5, 0.65, 1.0),

	vec3(1.0),

	vec3(1.0),

	vec3(0.0)
};;

static const vec3 caveFogColorRaw = vec3(0.13, 0.13, 0.15);
static vec3 caveFogColorRaw2;
static vec3 caveFogColor;

static vec3 underwaterColorM1;
static vec3 underwaterColorM2;
static vec3 waterFogColor;

static float netherColorMixer;
static vec3 netherColor;
static vec3 lavaLightColor;

static const vec3 endSkyColor = vec3(0.095, 0.07, 0.15) * 1.65;

static const float rainTexOpacity = 0.25;
static const float snowTexOpacity = 0.5;

static float cloudHeightM;
static float cloudHeightOffset;
static int cloudAlt1i;
static int cloudAlt2i;

static const float OSIEBCA = 1.0 / 255.0;

static vec3 binormal;
static vec3 tangent;

static vec2 jitterOffsets[8];

vec2 TAAJitter(vec2 coord, float w) {
        vec2 offset = jitterOffsets[int(framemod8)] * (w / vec2(viewWidth, viewHeight));
            offset *= 0.125;
        return coord + offset;
}

vec3 GetRawWave(in vec3 pos, float wind) {
    float magnitude = sin(wind * 0.0027 + pos.x + pos.y) * 0.04 + 0.04;
    float d0 = sin(wind * 0.0127);
    float d1 = sin(wind * 0.0089);
    float d2 = sin(wind * 0.0114);
    vec3 wave;
    wave.x = magnitude * sin(wind*0.0224 + d1 + d2 + pos.x - pos.z + pos.y);
    wave.y = magnitude * sin(wind*0.0015 + d2 + d0 + pos.x);
    wave.z = magnitude * sin(wind*0.0063 + d0 + d1 - pos.x + pos.z + pos.y);

    return wave;
}

vec3 GetWave(in vec3 pos, float waveSpeed) {
    float wind = frameTimeCounter * waveSpeed * 1.00;
    vec3 wave = GetRawWave(pos, wind);

    float wavingIntensity = 1.00 * mix(1.0, 100 * 0.01, rainFactor);

    return wave * wavingIntensity;
}

void DoWave_Foliage(inout vec3 playerPos, vec3 worldPos, float waveMult) {
    worldPos.y *= 0.5;

    vec3 wave = GetWave(worldPos, 170.0);
    wave.x = wave.x * 3.0;
    wave.y = 0.0;
    wave.z = wave.z * 8.0 + wave.y * 4.0;

        wave *= clamp(lmCoord.y - 0.87, 0.0, 0.1);

    playerPos.xyz += wave * waveMult;
}

void DoWave_Leaves(inout vec3 playerPos, vec3 worldPos, float waveMult) {
    worldPos *= vec3(0.75, 0.375, 0.75);

    vec3 wave = GetWave(worldPos, 170.0);
    wave *= vec3(4.0, 3.0, 8.0);

    wave *= 1.0 - inSnowy;

        wave *= clamp(lmCoord.y - 0.87, 0.0, 0.1);

    playerPos.xyz += wave * waveMult;
}

void DoWave_Water(inout vec3 playerPos, vec3 worldPos) {
    float waterWaveTime = frameTimeCounter * 6.0 * 1.00;
    worldPos.xz *= 14.0;

    float wave  = sin(waterWaveTime * 0.7 - worldPos.z * 0.14 + worldPos.x * 0.07);
          wave += sin(waterWaveTime * 0.5 - worldPos.z * 0.10 + worldPos.x * 0.05);

        wave *= clamp(lmCoord.y - 0.87, 0.0, 0.1);

    wave = wave * 0.125 - 0.05;

    playerPos.y += wave;

        normal = mix(normal, tangent, wave * 0.01);
}

void DoWave_Lava(inout vec3 playerPos, vec3 worldPos) {
    if (fract(worldPos.y + 0.005) > 0.06) {
        float lavaWaveTime = frameTimeCounter * 3.0 * 1.00;
        worldPos.xz *= 14.0;

        float wave  = sin(lavaWaveTime * 0.7 - worldPos.z * 0.14 + worldPos.x * 0.07);
              wave += sin(lavaWaveTime * 0.5 - worldPos.z * 0.05 + worldPos.x * 0.10);

        playerPos.y += wave * 0.0125;
    }
}

void DoWave(inout vec3 playerPos, int mat) {
    vec3 worldPos = playerPos.xyz + cameraPosition.xyz;

            if (mat == 32000) {
                if (fract(worldPos.y + 0.005) > 0.06)
                DoWave_Water(playerPos.xyz, worldPos);
            }
}

void stageMain() {
    tAmin = fract(sunAngle - 0.033333333);
    tAlin = tAmin < 0.433333333 ? tAmin * 1.15384615385 : tAmin * 0.882352941176 + 0.117647058824;
    hA = tAlin > 0.5 ? 1.0 : 0.0;
    tAfrc = fract(tAlin * 2.0);
    tAfrs = tAfrc*tAfrc*(3.0-2.0*tAfrc);
    tAmix = hA < 0.5 ? 0.3 : -0.1;
    timeAngle = (tAfrc * (1.0-tAmix) + tAfrs * tAmix + hA) * 0.5;
    overworldAngleRaw = fract(timeAngle - 0.25);
    overworldAngle = (overworldAngleRaw + (cos(overworldAngleRaw * 3.14159265358979) * -0.5 + 0.5 - overworldAngleRaw) / 3.0) * 6.28318530717959;
    renderDistance = far;
    noonFactorRaw = max(sin(timeAngle * 6.28318530718), 0.0);
    noonFactor = sqrt(noonFactorRaw);
    nightFactor = max(sin(timeAngle * (-6.28318530718)), 0.0);
    invNightFactor = 1.0 - nightFactor;
    rainFactor2 = rainFactor * rainFactor;
    invRainFactor = 1.0 - rainFactor;
    invNoonFactor = 1.0 - noonFactor;
    invNoonFactor2 = invNoonFactor * invNoonFactor;
    vsBrightness = clamp(screenBrightness, 0.0, 1.0);
    nightVisionWithAddedSupport = screenBrightness > 1.0 ? 1.0 : nightVision;
    modifiedWorldDay = int(mod(worldDay, 100) + 5.0);
    syncedTime = (worldTime + modifiedWorldDay * 24000) * 0.05;
    cameraPositionBestInt = ivec3(floor(cameraPosition));
    cameraPositionBestFract = fract(cameraPosition);
    previousCameraPositionBestFract = fract(previousCameraPosition);
    blocklightCol = vec3(0.1775, 0.104, 0.077) * vec3(1.00, 1.00, 1.00);
    fireSpecialLightColorBase = vec3(2.25, 0.83, 0.27) * 3.7;
    fireSpecialLightColor = fireSpecialLightColorBase;
    lavaSpecialLightColor = vec3(3.25, 0.9, 0.2) * 3.9;
    netherPortalSpecialLightColor = vec3(1.8, 0.4, 2.2) * 0.8;
    redstoneSpecialLightColor = vec3(4.0, 0.1, 0.1);
    soulFireSpecialColor = vec4(vec3(0.3, 2.0, 2.2) * 1.0, 0.3);
    candleColorMult = 2.0;
    candleExtraLight = 0.004;
    caveFogColorRaw2 = caveFogColorRaw * (0.7 + 0.3 * vsBrightness);
    caveFogColor = caveFogColorRaw2;
    underwaterColorM1 = pow(fogColor, vec3(0.33, 0.21, 0.26));
    underwaterColorM2 = underwaterColorM1;
    waterFogColor = underwaterColorM2 * vec3(0.2 + 0.1 * vsBrightness);
    netherColorMixer = inNetherWastes + inCrimsonForest + inWarpedForest + inBasaltDeltas + inSoulValley;
    netherColor = mix(
            fogColor * 0.6 + 0.2 * normalize(fogColor + 0.0001),
            (
                inNetherWastes * vec3(0.4, 0.14, 0.06) + inCrimsonForest * vec3(0.36, 0.07, 0.05) +
                inWarpedForest * vec3(0.18, 0.1, 0.25) + inBasaltDeltas * vec3(0.25, 0.235, 0.23) +
                inSoulValley * vec3(0.1, vec2(0.24))
            ),
            netherColorMixer
        );
    lavaLightColor = vec3(0.15, 0.06, 0.01);
    cloudHeightM = isnan(cloudHeight) ? 192.0 : cloudHeight;
    cloudHeightOffset = cloudHeightM - 192.0;
    cloudAlt1i = int(192 + cloudHeightOffset);
    cloudAlt2i = int(288 + cloudHeightOffset);
    jitterOffsets = vec2[8](
    vec2( 0.125,-0.375),
    vec2(-0.125, 0.375),
    vec2( 0.625, 0.125),
    vec2( 0.375,-0.625),
    vec2(-0.625, 0.625),
    vec2(-0.875,-0.125),
    vec2( 0.375,-0.875),
    vec2( 0.875, 0.875)
);
    texCoord = (u_jTextureMatrix0 * vec4(a_texcoord0, 0.0, 1.0)).xy;
    lmCoord  = GetLightMapCoordinates();

    glColor = a_color0;

    mat = int(a_mcEntity.x + 0.5);

    normal = normalize(u_jNormalMatrix * a_normal);

    vec3 rawBinormal = u_jNormalMatrix * cross(a_atTangent.xyz, a_normal.xyz) * a_atTangent.w;
    binormal = rawBinormal * inversesqrt(max(dot(rawBinormal, rawBinormal), 1e-8));
    vec3 rawTangent = u_jNormalMatrix * a_atTangent.xyz;
    tangent = rawTangent * inversesqrt(max(dot(rawTangent, rawTangent), 1e-8));

    mat3 tbnMatrix = mat3(
        tangent.x, binormal.x, normal.x,
        tangent.y, binormal.y, normal.y,
        tangent.z, binormal.z, normal.z
    );

    viewVector = tbnMatrix * (u_jModelViewMatrix * vec4(a_position, 1.0)).xyz;

        vec2 midCoord = (u_jTextureMatrix0 * a_mcMidTexCoord).xy;
    vec2 texMinMidCoord = texCoord - midCoord;
    signMidCoordPos = sign(texMinMidCoord);
    absMidCoordPos  = abs(texMinMidCoord);

    vec4 position = mul(gbufferModelViewInverse, u_jModelViewMatrix) * vec4(a_position, 1.0);
    playerPos = position.xyz;

        DoWave(position.xyz, mat);

    gl_Position = mul(u_jProjectionMatrix, gbufferModelView) * position;

        gl_Position.xy = TAAJitter(gl_Position.xy, gl_Position.w);

}
}

namespace fs_stage
{
static float4 gl_FragCoord;
static float4 gl_FragData0;
static float4 gl_FragData1;
static int mat;
static float2 texCoord;
static float2 lmCoord;
static float2 signMidCoordPos;
static float2 absMidCoordPos;
static float3 playerPos;
static float3 normal;
static float3 viewVector;
static float4 glColor;
static float jLayer;
static float FogScale;
static float FogStart;
static float aspectRatio;
static int2 atlasSize;
static float blindness;
static int blockEntityId;
static float3 cameraPosition;
static float3 cameraPositionFract;
static int3 cameraPositionInt;
static float cloudHeight;
static int currentRenderedItemId;
static float darknessFactor;
static float darknessLightFactor;
static float4 entityColor;
static int entityId;
static float eyeAltitude;
static int2 eyeBrightness;
static float eyeBrightnessM;
static float eyeBrightnessM2;
static float3 eyePosition;
static float far;
static float3 fogColor;
static int frameCounter;
static float frameTime;
static float frameTimeCounter;
static float frameTimeSmooth;
static float framemod2;
static float framemod4;
static float framemod600;
static float framemod8;
static float4x4 gbufferModelView;
static float4x4 gbufferModelViewInverse;
static float4x4 gbufferPreviousModelView;
static float4x4 gbufferPreviousProjection;
static float4x4 gbufferProjection;
static float4x4 gbufferProjectionInverse;
static bool heavyFog;
static int heldBlockLightValue;
static int heldBlockLightValue2;
static int heldItemId;
static int heldItemId2;
static float inBasaltDeltas;
static float inCrimsonForest;
static float inDry;
static float inNetherWastes;
static float inPaleGarden;
static float inRainy;
static float inSnowy;
static float inSoulValley;
static float inSulfurCaves;
static float inWarpedForest;
static bool isElytraFlying;
static float isEyeInCave;
static int isEyeInWater;
static float4 lightningBoltPosition;
static float maxBlindnessDarkness;
static int moonPhase;
static float near;
static float nightVision;
static float3 playerLookVector;
static float playerMood;
static float3 previousCameraPosition;
static float3 previousCameraPositionFract;
static int3 previousCameraPositionInt;
static float rainFactor;
static float rainStrength;
static float3 relativeEyePosition;
static float screenBrightness;
static float4x4 shadowModelView;
static float4x4 shadowModelViewInverse;
static float4x4 shadowProjection;
static float4x4 shadowProjectionInverse;
static float3 skyColor;
static float starter;
static float sunAngle;
static float velocity;
static float viewHeight;
static float viewWidth;
static float wetness;
static int worldDay;
static int worldTime;
static float4x4 u_jModelViewMatrix;
static float3x3 u_jNormalMatrix;
static float4x4 u_jProjectionMatrix;
static float4x4 u_jTextureMatrix0;
static float4x4 u_jTextureMatrix1;
#define diagonal3(m) vec3((m)[0].x, (m)[1].y, m[2].z)
#define projMAD(m, v) (diagonal3(m) * (v) + transpose(m)[3].xyz)

static const float shadowDistance = 192.0;

static const float sunPathRotation = 0.0;

static const int shadowMapResolution = 2048;

static float tAmin;
static float tAlin;
static float hA;
static float tAfrc;
static float tAfrs;
static float tAmix;
static float timeAngle;

static float overworldAngleRaw;
static float overworldAngle;

float GetLuminance(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec3 DoLuminanceCorrection(vec3 color) {
    return color / (GetLuminance(color) + 0.0001);
}

vec3 DoReducedLuminanceCorrection(vec3 color, float reduceAmount) {
    return color / mix(GetLuminance(color), 1.0, reduceAmount);
}

float GetBiasFactor(float NdotLM) {
    float NdotLM2 = NdotLM * NdotLM;
    return 1.25 * (1.0 - NdotLM2 * NdotLM2) / NdotLM;
}

float GetHorizonFactor(float XdotU) {
        float horizon = clamp((XdotU + 0.1) * 10.0, 0.0, 1.0);
        horizon *= horizon;
        return horizon * horizon * (3.0 - 2.0 * horizon);
}

bool CheckForColor(vec3 albedo, vec3 check) {
    vec3 dif = albedo - check * 0.003921568;
    return dot(dif, dif) < 0.00001;
}

bool CheckForStick(vec3 albedo) {
    return CheckForColor(albedo, vec3(40, 30, 11)) ||
            CheckForColor(albedo, vec3(73, 54, 21)) ||
            CheckForColor(albedo, vec3(104, 78, 30)) ||
            CheckForColor(albedo, vec3(137, 103, 39));
}

float GetMaxColorDif(vec3 color) {
    vec3 dif = abs(vec3(color.r - color.g, color.g - color.b, color.r - color.b));
    return max(dif.r, max(dif.g, dif.b));
}

float Noise3D(vec3 p) {
    p.z = fract(p.z) * 128.0;
    float iz = floor(p.z);
    float fz = fract(p.z);
    vec2 a_off = vec2(23.0, 29.0) * (iz) / 128.0;
    vec2 b_off = vec2(23.0, 29.0) * (iz + 1.0) / 128.0;
    float a = texture2DLod(s_noise, p.xy + a_off, 0.0).r;
    float b = texture2DLod(s_noise, p.xy + b_off, 0.0).r;
    return mix(a, b, fz);
}

float GetSkyLightFactor(vec2 lmCoordM, vec3 shadowMult) {
            float skyLightFactor = max(lmCoordM.y - 0.7, 0.0) * 3.33333;
                  skyLightFactor *= skyLightFactor;

                skyLightFactor = max(skyLightFactor, dot(shadowMult, shadowMult) * 0.333333);

    return skyLightFactor;
}

float minOf(vec3 x) {
    return min(x.x, min(x.y, x.z));
}

int minOf(ivec3 x) {
    return min(x.x, min(x.y, x.z));
}

float maxOf(vec3 x) {
    return max(x.x, max(x.y, x.z));
}

int maxOf(ivec3 x) {
    return max(x.x, max(x.y, x.z));
}

int min1(int x) {
    return min(x, 1);
}
float min1(float x) {
    return min(x, 1.0);
}
int max0(int x) {
    return max(x, 0);
}
float max0(float x) {
    return max(x, 0.0);
}
int clamp01(int x) {
    return clamp(x, 0, 1);
}
float clamp01(float x) {
    return clamp(x, 0.0, 1.0);
}
vec2 clamp01(vec2 x) {
    return clamp(x, vec2(0.0), vec2(1.0));
}
vec3 clamp01(vec3 x) {
    return clamp(x, vec3(0.0), vec3(1.0));
}

int pow2(int x) {
    return x * x;
}
float pow2(float x) {
    return x * x;
}
vec2 pow2(vec2 x) {
    return x * x;
}
vec3 pow2(vec3 x) {
    return x * x;
}
vec4 pow2(vec4 x) {
    return x * x;
}

int pow3(int x) {
    return pow2(x) * x;
}
float pow3(float x) {
    return pow2(x) * x;
}
vec2 pow3(vec2 x) {
    return pow2(x) * x;
}
vec3 pow3(vec3 x) {
    return pow2(x) * x;
}
vec4 pow3(vec4 x) {
    return pow2(x) * x;
}

float pow1_5(float x) {
    return x - x * pow2(1.0 - x);
}
vec2 pow1_5(vec2 x) {
    return x - x * pow2(1.0 - x);
}
vec3 pow1_5(vec3 x) {
    return x - x * pow2(1.0 - x);
}
vec4 pow1_5(vec4 x) {
    return x - x * pow2(1.0 - x);
}

float sqrt1(float x) {
    return x * (2.0 - x);
}
vec2 sqrt1(vec2 x) {
    return x * (2.0 - x);
}
vec3 sqrt1(vec3 x) {
    return x * (2.0 - x);
}
vec4 sqrt1(vec4 x) {
    return x * (2.0 - x);
}
float sqrt2(float x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec2 sqrt2(vec2 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec3 sqrt2(vec3 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec4 sqrt2(vec4 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
float sqrt3(float x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec2 sqrt3(vec2 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec3 sqrt3(vec3 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec4 sqrt3(vec4 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
float sqrt4(float x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec2 sqrt4(vec2 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec3 sqrt4(vec3 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}
vec4 sqrt4(vec4 x) {
    x = 1.0 - x;
    x *= x;
    x *= x;
    x *= x;
    x *= x;
    return 1.0 - x;
}

float smoothstep1(float x) {
    return x * x * (3.0 - 2.0 * x);
}
vec2 smoothstep1(vec2 x) {
    return x * x * (3.0 - 2.0 * x);
}
vec3 smoothstep1(vec3 x) {
    return x * x * (3.0 - 2.0 * x);
}
vec4 smoothstep1(vec4 x) {
    return x * x * (3.0 - 2.0 * x);
}

float dot3(vec3 x) {
    return dot(x, x);
}

vec3 rgb2hsv(vec3 c)
{
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

static float renderDistance;

static const float shadowMapBias = 1.0 - 25.6 / shadowDistance;
static float noonFactorRaw;
static float noonFactor;
static float nightFactor;
static float invNightFactor;
static float rainFactor2;
static float invRainFactor;
static float invNoonFactor;
static float invNoonFactor2;

static float vsBrightness;

static float nightVisionWithAddedSupport;

static int modifiedWorldDay;
static float syncedTime;

static const float pi = 3.14159265359;
static const float goldenRatio = 1.61803398875;

static const float oceanAltitude = 61.9;

static ivec3 cameraPositionBestInt;
static vec3 cameraPositionBestFract;
static vec3 previousCameraPositionBestFract;

void AddSpecialLightDetail(inout vec3 light, vec3 albedo, float emission) {
	vec3 lightM = max(light, vec3(0.0));
	lightM /= (0.2 + 0.8 * GetLuminance(lightM));
	lightM *= (1.0 / (1.0 + emission)) * 0.22;
	light *= 0.9;
	light += pow2(lightM / (albedo + 0.1));
}

static vec3 blocklightCol;

static vec3 fireSpecialLightColorBase;
static vec3 fireSpecialLightColor;

static vec3 lavaSpecialLightColor;
static vec3 netherPortalSpecialLightColor;
static vec3 redstoneSpecialLightColor;
static vec4 soulFireSpecialColor;
static float candleColorMult;
static float candleExtraLight;

vec4 GetSpecialBlocklightColor(int mat) {

	if (mat < 50) {
		if (mat < 26) {
			if (mat < 14) {
				if (mat < 8) {
					if (mat < 5) {
						if (mat == 2) return vec4(fireSpecialLightColor, 0.0);
							if (mat == 3) return vec4(vec3(1.0, 1.0, 1.0) * 4.0, 0.0);
						if (mat == 4) return vec4(vec3(0.7, 1.5, 2.0) * 3.0, 0.0);
					} else {
						if (mat == 5) return vec4(fireSpecialLightColor, 0.0);
						if (mat == 6) return vec4(vec3(0.7, 1.5, 1.5) * 1.7, 0.0);
						if (mat == 7) return vec4(vec3(1.1, 0.85, 0.35) * 5.0, 0.0);
					}
				} else {
					if (mat < 11) {
						if (mat == 8) return vec4(vec3(0.6, 1.3, 0.6) * 4.5, 0.0);
						if (mat == 9) return vec4(vec3(1.1, 0.5, 0.9) * 4.5, 0.0);
						if (mat == 10) return vec4(vec3(1.7, 0.9, 0.4) * 4.0, 0.0);
					} else {
						if (mat == 11) return vec4(fireSpecialLightColor, 0.0);
						if (mat == 12) return vec4(fireSpecialLightColor, 0.0);
						if (mat == 13) return vec4(lavaSpecialLightColor, 0.8);
					}
				}
			} else {
				if (mat < 20) {
					if (mat < 17) {
						if (mat == 14) return vec4(lavaSpecialLightColor, 0.0);
						if (mat == 15) return vec4(fireSpecialLightColor, 0.0);
						if (mat == 16) return vec4(vec3(1.7, 0.9, 0.4) * 4.0, 0.0);
					} else {
						if (mat == 17) return vec4(vec3(1.7, 0.9, 0.4) * 2.0, 0.0);
						if (mat == 18) return vec4(vec3(1.0, 1.25, 1.5) * 3.4, 0.0);
						if (mat == 19) return vec4(vec3(3.0, 0.9, 0.2) * 3.0, 0.0);
					}
				} else {
					if (mat < 23) {
						if (mat == 20) return vec4(vec3(2.3, 0.9, 0.2) * 3.4, 0.0);
						if (mat == 21) return vec4(fireSpecialLightColor * 0.7, 0.0);
						if (mat == 22) return vec4(fireSpecialLightColor * 0.7, 0.0);
					} else {
						if (mat == 23) return vec4(fireSpecialLightColor * 0.7, 0.0);
						if (mat == 24) return vec4(fireSpecialLightColor * 0.25 * candleColorMult, candleExtraLight);
						if (mat == 25) return vec4(netherPortalSpecialLightColor * 2.0, 0.4);
					}
				}
			}
		} else {
			if (mat < 38) {
				if (mat < 32) {
					if (mat < 29) {
						if (mat == 26) return vec4(netherPortalSpecialLightColor, 0.0);
						if (mat == 27) return soulFireSpecialColor;
						if (mat == 28) return soulFireSpecialColor;
					} else {
						if (mat == 29) return soulFireSpecialColor;
						if (mat == 30) return soulFireSpecialColor;
						if (mat == 31) return vec4(redstoneSpecialLightColor * 0.5, 0.1);
					}
				} else {
					if (mat < 35) {
						if (mat == 32) return vec4(redstoneSpecialLightColor * 0.3, 0.1) * 1.00;
						if (mat == 33) return vec4(vec3(1.4, 1.1, 0.5), 0.05);
							if (mat == 34) return vec4(vec3(0.8, 1.1, 1.1), 0.05);
					} else {
						if (mat == 35) return vec4(redstoneSpecialLightColor * 0.25, 0.0);
						if (mat == 36) return vec4(vec3(0.325, 0.15, 0.425) * 2.0, 0.05);
						if (mat == 37) return vec4(lavaSpecialLightColor * 0.1, 0.1);
					}
				}
			} else {
				if (mat < 44) {
					if (mat < 41) {
						if (mat == 38) return vec4(vec3(2.0, 0.5, 1.5) * 0.3, 0.1);
						if (mat == 39) return vec4(vec3(2.0, 1.0, 1.5) * 0.25, 0.1);
						if (mat == 40) return vec4(vec3(2.5, 1.2, 0.4) * 0.1, 0.1);
					} else {
						if (mat == 41) return vec4(redstoneSpecialLightColor * 0.4, 0.15);
						if (mat == 42) return vec4(vec3(0.75, 0.75, 3.0) * 0.277, 0.15);
						if (mat == 43) return vec4(vec3(1.7, 0.9, 0.4) * 0.45, 0.05) * 1.00;
					}
				} else {
					if (mat < 47) {
						if (mat == 44) return vec4(vec3(1.7, 1.1, 0.2) * 0.45, 0.1) * 1.00;
						if (mat == 45) return vec4(vec3(1.7, 0.8, 0.4) * 0.45, 0.05) * 1.00;
						if (mat == 46) return vec4(vec3(0.75, 0.75, 3.0) * 0.2, 0.1) * 1.00;
					} else {
						if (mat == 47) return vec4(vec3(0.5, 3.5, 0.5) * 0.3, 0.1) * 1.00;
						if (mat == 48) return vec4(vec3(0.5, 2.0, 2.0) * 0.4, 0.15) * 1.00;
						if (mat == 49) return vec4(vec3(1.5, 1.5, 1.5) * 0.3, 0.05) * 1.00;
					}
				}
			}
		}
	} else {
		if (mat < 74) {
			if (mat < 62) {
				if (mat < 56) {
					if (mat < 53) {
						if (mat == 50) return vec4(vec3(1.7, 1.1, 0.2) * 0.45, 0.05) * 1.00;
						if (mat == 51) return vec4(vec3(1.7, 1.1, 0.2) * 0.45, 0.05);
						if (mat == 52) return vec4(vec3(1.8, 0.8, 0.4) * 0.6, 0.15);
					} else {
						if (mat == 53) return vec4(vec3(1.4, 0.2, 1.4) * 0.3, 0.05);
						if (mat == 54) return vec4(vec3(3.1, 1.1, 0.3) * 1.0, 0.1);
						if (mat == 55) return vec4(vec3(1.7, 0.9, 0.4) * 4.0, 0.0);
					}
				} else {
					if (mat < 59) {
						if (mat == 56) return vec4(vec3(1.7, 0.9, 0.4) * 2.0, 0.0);
						if (mat == 57) return vec4(vec3(0.1, 0.3, 0.4) * 0.5, 0.0005);
						if (mat == 58) return vec4(vec3(0.0, 1.4, 1.4) * 4.0, 0.15);
					} else {
						if (mat == 59) return vec4(0.0);
						if (mat == 60) return vec4(vec3(3.1, 1.1, 0.3) * 0.125, 0.0125);
						if (mat == 61) return vec4(vec3(3.0, 0.9, 0.2) * 0.125, 0.0125);
					}
				}
			} else {
				if (mat < 68) {
					if (mat < 65) {
						if (mat == 62) return vec4(vec3(3.5, 0.6, 0.4) * 0.3, 0.05);
						if (mat == 63) return vec4(vec3(0.3, 1.9, 1.5) * 0.3, 0.05);
						if (mat == 64) return vec4(vec3(1.0, 1.0, 1.0) * 0.45, 0.1);
					} else {
						if (mat == 65) return vec4(vec3(3.0, 0.9, 0.2) * 0.125, 0.0125);
						if (mat == 66) return vec4(redstoneSpecialLightColor * 0.05, 0.002);
						if (mat == 67) return vec4(redstoneSpecialLightColor * 0.125, 0.0125);
					}
				} else {
					if (mat < 71) {
						if (mat == 68) return vec4(vec3(0.75), 0.0);
						if (mat == 69) return vec4(vec3(1.3, 1.6, 1.6) * 1.0, 0.1);
						if (mat == 70) return vec4(vec3(1.0, 0.1, 0.1) * candleColorMult, candleExtraLight);
					} else {
						if (mat == 71) return vec4(vec3(1.0, 0.4, 0.1) * candleColorMult, candleExtraLight);
						if (mat == 72) return vec4(vec3(1.0, 1.0, 0.1) * candleColorMult, candleExtraLight);
						if (mat == 73) return vec4(vec3(0.1, 1.0, 0.1) * candleColorMult, candleExtraLight);
					}
				}
			}
		} else {
			if (mat < 86) {
				if (mat < 80) {
					if (mat < 77) {
						if (mat == 74) return vec4(vec3(0.3, 1.0, 0.3) * candleColorMult, candleExtraLight);
						if (mat == 75) return vec4(vec3(0.3, 0.8, 1.0) * candleColorMult, candleExtraLight);
						if (mat == 76) return vec4(vec3(0.5, 0.65, 1.0) * candleColorMult, candleExtraLight);
					} else {
						if (mat == 77) return vec4(vec3(0.1, 0.15, 1.0) * candleColorMult, candleExtraLight);
						if (mat == 78) return vec4(vec3(0.7, 0.3, 1.0) * candleColorMult, candleExtraLight);
						if (mat == 79) return vec4(vec3(1.0, 0.1, 1.0) * candleColorMult, candleExtraLight);
					}
				} else {
					if (mat < 83) {
						if (mat == 80) return vec4(vec3(1.0, 0.4, 1.0) * candleColorMult, candleExtraLight);
						if (mat == 81) return vec4(vec3(2.8, 1.1, 0.2) * 0.125, 0.0125);
						if (mat == 82) return vec4(vec3(2.8, 1.1, 0.2) * 0.3, 0.05);
					} else {
						if (mat == 83) return vec4(vec3(1.6, 1.6, 0.7) * 0.3, 0.05);
						if (mat == 84) return vec4(vec3(0.85, 1.3, 1.0) * 3.9, 0.0);
						if (mat == 85) return vec4(0.0);
					}
				}
			} else {
				if (mat < 92) {
					if (mat < 89) {
						if (mat == 86) return vec4(0.0);
						if (mat == 87) return vec4(0.0);
						if (mat == 88) return vec4(0.0);
					} else {
						if (mat == 89) return vec4(0.0);
						if (mat == 90) return vec4(0.0);
						if (mat == 91) return vec4(0.0);
					}
				} else {
					if (mat < 95) {
						if (mat == 92) return vec4(0.0);
						if (mat == 93) return vec4(0.0);
						if (mat == 94) return vec4(0.0);
					} else {
						if (mat == 95) return vec4(0.0);
						if (mat == 96) return vec4(0.0);
						if (mat == 97) return vec4(0.0);
					}
				}
			}
		}
	}

	return vec4(blocklightCol * 20.0, 0.0);
}

static const vec3 specialTintColor[20] = {

	vec3(1.0),

	vec3(1.0, 0.3, 0.1),

	vec3(1.0, 0.1, 1.0),

	vec3(0.5, 0.65, 1.0),

	vec3(1.0, 1.0, 0.1),

	vec3(0.1, 1.0, 0.1),

	vec3(1.0, 0.4, 1.0),

	vec3(1.0),

	vec3(1.0),

	vec3(0.3, 0.8, 1.0),

	vec3(0.7, 0.3, 1.0),

	vec3(0.1, 0.15, 1.0),

	vec3(1.0, 0.75, 0.5),

	vec3(0.3, 1.0, 0.3),

	vec3(1.0, 0.1, 0.1),

	vec3(1.0),

	vec3(0.5, 0.65, 1.0),

	vec3(1.0),

	vec3(1.0),

	vec3(0.0)
};;

static const vec3 caveFogColorRaw = vec3(0.13, 0.13, 0.15);
static vec3 caveFogColorRaw2;
static vec3 caveFogColor;

static vec3 underwaterColorM1;
static vec3 underwaterColorM2;
static vec3 waterFogColor;

static float netherColorMixer;
static vec3 netherColor;
static vec3 lavaLightColor;

static const vec3 endSkyColor = vec3(0.095, 0.07, 0.15) * 1.65;

static const float rainTexOpacity = 0.25;
static const float snowTexOpacity = 0.5;

static ivec2 texelCoord;

static float cloudHeightM;
static float cloudHeightOffset;
static int cloudAlt1i;
static int cloudAlt2i;

static const float OSIEBCA = 1.0 / 255.0;

static vec3 upVec;
static vec3 eastVec;
static vec3 northVec;

static vec3 sunVec;

static float NdotU;
static float NdotUmax0;
static float SdotU;
static float sunFactor;
static float sunVisibility;
static float sunVisibility2;
static float shadowTimeVar1;
static float shadowTimeVar2;
static float shadowTime;

static vec3 lightVec;

float GetLinearDepth(float depth) {
    return (2.0 * near) / (far + near - depth * (far - near));
}

    float Bayer2  (vec2 c) { c = 0.5 * floor(c); return fract(1.5 * fract(c.y) + c.x); }
    float Bayer4  (vec2 c) { return 0.25 * Bayer2  (0.5 * c) + Bayer2(c); }
    float Bayer8  (vec2 c) { return 0.25 * Bayer4  (0.5 * c) + Bayer2(c); }
    float Bayer16 (vec2 c) { return 0.25 * Bayer8  (0.5 * c) + Bayer2(c); }
    float Bayer32 (vec2 c) { return 0.25 * Bayer16 (0.5 * c) + Bayer2(c); }
    float Bayer64 (vec2 c) { return 0.25 * Bayer32 (0.5 * c) + Bayer2(c); }
    float Bayer128(vec2 c) { return 0.25 * Bayer64 (0.5 * c) + Bayer2(c); }
    float Bayer256(vec2 c) { return 0.25 * Bayer128(0.5 * c) + Bayer2(c); }

vec3 ScreenToView(vec3 pos) {
    vec4 iProjDiag = vec4(transpose(gbufferProjectionInverse)[0].x,
                          transpose(gbufferProjectionInverse)[1].y,
                          transpose(gbufferProjectionInverse)[2].zw);
    vec3 p3 = pos * 2.0 - 1.0;
    vec4 viewPos = iProjDiag * p3.xyzz + transpose(gbufferProjectionInverse)[3];
    return viewPos.xyz / viewPos.w;
}

vec3 ViewToPlayer(vec3 pos) {
    return mat3(gbufferModelViewInverse) * pos + transpose(gbufferModelViewInverse)[3].xyz;
}

vec3 PlayerToShadow(vec3 pos) {
    vec3 shadowpos = mat3(shadowModelView) * pos + transpose(shadowModelView)[3].xyz;
    return projMAD(shadowProjection, shadowpos);
}

vec3 ShadowClipToShadowView(vec3 pos) {
    return mat3(shadowProjectionInverse) * pos;
}

vec3 ShadowViewToPlayer(vec3 pos) {
    return mat3(shadowModelViewInverse) * pos;
}

static vec3 noonClearLightColor;
static vec3 noonClearAmbientColor;

static vec3 sunsetClearLightColor;
static vec3 sunsetClearAmbientColor;

static vec3 nightClearLightColor;
static vec3 nightClearAmbientColor;

static vec3 drlcSnowM;
static vec3 drlcDryM;
static vec3 drlcRainM;
static vec3 dayRainLightColor;
static vec3 dayRainAmbientColor;

static vec3 nightRainLightColor;
static vec3 nightRainAmbientColor;

static float noonFactorDM;
static vec3 dayLightColor;
static vec3 dayAmbientColor;

static vec3 clearLightColor;
static vec3 clearAmbientColor;

static float rainShadowVisReduce;

static vec3 rainLightColor;
static vec3 rainAmbientColor;

static vec3 lightColor;
static vec3 ambientColor;

float GetNoHSquared(float radiusTan, float NoL, float NoV, float VoL) {
    float radiusCos = 1.0 / sqrt(1.0 + radiusTan * radiusTan);

    float RoL = 2.0 * NoL * NoV - VoL;
    if (RoL >= radiusCos)
        return 1.0;

    float rOverLengthT = radiusCos * radiusTan / sqrt(1.0 - RoL * RoL);
    float NoTr = rOverLengthT * (NoV - RoL * NoL);
    float VoTr = rOverLengthT * (2.0 * NoV * NoV - 1.0 - RoL * VoL);

    float triple = sqrt(clamp(1.0 - NoL * NoL - NoV * NoV - VoL * VoL + 2.0 * NoL * NoV * VoL, 0.0, 1.0));

    float NoBr = rOverLengthT * triple, VoBr = rOverLengthT * (2.0 * triple * NoV);
    float NoLVTr = NoL * radiusCos + NoV + NoTr, VoLVTr = VoL * radiusCos + 1.0 + VoTr;
    float p = NoBr * VoLVTr, q = NoLVTr * VoLVTr, s = VoBr * NoLVTr;
    float xNum = q * (-0.5 * p + 0.25 * VoBr * NoLVTr);
    float xDenom = p * p + s * ((s - 2.0 * p)) + NoLVTr * ((NoL * radiusCos + NoV) * VoLVTr * VoLVTr +
                   q * (-0.5 * (VoLVTr + VoL * radiusCos) - 0.5));
    float twoX1 = 2.0 * xNum / (xDenom * xDenom + xNum * xNum);
    float sinTheta = twoX1 * xDenom;
    float cosTheta = 1.0 - twoX1 * xNum;
    NoTr = cosTheta * NoTr + sinTheta * NoBr;
    VoTr = cosTheta * VoTr + sinTheta * VoBr;

    float newNoL = NoL * radiusCos + NoTr;
    float newVoL = VoL * radiusCos + VoTr;
    float NoH = NoV + newNoL;
    float HoH = 2.0 * newVoL + 2.0;
    return clamp(NoH * NoH / HoH, 0.0, 1.0);
}

float GGX(vec3 normalM, vec3 viewPos, vec3 lightVec, float NdotLmax0, float smoothnessG) {
    smoothnessG = sqrt1(smoothnessG * 0.9 + 0.1);
    float roughnessP = (1.35 - smoothnessG);
    float roughness = pow2(pow2(roughnessP));

    vec3 halfVec = normalize(lightVec - viewPos);

    float dotLH = clamp(dot(halfVec, lightVec), 0.0, 1.0);
    float dotNV = dot(normalM, -viewPos);

        float dotNH = GetNoHSquared(0.01, NdotLmax0, dotNV, dot(-viewPos, lightVec));

    float denom = dotNH * roughness - dotNH + 1.0;
    float D = roughness / (3.141592653589793 * pow2(denom));
    float f0 = 0.05;
    float F = exp2((-5.55473 * dotLH - 6.98316) * dotLH) * (1.0 - f0) + f0;

    float NdotLmax0M = sqrt3(NdotLmax0 * max0(dot(normal, lightVec)));
    float s_specular = max0(NdotLmax0M * D * F / pow2(dotLH));
    s_specular = s_specular / (0.125 * s_specular + 1.0);

    return s_specular;
}

vec3 GetMinimumLighting(float lightmapYM) {
        vec3 minLighting = vec3(0.005625 + vsBrightness * 0.043);
        minLighting *= vec3(0.45, 0.475, 0.6);
        minLighting *= 1.0 - lightmapYM;

    minLighting += nightVisionWithAddedSupport * vec3(0.5, 0.5, 0.75);

    return max(minLighting, vec3(0.0));
}

vec3 GetShadowPos(vec3 playerPos) {
    vec3 shadowPos = PlayerToShadow(playerPos);
    float distb = sqrt(shadowPos.x * shadowPos.x + shadowPos.y * shadowPos.y);
    float distortFactor = distb * shadowMapBias + (1.0 - shadowMapBias);

    return shadowPos * 0.5 + 0.5;
}

    vec3 SampleShadow(vec3 shadowPos, float colorMult, float colorPow) {
        float shadow0 = javaShadow2D(s_shadow, s_shadow_s, vec3(shadowPos.xy, shadowPos.z)).x;

        vec3 shadowcol = vec3(0.0);
        if (shadow0 < 1.0) {
            float shadow1 = javaShadow2D(s_shadowB, s_shadowB_s, vec3(shadowPos.xy, shadowPos.z)).x;

            if (shadow1 * 0.9999 > shadow0)
            {
                shadowcol = texture2D(s_shadowTint, shadowPos.xy).rgb * shadow1;

                shadowcol *= colorMult;
                shadowcol = pow(shadowcol, vec3(colorPow));
            }
        }

        return shadowcol * (1.0 - shadow0) + shadow0;
    }

    float InterleavedGradientNoiseForShadows() {
        float n = 52.9829189 * fract(0.06711056 * gl_FragCoord.x + 0.00583715 * gl_FragCoord.y);
            return fract(n + goldenRatio * mod(float(frameCounter), 3600.0));
    }

    vec2 offsetDist(float x, int s) {
        float n = fract(x * 2.427) * 3.1415;
        return vec2(cos(n), sin(n)) * 1.4 * x / s;
    }

    vec3 SampleTAAFilteredShadow(vec3 shadowPos, float offset, int shadowSamples, bool leaves, float colorMult, float colorPow) {
        vec3 shadow = vec3(0.0);
        float gradientNoise = InterleavedGradientNoiseForShadows();

            offset *= 1.3875;

        float shadowPosZM = shadowPos.z;
        for (int i = 0; i < shadowSamples; i++) {
            vec2 offset2 = offsetDist(gradientNoise + i, shadowSamples) * offset;
            if (leaves) shadowPosZM = shadowPos.z - 0.12 * offset * (gradientNoise + i) / shadowSamples;
            shadow += SampleShadow(vec3(shadowPos.xy + offset2, shadowPosZM), colorMult, colorPow);
            shadow += SampleShadow(vec3(shadowPos.xy - offset2, shadowPosZM), colorMult, colorPow);
        }

        shadow /= shadowSamples * 2.0;

        return shadow;
    }

static vec2 shadowOffsets[4];

    vec3 SampleFilteredShadow(vec3 shadowPos, float offset, float colorMult, float colorPow) {
        vec3 shadow = vec3(0.0);

        for (int i = 0; i < 4; i++) {
            shadow += SampleShadow(vec3(offset * shadowOffsets[i] + shadowPos.xy, shadowPos.z), colorMult, colorPow);
        }
        shadow += SampleShadow(shadowPos, colorMult, colorPow);

        return shadow * 0.2;
    }

    vec3 SampleBasicFilteredShadow(vec3 shadowPos, float offset) {
        float shadow = 0.0;

        for (int i = 0; i < 4; i++) {
            shadow += javaShadow2D(s_shadow, s_shadow_s, vec3(offset * shadowOffsets[i] + shadowPos.xy, shadowPos.z)).x;
        }

        return vec3(shadow * 0.25);
    }

    vec3 GetShadow(vec3 shadowPos, float lightmapY, float offset, int shadowSamples, bool leaves, vec3 playerPos) {
                    offset *= 1.0 + rainFactor2
                            * 2.0
                    ;

        float lightmapY2 = pow2(lightmapY);
        float lightmapY4 = pow2(lightmapY2);

        float colorMult = 2.5 + 5.5 * pow1_5(lightmapY) + 2.0 * lightmapY2;
        float colorPow = mix(1.5 + 0.5 * float(isEyeInWater == 0), 0.5, lightmapY4);

            vec3 shadow = SampleTAAFilteredShadow(shadowPos, offset, shadowSamples, leaves, colorMult, colorPow);

        return shadow;
    }

vec3 GetHeldLighting(vec3 playerPos, vec3 color, float emission) {
    float heldLight = heldBlockLightValue; float heldLight2 = heldBlockLightValue2;

        if (heldLight > 15.1) heldLight = 0.0;
        if (heldLight2 > 15.1) heldLight2 = 0.0;

        vec3 heldLightCol = blocklightCol; vec3 heldLightCol2 = blocklightCol;

        if (heldItemId == 45032) heldLight = 15; if (heldItemId2 == 45032) heldLight2 = 15;

    vec3 playerPosLightM = playerPos + relativeEyePosition;
         playerPosLightM.y += 0.7;
    float lViewPosL = length(playerPosLightM) + 6.0;

    heldLight = pow2(pow2(heldLight * 0.47 / lViewPosL));
    heldLight2 = pow2(pow2(heldLight2 * 0.47 / lViewPosL));

    vec3 heldLighting = pow2(heldLight * DoLuminanceCorrection(heldLightCol))
                        + pow2(heldLight2 * DoLuminanceCorrection(heldLightCol2));

    return heldLighting;
}

static float moonPhaseFactor;

static float halfCheck;
static float firstHalf;
static float secondHalf;

static float moonPhaseFactor2;
static float moonPhaseInfluence;

static vec3 highlightColor;

void DoLighting(inout vec4 color, inout vec3 shadowMult, vec3 playerPos, vec3 viewPos, float lViewPos, vec3 geoNormal, vec3 normalM, float dither,
                vec3 worldGeoNormal, vec2 s_lightmap, bool noSmoothLighting, bool noDirectionalShading, bool noVanillaAO,
                bool centerShadowBias, int subsurfaceMode, float smoothnessG, float highlightMult, float emission) {

    float NdotN = dot(normalM, northVec);
    float absNdotN = abs(NdotN);
    float NdotE = dot(normalM, eastVec);
    float absNdotE = abs(NdotE);
    float NdotL = dot(normalM, lightVec);

    float lightmapY2 = pow2(s_lightmap.y);
    float lightmapYM = smoothstep1(s_lightmap.y);
    float subsurfaceHighlight = 0.0;
    float ambientMult = 1.0;
    vec3 lightColorM = lightColor;
    vec3 ambientColorM = ambientColor;
    vec3 nViewPos = normalize(viewPos);

        float skyLightShadowMult = pow2(pow2(lightmapY2));

        float NdotLmax0 = max0(NdotL);
        float NdotLM = NdotLmax0 * 0.9999;

                NdotLM = max0(NdotL + 0.4) * 0.714;

        if (shadowMult.r > 0.00001) {
                if (NdotLM > 0.0001) {
                    vec3 shadowMultBeforeLighting = shadowMult;
                    float shadowLength = shadowDistance * 0.9166667 - lViewPos;

                    if (shadowLength > 0.000001) {
                            float offset = 0.00098;

                        vec3 playerPosM = playerPos;
                        vec3 centerPlayerPos = floor(playerPos + cameraPosition + worldGeoNormal * 0.01) - cameraPosition + 0.5;

                            playerPosM = mix(playerPosM, centerPlayerPos, 0.2 * (1.0 - lightmapYM));

                            {
                                float distanceBias = pow(dot(playerPos, playerPos), 0.75);
                                vec3 bias = worldGeoNormal * (0.12 + 0.0008 * distanceBias) * (2.0 - 0.95 * NdotLmax0);

                                playerPosM += bias;
                            }

                        vec3 shadowPos = GetShadowPos(playerPosM);

                        bool leaves = false;

                        int shadowSampleBooster = int(subsurfaceMode > 0 && lViewPos < 10.0);
                            int shadowSamples = 2 + 2 * shadowSampleBooster;

                        shadowMult *= GetShadow(shadowPos, s_lightmap.y, offset, shadowSamples, leaves, playerPos);
                    }

                    float shadowSmooth = 16.0;
                    if (shadowLength < shadowSmooth) {
                        float shadowMixer = max0(shadowLength / shadowSmooth);

                        shadowMult = mix(vec3(skyLightShadowMult * shadowMultBeforeLighting), shadowMult, shadowMixer);
                    }
                }

            shadowMult *= max(NdotLM * shadowTime, 0.0);
        }
            else {
                shadowMult = vec3(pow2(lightmapY2) * max(NdotLM * shadowTime, 0.0));
            }

    float lightmapXM;
    if (!noSmoothLighting) {
        float lightmapXMSteep = pow2(pow2(s_lightmap.x * s_lightmap.x))  * (2.8 - 0.6 * vsBrightness + 1.00);
        float lightmapXMCalm = (s_lightmap.x) * (2.8 + 0.6 * vsBrightness - 1.00);
        lightmapXM = pow(lightmapXMSteep + lightmapXMCalm, 2.25);
    } else {
        float xLightCurveM = 1.00 > 0.999 ? 1.00 : sqrt2(1.00);
        lightmapXM = pow(s_lightmap.x, 3.0 * xLightCurveM) * 10.0;
    }

    float daylightFactor = lightmapYM * invRainFactor * sunVisibility;
    emission *= 1.0 - 0.25 * daylightFactor;

    vec3 blockLighting = lightmapXM * blocklightCol;

            vec3 playerPosForHeldLighting = playerPos;

        vec3 heldLighting = GetHeldLighting(playerPosForHeldLighting, color.rgb, emission);

    vec3 minLighting = GetMinimumLighting(lightmapYM);
    vec3 shadowLightMult = shadowMult;
    float shadowMultFloat = min1(GetLuminance(shadowMult));

        ambientMult = mix(lightmapYM, pow2(lightmapYM) * lightmapYM, rainFactor);

        if (isEyeInWater != 1) {
            float lxFactor = (sunVisibility2 * 0.4 + (0.6 - 0.6 * invNoonFactor2)) * (6.0 - 5.0 * rainFactor);
            lxFactor *= lightmapY2 + lightmapY2 * 2.0 * pow2(shadowMultFloat);
            lxFactor = max0(lxFactor - emission * 1000000.0);
            blockLighting *= pow(lightmapXM / 60.0 + 0.001, 0.09 * lxFactor);

            float rainLF = 0.1 * rainFactor;
            float lightFogTweaks = 1.0 + max0(96.0 - lViewPos) * (0.002 * (1.0 - sunVisibility2) + 0.0104 * rainLF) - rainLF;
            ambientMult *= lightFogTweaks;
            lightColorM *= lightFogTweaks;
        }

    float directionShade = 1.0;
        if (!noDirectionalShading) {
            float absNdotE2 = pow2(absNdotE);

                float NdotUM = 0.75 + NdotU * 0.25;
            float NdotNM = 1.0 + 0.075 * absNdotN;
            float NdotEM = 1.0 - 0.1 * absNdotE2;
            directionShade = NdotUM * NdotEM * NdotNM;

                lightColorM *= 1.0 + absNdotE2 * 0.75;

                ambientColorM = mix(ambientColorM, lightColorM, (0.05 + 0.03 * subsurfaceMode) * absNdotN * lightmapY2);

                lightColorM *= 1.0 + max0(1.0 - subsurfaceMode) * pow(noonFactor, 20.0) * (pow2(absNdotN) * 0.8 - absNdotE2 * 0.2);
        }

    vec3 sceneLighting = lightColorM * shadowLightMult + ambientColorM * ambientMult;
    float dotSceneLighting = dot(sceneLighting, sceneLighting);

        blockLighting = sqrt(pow2(blockLighting) + heldLighting);

    blockLighting *= 1.00;

    float vanillaAO = 1.0;
        vanillaAO = glColor.a;

        if (subsurfaceMode != 0) vanillaAO = mix(min1(vanillaAO * 1.15), 1.0, shadowMultFloat);
        else if (!noVanillaAO) {
            vanillaAO = vanillaAO * 0.9 + 0.1;

        }

    vec3 lightHighlight = vec3(0.0);

    vec3 finalDiffuse = pow2(directionShade * vanillaAO) * (blockLighting + pow2(sceneLighting) + minLighting) + pow2(emission);
    finalDiffuse = sqrt(max(finalDiffuse, vec3(0.0)));

    color.rgb *= finalDiffuse;
    color.rgb += lightHighlight;
    color.rgb *= pow2(1.0 - darknessLightFactor);
}

static vec3 skyColorSqrt;

static float invRainStrength2;
static vec3 skyColorM;
static vec3 skyColorM2;

static vec3 nmscSnowM;
static vec3 nmscDryM;
static vec3 ndscSnowM;
static vec3 ndscDryM;
static vec3 nmscRainM, ndscRainM;
static vec3 nuscWeatherM;
static vec3 nmscWeatherM;
static vec3 ndscWeatherM;

static vec3 noonUpSkyColor;
static vec3 noonMiddleSkyColor;
static vec3 noonDownSkyColor;

static vec3 sunsetUpSkyColor;
static vec3 sunsetMiddleSkyColor;
static vec3 sunsetDownSkyColorP;
static vec3 sunsetDownSkyColor;

static vec3 dayUpSkyColor;
static vec3 dayMiddleSkyColor;
static vec3 dayDownSkyColor;

static vec3 nightColFactor;
static vec3 nightUpSkyColor;
static vec3 nightMiddleSkyColor;
static vec3 nightDownSkyColor;

    float GetCaveFactor() {
        float caveFactor = clamp(1.0 - cameraPosition.y / oceanAltitude, 0.0, 1.0 - eyeBrightnessM);
        return caveFactor;
    }

    vec3 GetSky(float VdotU, float VdotS, float dither, bool doGlare, bool doGround) {

        float nightFactorSqrt2 = sqrt2(nightFactor);
        float nightFactorM = sqrt2(nightFactorSqrt2) * 0.4;
        float VdotSM1 = pow2(max(VdotS, 0.0));
        float VdotSM2 = pow2(VdotSM1);
        float VdotSM3 = pow2(pow2(max(-VdotS, 0.0)));
        float VdotSML = sunVisibility > 0.5 ? VdotS : -VdotS;

        float VdotUmax0 = max(VdotU, 0.0);
        float VdotUmax0M = 1.0 - pow2(VdotUmax0);

        vec3 upColor = mix(nightUpSkyColor * (1.5 - 0.5 * nightFactorSqrt2 + nightFactorM * VdotSM3 * 1.5), dayUpSkyColor, sunFactor);
        vec3 middleColor = mix(nightMiddleSkyColor * (3.0 - 2.0 * nightFactorSqrt2), dayMiddleSkyColor * (1.0 + VdotSM2 * 0.3), sunFactor);
        vec3 downColor = mix(nightDownSkyColor, dayDownSkyColor, (sunFactor + sunVisibility) * 0.5);

            float VdotUM1 = pow2(1.0 - VdotUmax0);
                  VdotUM1 = pow(VdotUM1, (1.0 - VdotSM2 * 0.4) * (1.15 - 0.15 * rainFactor2));
                  VdotUM1 = mix(VdotUM1, 1.0, rainFactor2 * 0.15);
            vec3 finalSky = mix(upColor, middleColor, VdotUM1);

            float VdotUM2 = pow2(1.0 - abs(VdotU));
                  VdotUM2 = VdotUM2 * VdotUM2 * (3.0 - 2.0 * VdotUM2);
                  VdotUM2 *= (0.7 - nightFactorM + VdotSM1 * (0.3 + nightFactorM)) * invNoonFactor * sunFactor;
            finalSky = mix(finalSky, sunsetDownSkyColorP * (1.0 + VdotSM1 * 0.3), VdotUM2 * invRainFactor);

            float VdotUM3 = min(max0(-VdotU + 0.08) / 0.35, 1.0);
                  VdotUM3 = smoothstep1(VdotUM3);
            vec3 scatteredGroundMixer = vec3(VdotUM3 * VdotUM3, sqrt1(VdotUM3), sqrt3(VdotUM3));
                 scatteredGroundMixer = mix(vec3(VdotUM3), scatteredGroundMixer, 0.75 - 0.5 * rainFactor);
            finalSky = mix(finalSky, downColor, scatteredGroundMixer);

        if (doGround)
            finalSky *= smoothstep1(pow2(1.0 + min(VdotU, 0.0)));

        if (isEyeInWater == 1)
            finalSky = mix(finalSky * 3.0, waterFogColor, VdotUmax0M);

        if (doGlare) {
            if (0.0 < VdotSML) {
                float glareScatter = 3.0 * (2.0 - clamp01(VdotS * 1000.0));
                float VdotSM4 = pow(abs(VdotS), glareScatter);

                float visfactor = 0.075;
                float glare = visfactor / (1.0 - (1.0 - visfactor) * VdotSM4) - visfactor;
                glare *= 0.7;

                float glareWaterFactor = isEyeInWater * sunVisibility;
                vec3 glareColor = mix(vec3(0.38, 0.4, 0.5) * 0.3, vec3(1.5, 0.7, 0.3) + vec3(0.0, 0.5, 0.5) * noonFactor, sunVisibility);
                     glareColor = glareColor + glareWaterFactor * vec3(7.0);

                    glare *= 1.0 - 0.6 * rainFactor;
                    float glareDesaturateFactor = 0.5 * rainFactor;
                glareColor = mix(glareColor, vec3(GetLuminance(glareColor)), glareDesaturateFactor);

                finalSky += glareColor * glare * shadowTime;
            }
        }

            finalSky = mix(finalSky, caveFogColor, GetCaveFactor() * VdotUmax0M);

        finalSky = max(finalSky + (dither - 0.5) / 128.0, vec3(0.0));

        return finalSky;
    }

    vec3 GetLowQualitySky(float VdotU, float VdotS, float dither, bool doGlare, bool doGround) {

        float VdotUmax0 = max(VdotU, 0.0);
        float VdotUmax0M = 1.0 - pow2(VdotUmax0);

        vec3 upColor = mix(nightUpSkyColor, dayUpSkyColor, sunFactor);
        vec3 middleColor = mix(nightMiddleSkyColor, dayMiddleSkyColor, sunFactor);

            float VdotUM1 = pow2(1.0 - VdotUmax0);
                  VdotUM1 = mix(VdotUM1, 1.0, rainFactor2 * 0.2);
            vec3 finalSky = mix(upColor, middleColor, VdotUM1);

            float VdotUM2 = pow2(1.0 - abs(VdotU));
                  VdotUM2 *= invNoonFactor * sunFactor * (0.8 + 0.2 * VdotS);
            finalSky = mix(finalSky, sunsetDownSkyColorP * (shadowTime * 0.6 + 0.2), VdotUM2 * invRainFactor);

        finalSky *= pow2(pow2(1.0 + min(VdotU, 0.0)));

        if (isEyeInWater == 1)
            finalSky = mix(finalSky, waterFogColor, VdotUmax0M);

        finalSky *= 1.0 + mix(nightFactor, 0.5 + 0.7 * noonFactor, VdotS * 0.5 + 0.5) * pow2(pow2(pow2(VdotS)));

            finalSky = mix(finalSky, caveFogColor, GetCaveFactor() * VdotUmax0M);

        return finalSky;
    }

    void DoBorderFog(inout vec4 color, inout float skyFade, float lPos, float VdotU, float VdotS, float dither) {
            float fog = lPos / renderDistance;
            fog = pow2(pow2(fog));
                fog = pow2(pow2(fog));
            fog = 1.0 - exp(-3.0 * fog);

        if (fog > 0.01 + 0.0025 * dither) {
            fog = clamp(fog, 0.0, 1.0);

                vec3 fogColorM = GetSky(VdotU, VdotS, dither, true, false);

            color = mix(color, vec4(fogColorM, 0.0), fog);

                skyFade = fog * (1.0 - isEyeInWater);
        }
    }

    void DoCaveFog(inout vec4 color, float lViewPos) {
        float fog = GetCaveFactor() * (0.9 - 0.9 * exp(- lViewPos * 0.015));

        color = mix(color, vec4(caveFogColor, 0.0), fog);
    }

static float atmFogCRFTM;

        vec3 GetAtmFogColor(float altitudeFactorRaw, float VdotS) {
            float nightFogMult = 2.5 - 0.625 * max(pow2(pow2(altitudeFactorRaw)), rainFactor);
            float dayNightFogBlend = pow(invNightFactor, 4.0 - VdotS - 2.5 * sunVisibility2);
            return mix(
                nightUpSkyColor * (nightFogMult - dayNightFogBlend * nightFogMult),
                dayDownSkyColor * (0.9 + 0.3 * noonFactor)
                ,
                dayNightFogBlend
            );
        }

    float GetAtmFogAltitudeFactor(float altitude) {
        float altitudeFactor = pow2(1.0 - clamp(altitude - 63 + 0.1, 0.0, atmFogCRFTM) / atmFogCRFTM);
        return altitudeFactor;
    }

    void DoAtmosphericFog(inout vec4 color, vec3 playerPos, float lViewPos, float VdotS, float dither) {
            float renDisFactor = min1(192.0 / renderDistance);

            float fog = 1.0 - exp(-pow(lViewPos * (0.001 - 0.0007 * rainFactor), 2.0 - rainFactor2) * lViewPos * renDisFactor);

        float altitudeFactorRaw = GetAtmFogAltitudeFactor(playerPos.y + cameraPosition.y);
        float altitudeFactor = altitudeFactorRaw * 0.9 + 0.1;

        fog *= 0.95 - 0.1 - 0.15 * invRainFactor;

            float cameraAltitudeFactor = GetAtmFogAltitudeFactor(cameraPosition.y + 0.25 * atmFogCRFTM);
            altitudeFactor = mix(altitudeFactor, 1.0, 0.8 * cameraAltitudeFactor);

            altitudeFactor *= 1.0 - 0.5 * cameraAltitudeFactor * invRainFactor;

                if (isEyeInWater == 0) {
                        float factor = max(inSnowy, inDry);

                    float fogFactor = 4.0;
                        fogFactor += 2.0 * inDry;
                    fogFactor *= 0.5 + 0.5 * sunVisibility;

                    float fogIntense = pow2(1.0 - exp(-lViewPos * fogFactor / 100));
                    fog = mix(fog, fogIntense / altitudeFactor, 0.8 * rainFactor * factor);
                }

                fog *= 0.2 + 0.8 * sqrt2(eyeBrightnessM);
                fog *= 1.0 - GetCaveFactor();

        fog *= altitudeFactor;

        if (fog > 0.01 + 0.0025 * dither) {
            fog = clamp(fog, 0.0, 1.0);

                vec3 fogColorM = GetAtmFogColor(altitudeFactorRaw, VdotS);

            color = mix(color, vec4(fogColorM, 0.0), fog);
        }
    }

    float GetWaterFog(float lViewPos) {

            float fog = lViewPos / 48.0;
            fog *= fog;

        return 1.0 - exp(-fog);
    }

void DoWaterFog(inout vec4 color, float lViewPos) {
    float fog = GetWaterFog(lViewPos);

    color = mix(color, vec4(waterFogColor, 0), fog);
}

void DoLavaFog(inout vec4 color, float lViewPos) {
        float fog = (lViewPos * 3.0 - u_jFogStart) * u_jFogScale;

        fog = 1.0 - exp(-fog);

        fog = clamp(fog, 0.0, 1.0);

    color = mix(color, vec4(fogColor * 5.0, 0.0), fog);
}

void DoPowderSnowFog(inout vec4 color, float lViewPos) {
    float fog = lViewPos;

    fog *= fog;
    fog = 1.0 - exp(-fog);

    fog = clamp(fog, 0.0, 1.0);
    color = mix(color, vec4(fogColor, 0.0), fog);
}

void DoBlindnessFog(inout vec4 color, float lViewPos) {
    float fog = lViewPos * 0.3 * blindness;
    fog *= fog;
    fog = 1.0 - exp(-fog);

    fog = clamp(fog, 0.0, 1.0);
    color *= 1.0 - fog;
}

void DoDarknessFog(inout vec4 color, float lViewPos) {
    float fog = lViewPos * 0.075 * darknessFactor;
    fog *= fog;
    fog *= fog;
    color *= exp(-fog);
}

void DoFog(inout vec4 color, inout float skyFade, float lViewPos, vec3 playerPos, float VdotU, float VdotS, float dither, bool isReflection, float lBlockPos) {
        DoCaveFog(color, lViewPos);
        float lViewPosAtm = lViewPos;

        if (isReflection) lViewPosAtm *= max0(1.0 - lBlockPos / lViewPos);
        DoAtmosphericFog(color, playerPos, lViewPosAtm, VdotS, dither);
        DoBorderFog(color, skyFade, max(length(playerPos.xz), abs(playerPos.y)), VdotU, VdotS, dither);

    if (isEyeInWater == 1) DoWaterFog(color, lViewPos);
    else if (isEyeInWater == 2) DoLavaFog(color, lViewPos);
    else if (isEyeInWater == 3) DoPowderSnowFog(color, lViewPos);

    if (blindness > 0.00001) DoBlindnessFog(color, lViewPos);
    if (darknessFactor > 0.00001) DoDarknessFog(color, lViewPos);
}

void DoTranslucentTweaks(inout vec4 color, inout float fresnelM, inout float reflectMult, float lViewPos) {
        float tweakDistance = 192.0;

        float factor = smoothstep(0.0, tweakDistance, lViewPos);

        color.a = mix(color.a, 1.0, factor * 0.75);
        fresnelM = mix(fresnelM, 1.0, factor * 0.25);
        reflectMult = mix(reflectMult, reflectMult / color.a, factor);

    float factor2 = smoothstep(1.8, 0.3, lViewPos);
    reflectMult = mix(reflectMult, 0.0, factor2);

    float factor3 = smoothstep(4.0, 0.5, lViewPos);
    color.a = mix(color.a, pow2(color.a), factor3 * 0.5);
}

vec2 Reprojection(vec3 pos, vec3 cameraOffset) {
    pos = pos * 2.0 - 1.0;

    vec4 viewPosPrev = gbufferProjectionInverse * vec4(pos, 1.0);
    viewPosPrev /= viewPosPrev.w;
    viewPosPrev = gbufferModelViewInverse * viewPosPrev;

    vec4 previousPosition = viewPosPrev + vec4(cameraOffset, 0.0);
    previousPosition = gbufferPreviousModelView * previousPosition;
    previousPosition = gbufferPreviousProjection * previousPosition;
    return previousPosition.xy / previousPosition.w * 0.5 + 0.5;
}

vec3 FHalfReprojection(vec3 pos) {
    pos = pos * 2.0 - 1.0;

    vec4 viewPosPrev = gbufferProjectionInverse * vec4(pos, 1.0);
    viewPosPrev /= viewPosPrev.w;
    viewPosPrev = gbufferModelViewInverse * viewPosPrev;

    return viewPosPrev.xyz;
}

vec2 SHalfReprojection(vec3 playerPos, vec3 cameraOffset) {
    vec4 proPos = vec4(playerPos + cameraOffset, 1.0);
    vec4 previousPosition = gbufferPreviousModelView * proPos;
    previousPosition = gbufferPreviousProjection * previousPosition;
    return previousPosition.xy / previousPosition.w * 0.5 + 0.5;
}

float GetApproxDistance(float depth) {
    return near * far / (far - depth * far);
}

vec3 nvec3(vec4 pos) {
    return pos.xyz/pos.w;
}

static float refDist;

void AddBackgroundReflection(inout vec4 reflection, vec3 color, vec3 playerPos, vec3 normalM, vec3 normalMR, vec3 nViewPos, vec3 nViewPosR,
                             vec3 shadowMult, float RVdotU, float RVdotS, float z0, float dither, float skyLightFactor, float smoothness, float highlightMult) {
            vec3 skyReflection = GetSky(RVdotU, RVdotS, dither, isEyeInWater == 0, true);

            float specularHighlight = GGX(normalM, nViewPos, lightVec, max(dot(normalM, lightVec), 0.0), smoothness);
            skyReflection += specularHighlight * highlightColor * shadowMult * highlightMult * invRainFactor;

                skyReflection = mix(color * 0.5, skyReflection, skyLightFactor);

    reflection.rgb = mix(skyReflection, reflection.rgb, reflection.a);
}

vec4 GetReflection(inout vec3 normalM, vec3 viewPos, vec3 nViewPos, vec3 playerPos, float lViewPos, float z0, Texture2D depthtex, float dither, float skyLightFactor, float fresnel, float smoothness, vec3 geoNormal, vec3 color, vec3 shadowMult, float highlightMult, SamplerState depthtex_s) {

        vec2 rEdge = vec2(0.6, 0.55);
    vec3 normalMR = normalM;

    vec3 nViewPosR = normalize(reflect(nViewPos, normalMR));
    float RVdotU = dot(nViewPosR, upVec);
    float RVdotS = dot(nViewPosR, sunVec);

    vec4 reflection = vec4(0.0);

            vec3 start = viewPos + normalMR * (lViewPos * 0.025 * (1.0 - fresnel) + 0.05);
                vec3 vector = nViewPosR;
            vector *= 0.5;
            vec3 vectorBase = vector;
            vec3 viewPosRT = viewPos + vector;
            vec3 tvector = vector;

                int sampleCount = 30;
                int refinementCount = 6;

            int sr = 0;
            float dist = 0.0;
            vec3 refPos = vec3(0.0);
            vec3 rfragpos = vec3(0.0);
            float err = 9999999.0;
            float sampleDepth = 1.0;
            for (int i = 0; i < sampleCount; i++) {
                refPos = nvec3(gbufferProjection * vec4(viewPosRT, 1.0)) * 0.5 + 0.5;
                if (abs(refPos.x - 0.5) > rEdge.x  ||  abs(refPos.y - 0.5) > rEdge.y) break;

                sampleDepth = texture2D(depthtex, refPos.xy).r;
                rfragpos = nvec3(gbufferProjectionInverse * vec4(vec3(refPos.xy, sampleDepth) * 2.0 - 1.0, 1.0));

                dist = length(start - rfragpos);

                err = length(viewPosRT - rfragpos);
                if (err * 0.33333 < length(vector)) {
                    sr++;
                    if (sr >= refinementCount) break;
                    tvector -= vector;
                    vector *= 0.1;
                }
                vector *= 2.0;
                tvector += vector * (0.95 + 0.1 * dither);
                viewPosRT = start + tvector;
            }

            float lViewPosRT = length(rfragpos);
            refPos.z = sampleDepth;

                vec3 viewRefPos = ScreenToView(refPos);

                bool possibleReflection = dot(normalize(viewRefPos - start), geoNormal) > 0.0;

            if (
                refPos.z < 1.0
                 &&  possibleReflection
                 &&  (err * (1.0 - fresnel) < 1.0 + lViewPosRT * 0.2)
            ) {
                vec2 absPos = abs(refPos.xy - 0.5);
                vec2 cdist = absPos / rEdge;
                float border = clamp(1.0 - pow(max(cdist.x, cdist.y), 50.0), 0.0, 1.0);
                reflection.a = border;

                if (reflection.a > 0.001) {
                    vec2 edgeFactor = pow2(pow2(pow2(cdist)));
                        refPos.y += (dither - 0.5) * (0.05 * (edgeFactor.x + edgeFactor.y));

                            reflection = vec4(texture2D(s_gaux2, refPos.xy).rgb, 1.0);
                        reflection.rgb = pow2(reflection.rgb * 2.0);

                    float skyFade = 0.0;

                        float reflectionPrevAlpha = reflection.a;
                        DoFog(reflection, skyFade, lViewPosRT, ViewToPlayer(rfragpos.xyz), RVdotU, RVdotS, dither, true, lViewPos);
                        reflection.a = reflectionPrevAlpha;

                    edgeFactor.x = pow2(edgeFactor.x);
                    edgeFactor = 1.0 - edgeFactor;
                    float refFactor = pow(edgeFactor.x * edgeFactor.y, 2.0 + 3.0 * GetLuminance(reflection.rgb));
                    reflection.a *= refFactor;
                    refDist = dist;
                }

                float posDif = lViewPosRT - lViewPos;
                reflection.a *= clamp(posDif + 3.0, 0.0, 1.0);
            }

        if (reflection.a < 1.0)
    {
        AddBackgroundReflection(reflection, color, playerPos, normalM, normalMR, nViewPos, nViewPosR,
                                shadowMult, RVdotU, RVdotS, z0, dither, skyLightFactor, smoothness, highlightMult);
    }

    return reflection;
}

static vec2 jitterOffsets[8];

vec2 TAAJitter(vec2 coord, float w) {
        vec2 offset = jitterOffsets[int(framemod8)] * (w / vec2(viewWidth, viewHeight));
            offset *= 0.125;
        return coord + offset;
}

void stageMain() {
    tAmin = fract(sunAngle - 0.033333333);
    tAlin = tAmin < 0.433333333 ? tAmin * 1.15384615385 : tAmin * 0.882352941176 + 0.117647058824;
    hA = tAlin > 0.5 ? 1.0 : 0.0;
    tAfrc = fract(tAlin * 2.0);
    tAfrs = tAfrc*tAfrc*(3.0-2.0*tAfrc);
    tAmix = hA < 0.5 ? 0.3 : -0.1;
    timeAngle = (tAfrc * (1.0-tAmix) + tAfrs * tAmix + hA) * 0.5;
    overworldAngleRaw = fract(timeAngle - 0.25);
    overworldAngle = (overworldAngleRaw + (cos(overworldAngleRaw * 3.14159265358979) * -0.5 + 0.5 - overworldAngleRaw) / 3.0) * 6.28318530717959;
    renderDistance = far;
    noonFactorRaw = max(sin(timeAngle * 6.28318530718), 0.0);
    noonFactor = sqrt(noonFactorRaw);
    nightFactor = max(sin(timeAngle * (-6.28318530718)), 0.0);
    invNightFactor = 1.0 - nightFactor;
    rainFactor2 = rainFactor * rainFactor;
    invRainFactor = 1.0 - rainFactor;
    invNoonFactor = 1.0 - noonFactor;
    invNoonFactor2 = invNoonFactor * invNoonFactor;
    vsBrightness = clamp(screenBrightness, 0.0, 1.0);
    nightVisionWithAddedSupport = screenBrightness > 1.0 ? 1.0 : nightVision;
    modifiedWorldDay = int(mod(worldDay, 100) + 5.0);
    syncedTime = (worldTime + modifiedWorldDay * 24000) * 0.05;
    cameraPositionBestInt = ivec3(floor(cameraPosition));
    cameraPositionBestFract = fract(cameraPosition);
    previousCameraPositionBestFract = fract(previousCameraPosition);
    blocklightCol = vec3(0.1775, 0.104, 0.077) * vec3(1.00, 1.00, 1.00);
    fireSpecialLightColorBase = vec3(2.25, 0.83, 0.27) * 3.7;
    fireSpecialLightColor = fireSpecialLightColorBase;
    lavaSpecialLightColor = vec3(3.25, 0.9, 0.2) * 3.9;
    netherPortalSpecialLightColor = vec3(1.8, 0.4, 2.2) * 0.8;
    redstoneSpecialLightColor = vec3(4.0, 0.1, 0.1);
    soulFireSpecialColor = vec4(vec3(0.3, 2.0, 2.2) * 1.0, 0.3);
    candleColorMult = 2.0;
    candleExtraLight = 0.004;
    caveFogColorRaw2 = caveFogColorRaw * (0.7 + 0.3 * vsBrightness);
    caveFogColor = caveFogColorRaw2;
    underwaterColorM1 = pow(fogColor, vec3(0.33, 0.21, 0.26));
    underwaterColorM2 = underwaterColorM1;
    waterFogColor = underwaterColorM2 * vec3(0.2 + 0.1 * vsBrightness);
    netherColorMixer = inNetherWastes + inCrimsonForest + inWarpedForest + inBasaltDeltas + inSoulValley;
    netherColor = mix(
            fogColor * 0.6 + 0.2 * normalize(fogColor + 0.0001),
            (
                inNetherWastes * vec3(0.4, 0.14, 0.06) + inCrimsonForest * vec3(0.36, 0.07, 0.05) +
                inWarpedForest * vec3(0.18, 0.1, 0.25) + inBasaltDeltas * vec3(0.25, 0.235, 0.23) +
                inSoulValley * vec3(0.1, vec2(0.24))
            ),
            netherColorMixer
        );
    lavaLightColor = vec3(0.15, 0.06, 0.01);
    texelCoord = ivec2(gl_FragCoord.xy);
    cloudHeightM = isnan(cloudHeight) ? 192.0 : cloudHeight;
    cloudHeightOffset = cloudHeightM - 192.0;
    cloudAlt1i = int(192 + cloudHeightOffset);
    cloudAlt2i = int(288 + cloudHeightOffset);
    upVec = normalize(transpose(gbufferModelView)[1].xyz);
    eastVec = normalize(transpose(gbufferModelView)[0].xyz);
    northVec = normalize(transpose(gbufferModelView)[2].xyz);
    sunVec = GetSunVector();
    NdotU = dot(normal, upVec);
    NdotUmax0 = max(NdotU, 0.0);
    SdotU = dot(sunVec, upVec);
    sunFactor = SdotU < 0.0 ? clamp(SdotU + 0.375, 0.0, 0.75) / 0.75 : clamp(SdotU + 0.03125, 0.0, 0.0625) / 0.0625;
    sunVisibility = clamp(SdotU + 0.0625, 0.0, 0.125) / 0.125;
    sunVisibility2 = sunVisibility * sunVisibility;
    shadowTimeVar1 = abs(sunVisibility - 0.5) * 2.0;
    shadowTimeVar2 = shadowTimeVar1 * shadowTimeVar1;
    shadowTime = shadowTimeVar2 * shadowTimeVar2;
    lightVec = sunVec * ((timeAngle < 0.5325 || timeAngle > 0.9675) ? 1.0 : -1.0);
    noonClearLightColor = vec3(0.65, 0.55, 0.375) * 2.05;
    noonClearAmbientColor = pow(skyColor, vec3(0.75)) * 0.85;
    sunsetClearLightColor = pow(vec3(0.64, 0.45, 0.3), vec3(1.5 + invNoonFactor)) * 5.0;
    sunsetClearAmbientColor = noonClearAmbientColor * vec3(1.21, 0.92, 0.76) * 0.95;
    nightClearLightColor = 0.9 * vec3(0.15, 0.14, 0.20) * (0.4 + vsBrightness * 0.4);
    nightClearAmbientColor = 0.9 * vec3(0.09, 0.12, 0.17) * (1.55 + vsBrightness * 0.77);
    drlcSnowM = inSnowy * vec3(-0.06, 0.0, 0.04);
    drlcDryM = inDry * vec3(0.01, -0.035, -0.06);
    drlcRainM = vec3(0.0);
    dayRainLightColor = vec3(0.21, 0.16, 0.13) * 0.85 + noonFactor * vec3(0.0, 0.02, 0.06)
                                 + drlcRainM + drlcSnowM + drlcDryM;
    dayRainAmbientColor = vec3(0.2, 0.2, 0.25) * (1.8 + 0.5 * vsBrightness);
    nightRainLightColor = vec3(0.03, 0.035, 0.05) * (0.5 + 0.5 * vsBrightness);
    nightRainAmbientColor = vec3(0.16, 0.20, 0.3) * (0.75 + 0.6 * vsBrightness);
    noonFactorDM = noonFactor;
    dayLightColor = mix(sunsetClearLightColor, noonClearLightColor, noonFactorDM);
    dayAmbientColor = mix(sunsetClearAmbientColor, noonClearAmbientColor, noonFactorDM);
    clearLightColor = mix(nightClearLightColor, dayLightColor, sunVisibility2);
    clearAmbientColor = mix(nightClearAmbientColor, dayAmbientColor, sunVisibility2);
    rainShadowVisReduce = 0.0
                    + 0.2 * inSnowy + 0.2 * inDry;
    rainLightColor = mix(nightRainLightColor, dayRainLightColor * (1.0 - rainShadowVisReduce), sunVisibility2) * 2.5;
    rainAmbientColor = mix(nightRainAmbientColor, dayRainAmbientColor * (1.0 + rainShadowVisReduce), sunVisibility2);
    lightColor = mix(clearLightColor, rainLightColor, rainFactor);
    ambientColor = mix(clearAmbientColor, rainAmbientColor, rainFactor);
    shadowOffsets = vec2[4](
        vec2( 1.0, 0.0),
        vec2( 0.0, 1.0),
        vec2(-1.0, 0.0),
        vec2( 0.0,-1.0));
    moonPhaseFactor = abs(moonPhase - 4.0) * 0.25;
    halfCheck = step(0.5, moonPhaseFactor);
    firstHalf = mix(0.60, 0.85, smoothstep(0.0, 0.5, moonPhaseFactor));
    secondHalf = mix(0.85, 1.00, smoothstep(0.5, 1.0, moonPhaseFactor));
    moonPhaseFactor2 = mix(firstHalf, secondHalf, halfCheck);
    moonPhaseInfluence = mix(1.0, moonPhaseFactor2, 1.0 - sunVisibility2);
    highlightColor = normalize(pow(lightColor, vec3(0.37))) * (0.3 + 1.5 * sunVisibility2) * (1.0 - 0.85 * rainFactor);
    skyColorSqrt = sqrt(skyColor);
    invRainStrength2 = (1.0 - rainStrength) * (1.0 - rainStrength);
    skyColorM = mix(max(skyColorSqrt, vec3(0.63, 0.67, 0.73)), skyColorSqrt, invRainStrength2);
    skyColorM2 = mix(max(skyColor, sunFactor * vec3(0.265, 0.295, 0.35)), skyColor, invRainStrength2);
    nmscSnowM = inSnowy * vec3(-0.1, 0.3, 0.6);
    nmscDryM = inDry * vec3(-0.1, -0.2, -0.3);
    ndscSnowM = inSnowy * vec3(-0.25, -0.01, 0.25);
    ndscDryM = inDry * vec3(-0.05, -0.09, -0.1);
    nmscRainM = vec3(0.0);
    ndscRainM = vec3(0.0);
    nuscWeatherM = vec3(0.1, 0.0, 0.1);
    nmscWeatherM = vec3(-0.1, -0.4, -0.6) + vec3(0.0, 0.06, 0.12) * noonFactor;
    ndscWeatherM = vec3(-0.15, -0.3, -0.42) + vec3(0.0, 0.02, 0.08) * noonFactor;
    noonUpSkyColor = pow(skyColorM, vec3(2.9)) * (vec3(0.85, 0.92, 0.81) + rainFactor * nuscWeatherM);
    noonMiddleSkyColor = skyColorM * (vec3(1.4, 1.3, 1.3) + rainFactor * (nmscWeatherM + nmscRainM + nmscSnowM + nmscDryM))
                                + noonUpSkyColor * 0.25;
    noonDownSkyColor = skyColorM * (vec3(0.9) + rainFactor * (ndscWeatherM + ndscRainM + ndscSnowM + ndscDryM))
                                + noonUpSkyColor * 0.25;
    sunsetUpSkyColor = skyColorM2 * (vec3(0.72, 0.522, 0.47) + vec3(0.1, 0.2, 0.35) * rainFactor2);
    sunsetMiddleSkyColor = skyColorM2 * (vec3(1.8, 1.3, 1.2) + vec3(0.15, 0.25, -0.05) * rainFactor2);
    sunsetDownSkyColorP = vec3(1.45, 0.86, 0.5) - vec3(0.8, 0.3, 0.0) * rainFactor;
    sunsetDownSkyColor = sunsetDownSkyColorP * 0.5 + 0.25 * sunsetMiddleSkyColor;
    dayUpSkyColor = mix(noonUpSkyColor, sunsetUpSkyColor, invNoonFactor2);
    dayMiddleSkyColor = mix(noonMiddleSkyColor, sunsetMiddleSkyColor, invNoonFactor2);
    dayDownSkyColor = mix(noonDownSkyColor, sunsetDownSkyColor, invNoonFactor2);
    nightColFactor = 0.9 * vec3(0.07, 0.14, 0.24) * (1.0 - 0.5 * rainFactor) + skyColor;
    nightUpSkyColor = pow(nightColFactor, vec3(0.90)) * 0.45;
    nightMiddleSkyColor = sqrt(nightUpSkyColor) * 0.65;
    nightDownSkyColor = nightMiddleSkyColor * vec3(0.82, 0.82, 0.88);
    atmFogCRFTM = 60.0;
    refDist = far;
    jitterOffsets = vec2[8](
    vec2( 0.125,-0.375),
    vec2(-0.125, 0.375),
    vec2( 0.625, 0.125),
    vec2( 0.375,-0.625),
    vec2(-0.625, 0.625),
    vec2(-0.875,-0.125),
    vec2( 0.375,-0.875),
    vec2( 0.875, 0.875)
);
        vec4 colorP = perlinAtlas(float3(texCoord, jLayer));

        vec4 color = colorP * vec4(glColor.rgb, 1.0);

    vec3 screenPos = vec3(gl_FragCoord.xy / vec2(viewWidth, viewHeight), gl_FragCoord.z);
        vec3 viewPos = ScreenToView(vec3(TAAJitter(screenPos.xy, -0.5), screenPos.z));
    float lViewPos = length(viewPos);

    float dither = Bayer64(gl_FragCoord.xy);
        dither = fract(dither + goldenRatio * mod(float(frameCounter), 3600.0));

        float cloudLinearDepth = texelFetch(s_gaux2, texelCoord, 0).a;

        if (pow2(cloudLinearDepth + OSIEBCA * dither) * renderDistance < min(lViewPos, renderDistance)) discard;

    float materialMask = 0.0;

    vec3 nViewPos = normalize(viewPos);
    float VdotU = dot(nViewPos, upVec);
    float VdotS = dot(nViewPos, sunVec);
    float VdotN = dot(nViewPos, normal);

    vec4 translucentMult = vec4(1.0);
    bool noSmoothLighting = false, noDirectionalShading = false, translucentMultCalculated = false, noGeneratedNormals = false;
    int subsurfaceMode = 0;
    vec2 lmCoordM = lmCoord;
    float smoothnessG = 0.0, highlightMult = 1.0, reflectMult = 0.0, emission = 0.0;
    vec3 normalM = VdotN > 0.0 ? -normal : normal;
    vec3 geoNormal = normalM;
    vec3 worldGeoNormal = normalize(ViewToPlayer(geoNormal * 10000.0));
    vec3 shadowMult = vec3(1.0);
    float fresnel = clamp(1.0 + dot(normalM, nViewPos), 0.0, 1.0);
    float fresnelM = pow3(fresnel);

if (mat < 32008) {
    if (mat < 30016) {
        if (mat < 30008) {
            if (mat == 30000) {

            } else if (mat == 30004) {

            }
        } else {
            if (mat == 30008) {
                color.a = pow(color.a, 1.0 - fresnelM);
                reflectMult = 1.0;

                    DoTranslucentTweaks(color, fresnelM, reflectMult, lViewPos);
            } else   {
                translucentMultCalculated = true;
                reflectMult = 0.7;
                translucentMult.rgb = pow2(color.rgb) * 0.2;

                smoothnessG = color.g * 0.7;
                highlightMult = 2.5;
            }
        }
    } else {
        if (mat < 32000) {
            if (mat < 31000) {
                if (mat == 30016) {
                    translucentMultCalculated = true;
                    reflectMult = 1.0;
                    translucentMult.rgb = pow2(color.rgb) * 0.2;

                    smoothnessG = color.r * 0.7;
                    highlightMult = 2.5;
                } else   {

lmCoordM = vec2(0.0);

    color = vec4(0.0);

    int sampleCount = 8;

    float multiplier = 0.4 / (-viewVector.z * sampleCount);
    vec2 interval = viewVector.xy * multiplier;
    vec2 coord = signMidCoordPos * 0.5 + 0.5;
    vec2 absMidCoordPos2 = absMidCoordPos * 2.0;
    vec2 midCoord = texCoord - absMidCoordPos * signMidCoordPos;
    vec2 minimumMidCoordPos = midCoord - absMidCoordPos;

    for (int i = 0; i < sampleCount; i++) {
        float portalStep = (i + dither) / sampleCount;
        coord += interval * portalStep;
        vec2 sampleCoord = fract(coord) * absMidCoordPos2 + minimumMidCoordPos;
        vec4 psample = perlinAtlasLod(float3(sampleCoord, jLayer), 0);

        float factor = 1.0 - portalStep;
        psample *= pow(factor, 0.1);

        emission = max(emission, psample.r);

        color += psample;
    }
    color /= sampleCount;

color.rgb *= color.rgb * vec3(1.25, 1.0, 0.65);
color.a = sqrt1(color.a) * 0.8;

emission *= emission;
emission *= emission;
emission *= emission;
emission = clamp(emission * 120.0, 0.03, 1.2) * 8.0;

    color.a *= min1(lViewPos - 0.2);
    if (color.a < 0.101) {
        if (color.a < 0.101 * dither) discard;
        else color.a = 0.101;
    }

                }
            } else {
                if (mat % 2 == 0) {

smoothnessG = 0.5;
highlightMult = 1.5;
reflectMult = 1.0;

DoTranslucentTweaks(color, fresnelM, reflectMult, lViewPos);
                } else   {

smoothnessG = 0.5;
highlightMult = 1.5;
reflectMult = 1.0;

DoTranslucentTweaks(color, fresnelM, reflectMult, lViewPos);
                    noSmoothLighting = true;
                }
            }
        } else {
            if (mat < 32004) {

        color.rgb = mix(color.rgb, vec3(GetLuminance(color.rgb)), 0.88);
        color.rgb = pow2(color.rgb) * vec3(2.3, 3.5, 3.1) * 0.9;

    lmCoordM.y = min(lmCoord.y * 1.07, 1.0);

    float fresnel2 = pow2(fresnel);
    float fresnel4 = pow2(fresnel2);

    reflectMult = 1.0;

    float rawWind = frameTimeCounter * 1.10 * 0.018;
    vec2 wind = vec2(0.0, -rawWind);
    vec3 worldPos = playerPos + cameraPosition;
    vec2 waterPos = worldPos.xz;
        float blockRes = absMidCoordPos.x * atlasSize.x * 2.0;
        waterPos = floor(waterPos * blockRes) / blockRes;
    waterPos = 0.032 * (waterPos + worldPos.y * 2.0);

        if (isEyeInWater != 1) {

            float noise = texture2DLod(s_noise, (waterPos + wind) * 0.25, 0.0).g;
                  noise = noise - 0.5;
                  noise *= 0.25;
            color.rgb = pow(color.rgb, vec3(1.0 + noise));

                float depthT = texelFetch(s_depth, texelCoord, 0).r;
            vec3 screenPosT = vec3(screenPos.xy, depthT);
                vec3 viewPosT = ScreenToView(vec3(TAAJitter(screenPosT.xy, -0.5), screenPosT.z));
            float lViewPosT = length(viewPosT);

            float lViewPosDifM = lViewPos - lViewPosT;

                color.a = sqrt1(color.a);

            float waterFog = max0(1.0 - exp(lViewPosDifM * 0.075));
            color.a *= 0.25 + 0.75 * waterFog;

                color.rgb *= 2.5 - sqrt2(waterFog) - 0.5 * lmCoordM.y;

                if (mat == 32000)
                if (NdotU > 0.99) {
                    vec3 matrixM = vec3(
                        transpose(gbufferModelViewInverse)[0].y,
                        transpose(gbufferModelViewInverse)[1].y,
                        transpose(gbufferModelViewInverse)[2].y
                    );
                    float playerPosTY = dot(matrixM, viewPosT) + transpose(gbufferModelViewInverse)[3].y;
                    float yPosDif = playerPosTY - playerPos.y;

                        float foamThreshold = pow2(texture2DLod(s_noise, waterPos * 4.0 + wind * 0.5, 0.0).g) * 1.6;
                    float foam = pow2(clamp((foamThreshold + yPosDif) / foamThreshold, 0.0, 1.0));
                        foam *= 0.4 + 0.25 * lmCoord.y;
                    foam *= clamp((fract(worldPos.y) - 0.7) * 10.0, 0.0, 1.0);

                    vec4 foamColor = vec4(0.9, 0.95, 1.05, 1.0);

                    color = mix(color, foamColor, foam);
                    reflectMult = 1.0 - foam;
                }

        } else {
            noDirectionalShading = true;

            reflectMult = 0.5;

                    translucentMult.rgb *= 1.0 - fresnel4;

        }

    reflectMult *= 0.5 + 0.5 * NdotUmax0;

    color.a = mix(color.a, 1.0, fresnel4);

        smoothnessG = 0.5;

            highlightMult = min(pow2(pow2(dot(colorP.rgb, colorP.rgb) * 0.4)), 0.5);
        highlightMult *= (16.0 - 15.0 * fresnel2) * (sunVisibility > 0.5 ? 0.85 : 0.425);

            } else   {
                smoothnessG = pow2(color.g) * color.g;
                highlightMult = pow2(min1(pow2(color.g) * 1.5)) * 3.5;

                reflectMult = 0.7;
            }
        }
    }
} else {
    if (mat < 32024) {
        if (mat < 32016) {
            if (mat == 32008) {

    float minAlpha = min(color.a, perlinAtlasLod(float3(texCoord, jLayer), 0).a);

if (minAlpha > 0.001) {
    smoothnessG = 1.0;
    highlightMult = 3.5;
    reflectMult = 0.5;

    translucentMultCalculated = true;
    translucentMult = vec4(0.0, 0.0, 0.0, 1.0);
}

else {
        smoothnessG = 0.5;
        highlightMult = 2.5;
        reflectMult = 1.0;
        color.rgb = vec3(0.75, 0.8, 0.85);

        color.a = max(color.a, 0.25);

        DoTranslucentTweaks(color, fresnelM, reflectMult, lViewPos);

            float dither = texture2DLod(s_noise, gl_FragCoord.xy / 128.0, 0.0).b;
                dither = fract(dither + goldenRatio * mod(float(frameCounter), 3600.0));

            if (dither > eyeBrightnessM2 * 1.1 - 0.05) discard;
}
            } else   {
                if (color.a < 0.001  &&  abs(NdotU) > 0.95) discard;

    float minAlpha = min(color.a, perlinAtlasLod(float3(texCoord, jLayer), 0).a);

if (minAlpha > 0.001) {
    smoothnessG = 1.0;
    highlightMult = 3.5;
    reflectMult = 0.5;

    translucentMultCalculated = true;
    translucentMult = vec4(0.0, 0.0, 0.0, 1.0);
}

else {
        smoothnessG = 0.5;
        highlightMult = 2.5;
        reflectMult = 1.0;
        color.rgb = vec3(0.75, 0.8, 0.85);

        color.a = max(color.a, 0.25);

        DoTranslucentTweaks(color, fresnelM, reflectMult, lViewPos);

            float dither = texture2DLod(s_noise, gl_FragCoord.xy / 128.0, 0.0).b;
                dither = fract(dither + goldenRatio * mod(float(frameCounter), 3600.0));

            if (dither > eyeBrightnessM2 * 1.1 - 0.05) discard;
}
                noSmoothLighting = true;
            }
        } else {
            if (mat == 32016) {
                lmCoordM.x = 0.88;

                translucentMultCalculated = true;
                translucentMult = vec4(0.0, 0.0, 0.0, 1.0);

                if (color.b > 0.5) {
                    if (color.g - color.b < 0.01  &&  color.g < 0.99) {

    float minAlpha = min(color.a, perlinAtlasLod(float3(texCoord, jLayer), 0).a);

if (minAlpha > 0.001) {
    smoothnessG = 1.0;
    highlightMult = 3.5;
    reflectMult = 0.5;

    translucentMultCalculated = true;
    translucentMult = vec4(0.0, 0.0, 0.0, 1.0);
}

else {
        smoothnessG = 0.5;
        highlightMult = 2.5;
        reflectMult = 1.0;
        color.rgb = vec3(0.75, 0.8, 0.85);

        color.a = max(color.a, 0.25);

        DoTranslucentTweaks(color, fresnelM, reflectMult, lViewPos);

            float dither = texture2DLod(s_noise, gl_FragCoord.xy / 128.0, 0.0).b;
                dither = fract(dither + goldenRatio * mod(float(frameCounter), 3600.0));

            if (dither > eyeBrightnessM2 * 1.1 - 0.05) discard;
}
                    } else {
                        lmCoordM = vec2(0.0);
                        noDirectionalShading = true;

                        float lColor = length(color.rgb);
                        vec3 baseColor = vec3(0.1, 1.0, 0.92);
                        if (lColor > 1.65)      color.rgb = baseColor + 0.2;
                        else if (lColor > 1.5)  color.rgb = baseColor + 0.15;
                        else if (lColor > 1.3)  color.rgb = baseColor + 0.08;
                        else if (lColor > 1.15) color.rgb = baseColor + 0.035;
                        else                    color.rgb = baseColor;
                        emission = 3.5;
                    }
                } else {
                    float factor = color.r * 1.5;

                    smoothnessG = factor;
                    highlightMult = 2.0 + min1(smoothnessG * 2.0) * 1.5;
                    smoothnessG = min1(smoothnessG);
                }

            } else   {

            }
        }
    } else {
        if (mat < 32032) {
            if (mat == 32024) {

            } else   {

            }
        } else {
            if (mat == 32032) {

            } else   {

            }
        }
    }
}

    if (!translucentMultCalculated)
        translucentMult = vec4(mix(vec3(0.666), color.rgb * (1.0 - pow2(pow2(color.a))), color.a), 1.0);

    translucentMult.rgb = mix(translucentMult.rgb, vec3(1.0), min1(pow2(pow2(lViewPos / far))));

    DoLighting(color, shadowMult, playerPos, viewPos, lViewPos, geoNormal, normalM, dither,
               worldGeoNormal, lmCoordM, noSmoothLighting, noDirectionalShading, false,
               false, subsurfaceMode, smoothnessG, highlightMult, emission);

    float skyLightFactor = GetSkyLightFactor(lmCoordM, shadowMult);
            highlightColor *= pow2(moonPhaseInfluence);

        fresnelM = (fresnelM * 0.85 + 0.15) * reflectMult;

        vec4 reflection = GetReflection(normalM,  viewPos.xyz,  nViewPos,  playerPos,  lViewPos,  -1.0,
                                        s_depth,  dither,  skyLightFactor,  fresnel,
                                        smoothnessG,  geoNormal,  color.rgb,  shadowMult,  highlightMult, s_depth_s);
        color.rgb = mix(color.rgb, reflection.rgb, fresnelM);

    float skyFade = 0.0;
    float prevAlpha = color.a;
    color.a = 1.0;
    DoFog(color, skyFade, lViewPos, playerPos, VdotU, VdotS, dither, false, 0.0);
    float fogAlpha = color.a;
    color.a = prevAlpha * (1.0 - skyFade);

    gl_FragData0 = color;
    gl_FragData1 = vec4(1.0 - translucentMult.rgb, translucentMult.a);

}
}

float2 operator*(float2x2 m, float2 v) { return mul(m, v); }
float2 operator*(float2 v, float2x2 m) { return mul(v, m); }
float3 operator*(float3x3 m, float3 v) { return mul(m, v); }
float3 operator*(float3 v, float3x3 m) { return mul(v, m); }
float4 operator*(float4x4 m, float4 v) { return mul(m, v); }
float4 operator*(float4 v, float4x4 m) { return mul(v, m); }
float2x2 operator*(float2x2 a, float2x2 b) { return mul(a, b); }
float3x3 operator*(float3x3 a, float3x3 b) { return mul(a, b); }
float4x4 operator*(float4x4 a, float4x4 b) { return mul(a, b); }

float4 javaShadow2D(Texture2D t, SamplerState s, float3 uvw)
{ if (u_shadowInfo.w <= 0.0 || any(uvw.xyz < 0.0) || any(uvw.xyz > 1.0)) return float4(1.0); return float4(step(uvw.z, t.Sample(s, uvw.xy).r)); }
float4 javaShadow2D(Texture2D t, SamplerState s, float4 uvw)
{ if (u_shadowInfo.w <= 0.0 || any(uvw.xyz < 0.0) || any(uvw.xyz > 1.0)) return float4(1.0); return float4(step(uvw.z, t.Sample(s, uvw.xy).r)); }
float javaShadow2DProj(Texture2D t, SamplerState s, float4 uvw)
{ float w = max(uvw.w, 1e-6); return step(uvw.z / w, t.Sample(s, uvw.xy / w).r); }
float javaTextureProj(Texture2D t, SamplerState s, float4 uvw)
{ float w = max(uvw.w, 1e-6); return t.Sample(s, uvw.xy / w).r; }
int2 javaTextureSize(Texture2D t, int lod) { uint w = 0, h = 0, n = 0; t.GetDimensions(uint(lod), w, h, n); return int2(w, h); }

float4 javaLightCoord(float4 t) { return float4((t.xy * 255.0 + 8.0) / 256.0, t.zw); }

VsOut vsMain(VsIn input)
{
    vs_stage::a_position = input.a_position;
    vs_stage::a_normal = normalize(input.a_nPacked.xyz * 2.0 - 1.0);
    vs_stage::a_color0 = input.a_color0;
    vs_stage::a_texcoord0 = input.a_texcoord0;
    vs_stage::a_texcoord1 = input.a_texcoord1;
    vs_stage::a_mcEntity = float4(input.a_ePacked, 0.0, 0.0, 0.0);
    vs_stage::a_mcMidTexCoord = float4(input.a_mPacked, 0.0, 0.0);
    { float ti = floor(input.a_tPacked); float ix = floor(ti * 0.5); float hd = (ti - ix * 2.0) * 2.0 - 1.0;
      float3 ax = ix < 0.5 ? float3(1.0, 0.0, 0.0) : ix < 1.5 ? float3(-1.0, 0.0, 0.0) :
          ix < 2.5 ? float3(0.0, 1.0, 0.0) : ix < 3.5 ? float3(0.0, -1.0, 0.0) :
          ix < 4.5 ? float3(0.0, 0.0, 1.0) : float3(0.0, 0.0, -1.0);
      vs_stage::a_atTangent = float4(ax, hd); }
    vs_stage::a_mcChunkFade = 1.0;
    { float ao = fract(input.a_tPacked) * 16.0; ao = ao > 0.0 ? ao : 1.0;
      vs_stage::a_color0.rgb /= max(ao * input.a_nPacked.w, 1.0 / 255.0); vs_stage::a_color0.a = ao; }
    { int id = int(input.a_ePacked + 0.5); vs_stage::a_mcEntity.x = texelFetch(s_javaMaterials, int2(id & 255, id >> 8), 0).r; }
    vs_stage::FogScale = u_jFogScale;
    vs_stage::FogStart = u_jFogStart;
    vs_stage::aspectRatio = u_jaspectRatio;
    vs_stage::atlasSize = u_jatlasSize;
    vs_stage::blindness = u_jblindness;
    vs_stage::blockEntityId = u_jblockEntityId;
    vs_stage::cameraPosition = u_jcameraPosition;
    vs_stage::cameraPositionFract = u_jcameraPositionFract;
    vs_stage::cameraPositionInt = u_jcameraPositionInt;
    vs_stage::cloudHeight = u_jcloudHeight;
    vs_stage::currentRenderedItemId = u_jcurrentRenderedItemId;
    vs_stage::darknessFactor = u_jdarknessFactor;
    vs_stage::darknessLightFactor = u_jdarknessLightFactor;
    vs_stage::entityColor = u_jentityColor;
    vs_stage::entityId = u_jentityId;
    vs_stage::eyeAltitude = u_jeyeAltitude;
    vs_stage::eyeBrightness = u_jeyeBrightness;
    vs_stage::eyeBrightnessM = u_jeyeBrightnessM;
    vs_stage::eyeBrightnessM2 = u_jeyeBrightnessM2;
    vs_stage::eyePosition = u_jeyePosition;
    vs_stage::far = u_jfar;
    vs_stage::fogColor = u_jfogColor;
    vs_stage::frameCounter = u_jframeCounter;
    vs_stage::frameTime = u_jframeTime;
    vs_stage::frameTimeCounter = u_jframeTimeCounter;
    vs_stage::frameTimeSmooth = u_jframeTimeSmooth;
    vs_stage::framemod2 = u_jframemod2;
    vs_stage::framemod4 = u_jframemod4;
    vs_stage::framemod600 = u_jframemod600;
    vs_stage::framemod8 = u_jframemod8;
    vs_stage::gbufferModelView = u_jgbufferModelView;
    vs_stage::gbufferModelViewInverse = u_jgbufferModelViewInverse;
    vs_stage::gbufferPreviousModelView = u_jgbufferPreviousModelView;
    vs_stage::gbufferPreviousProjection = u_jgbufferPreviousProjection;
    vs_stage::gbufferProjection = u_jgbufferProjection;
    vs_stage::gbufferProjectionInverse = u_jgbufferProjectionInverse;
    vs_stage::heavyFog = u_jheavyFog;
    vs_stage::heldBlockLightValue = u_jheldBlockLightValue;
    vs_stage::heldBlockLightValue2 = u_jheldBlockLightValue2;
    vs_stage::heldItemId = u_jheldItemId;
    vs_stage::heldItemId2 = u_jheldItemId2;
    vs_stage::inBasaltDeltas = u_jinBasaltDeltas;
    vs_stage::inCrimsonForest = u_jinCrimsonForest;
    vs_stage::inDry = u_jinDry;
    vs_stage::inNetherWastes = u_jinNetherWastes;
    vs_stage::inPaleGarden = u_jinPaleGarden;
    vs_stage::inRainy = u_jinRainy;
    vs_stage::inSnowy = u_jinSnowy;
    vs_stage::inSoulValley = u_jinSoulValley;
    vs_stage::inSulfurCaves = u_jinSulfurCaves;
    vs_stage::inWarpedForest = u_jinWarpedForest;
    vs_stage::isElytraFlying = u_jisElytraFlying;
    vs_stage::isEyeInCave = u_jisEyeInCave;
    vs_stage::isEyeInWater = u_jisEyeInWater;
    vs_stage::lightningBoltPosition = u_jlightningBoltPosition;
    vs_stage::maxBlindnessDarkness = u_jmaxBlindnessDarkness;
    vs_stage::moonPhase = u_jmoonPhase;
    vs_stage::near = u_jnear;
    vs_stage::nightVision = u_jnightVision;
    vs_stage::playerLookVector = u_jplayerLookVector;
    vs_stage::playerMood = u_jplayerMood;
    vs_stage::previousCameraPosition = u_jpreviousCameraPosition;
    vs_stage::previousCameraPositionFract = u_jpreviousCameraPositionFract;
    vs_stage::previousCameraPositionInt = u_jpreviousCameraPositionInt;
    vs_stage::rainFactor = u_jrainFactor;
    vs_stage::rainStrength = u_jrainStrength;
    vs_stage::relativeEyePosition = u_jrelativeEyePosition;
    vs_stage::screenBrightness = u_jscreenBrightness;
    vs_stage::shadowModelView = u_jshadowModelView;
    vs_stage::shadowModelViewInverse = u_jshadowModelViewInverse;
    vs_stage::shadowProjection = u_jshadowProjection;
    vs_stage::shadowProjectionInverse = u_jshadowProjectionInverse;
    vs_stage::skyColor = u_jskyColor;
    vs_stage::starter = u_jstarter;
    vs_stage::sunAngle = u_jsunAngle;
    vs_stage::velocity = u_jvelocity;
    vs_stage::viewHeight = u_jviewHeight;
    vs_stage::viewWidth = u_jviewWidth;
    vs_stage::wetness = u_jwetness;
    vs_stage::worldDay = u_jworldDay;
    vs_stage::worldTime = u_jworldTime;
    vs_stage::u_jModelViewMatrix = u_jModelViewMatrix;
    vs_stage::u_jNormalMatrix = u_jNormalMatrix;
    vs_stage::u_jProjectionMatrix = u_jProjectionMatrix;
    vs_stage::u_jTextureMatrix0 = u_jTextureMatrix0;
    vs_stage::u_jTextureMatrix1 = u_jTextureMatrix1;
    vs_stage::jLayer = floor(input.a_texcoord1.z * 255.0 + 0.5) + floor(input.a_texcoord1.w * 255.0 + 0.5) * 256.0;
    vs_stage::stageMain();
    VsOut output;
    output.clipPosition = vs_stage::gl_Position;
    output.v_mat = vs_stage::mat;
    output.v_texCoord = vs_stage::texCoord;
    output.v_lmCoord = vs_stage::lmCoord;
    output.v_signMidCoordPos = vs_stage::signMidCoordPos;
    output.v_absMidCoordPos = vs_stage::absMidCoordPos;
    output.v_playerPos = vs_stage::playerPos;
    output.v_normal = vs_stage::normal;
    output.v_viewVector = vs_stage::viewVector;
    output.v_glColor = vs_stage::glColor;
    output.v_jLayer = vs_stage::jLayer;
    return output;
}

[shader("fragment")]
FsOut fsMain(FsIn input)
{
    fs_stage::gl_FragCoord = input.fragCoord;
    fs_stage::mat = input.v_mat;
    fs_stage::texCoord = input.v_texCoord;
    fs_stage::lmCoord = input.v_lmCoord;
    fs_stage::signMidCoordPos = input.v_signMidCoordPos;
    fs_stage::absMidCoordPos = input.v_absMidCoordPos;
    fs_stage::playerPos = input.v_playerPos;
    fs_stage::normal = input.v_normal;
    fs_stage::viewVector = input.v_viewVector;
    fs_stage::glColor = input.v_glColor;
    fs_stage::jLayer = input.v_jLayer;
    fs_stage::FogScale = u_jFogScale;
    fs_stage::FogStart = u_jFogStart;
    fs_stage::aspectRatio = u_jaspectRatio;
    fs_stage::atlasSize = u_jatlasSize;
    fs_stage::blindness = u_jblindness;
    fs_stage::blockEntityId = u_jblockEntityId;
    fs_stage::cameraPosition = u_jcameraPosition;
    fs_stage::cameraPositionFract = u_jcameraPositionFract;
    fs_stage::cameraPositionInt = u_jcameraPositionInt;
    fs_stage::cloudHeight = u_jcloudHeight;
    fs_stage::currentRenderedItemId = u_jcurrentRenderedItemId;
    fs_stage::darknessFactor = u_jdarknessFactor;
    fs_stage::darknessLightFactor = u_jdarknessLightFactor;
    fs_stage::entityColor = u_jentityColor;
    fs_stage::entityId = u_jentityId;
    fs_stage::eyeAltitude = u_jeyeAltitude;
    fs_stage::eyeBrightness = u_jeyeBrightness;
    fs_stage::eyeBrightnessM = u_jeyeBrightnessM;
    fs_stage::eyeBrightnessM2 = u_jeyeBrightnessM2;
    fs_stage::eyePosition = u_jeyePosition;
    fs_stage::far = u_jfar;
    fs_stage::fogColor = u_jfogColor;
    fs_stage::frameCounter = u_jframeCounter;
    fs_stage::frameTime = u_jframeTime;
    fs_stage::frameTimeCounter = u_jframeTimeCounter;
    fs_stage::frameTimeSmooth = u_jframeTimeSmooth;
    fs_stage::framemod2 = u_jframemod2;
    fs_stage::framemod4 = u_jframemod4;
    fs_stage::framemod600 = u_jframemod600;
    fs_stage::framemod8 = u_jframemod8;
    fs_stage::gbufferModelView = u_jgbufferModelView;
    fs_stage::gbufferModelViewInverse = u_jgbufferModelViewInverse;
    fs_stage::gbufferPreviousModelView = u_jgbufferPreviousModelView;
    fs_stage::gbufferPreviousProjection = u_jgbufferPreviousProjection;
    fs_stage::gbufferProjection = u_jgbufferProjection;
    fs_stage::gbufferProjectionInverse = u_jgbufferProjectionInverse;
    fs_stage::heavyFog = u_jheavyFog;
    fs_stage::heldBlockLightValue = u_jheldBlockLightValue;
    fs_stage::heldBlockLightValue2 = u_jheldBlockLightValue2;
    fs_stage::heldItemId = u_jheldItemId;
    fs_stage::heldItemId2 = u_jheldItemId2;
    fs_stage::inBasaltDeltas = u_jinBasaltDeltas;
    fs_stage::inCrimsonForest = u_jinCrimsonForest;
    fs_stage::inDry = u_jinDry;
    fs_stage::inNetherWastes = u_jinNetherWastes;
    fs_stage::inPaleGarden = u_jinPaleGarden;
    fs_stage::inRainy = u_jinRainy;
    fs_stage::inSnowy = u_jinSnowy;
    fs_stage::inSoulValley = u_jinSoulValley;
    fs_stage::inSulfurCaves = u_jinSulfurCaves;
    fs_stage::inWarpedForest = u_jinWarpedForest;
    fs_stage::isElytraFlying = u_jisElytraFlying;
    fs_stage::isEyeInCave = u_jisEyeInCave;
    fs_stage::isEyeInWater = u_jisEyeInWater;
    fs_stage::lightningBoltPosition = u_jlightningBoltPosition;
    fs_stage::maxBlindnessDarkness = u_jmaxBlindnessDarkness;
    fs_stage::moonPhase = u_jmoonPhase;
    fs_stage::near = u_jnear;
    fs_stage::nightVision = u_jnightVision;
    fs_stage::playerLookVector = u_jplayerLookVector;
    fs_stage::playerMood = u_jplayerMood;
    fs_stage::previousCameraPosition = u_jpreviousCameraPosition;
    fs_stage::previousCameraPositionFract = u_jpreviousCameraPositionFract;
    fs_stage::previousCameraPositionInt = u_jpreviousCameraPositionInt;
    fs_stage::rainFactor = u_jrainFactor;
    fs_stage::rainStrength = u_jrainStrength;
    fs_stage::relativeEyePosition = u_jrelativeEyePosition;
    fs_stage::screenBrightness = u_jscreenBrightness;
    fs_stage::shadowModelView = u_jshadowModelView;
    fs_stage::shadowModelViewInverse = u_jshadowModelViewInverse;
    fs_stage::shadowProjection = u_jshadowProjection;
    fs_stage::shadowProjectionInverse = u_jshadowProjectionInverse;
    fs_stage::skyColor = u_jskyColor;
    fs_stage::starter = u_jstarter;
    fs_stage::sunAngle = u_jsunAngle;
    fs_stage::velocity = u_jvelocity;
    fs_stage::viewHeight = u_jviewHeight;
    fs_stage::viewWidth = u_jviewWidth;
    fs_stage::wetness = u_jwetness;
    fs_stage::worldDay = u_jworldDay;
    fs_stage::worldTime = u_jworldTime;
    fs_stage::u_jModelViewMatrix = u_jModelViewMatrix;
    fs_stage::u_jNormalMatrix = u_jNormalMatrix;
    fs_stage::u_jProjectionMatrix = u_jProjectionMatrix;
    fs_stage::u_jTextureMatrix0 = u_jTextureMatrix0;
    fs_stage::u_jTextureMatrix1 = u_jTextureMatrix1;
    fs_stage::stageMain();
    FsOut output;
    output.t0 = fs_stage::gl_FragData0;
    output.t1 = fs_stage::gl_FragData1;
    return output;
}
