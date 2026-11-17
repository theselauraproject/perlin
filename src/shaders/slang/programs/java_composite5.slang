#include "compat.slang"

#define PERLIN_JAVA_UNIFORMS \
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
};
struct VsOut
{
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] noperspective float2 v_texCoord : TEXCOORD0;
    [[vk::location(1)]] nointerpolation float3 v_upVec : TEXCOORD1;
    [[vk::location(2)]] nointerpolation float3 v_sunVec : TEXCOORD2;
};
struct FsIn
{
    float4 fragCoord : SV_Position;
    [[vk::location(0)]] noperspective float2 v_texCoord : TEXCOORD0;
    [[vk::location(1)]] nointerpolation float3 v_upVec : TEXCOORD1;
    [[vk::location(2)]] nointerpolation float3 v_sunVec : TEXCOORD2;
};
struct FsOut
{
    float4 t0 : SV_Target0;
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
static float2 texCoord;
static float3 upVec;
static float3 sunVec;
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
    gl_Position = (mul(u_jProjectionMatrix, u_jModelViewMatrix) * vec4(a_position, 1.0));

    texCoord = (u_jTextureMatrix0 * vec4(a_texcoord0, 0.0, 1.0)).xy;

        upVec = normalize(transpose(gbufferModelView)[1].xyz);
        sunVec = GetSunVector();
}
}

namespace fs_stage
{
static float4 gl_FragCoord;
static float4 gl_FragData0;
static float2 texCoord;
static float3 upVec;
static float3 sunVec;
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
static float4x4 u_jProjectionMatrix;
static float4x4 u_jTextureMatrix0;
static float4x4 u_jTextureMatrix1;

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

static float pw;
static float ph;

static vec2 view;

static float SdotU;
static float sunFactor;

void LinearToRGB(inout vec3 color) {
    const vec3 k = vec3(0.055);
    color = mix((vec3(1.0) + k) * pow(color, vec3(1.0 / 2.4)) - k, 12.92 * color, ((color)<(vec3(0.0031308))));
}

void DoCompTonemap(inout vec3 color) {

    color = 1.00 * color;

    float colorMax = max(color.r, max(color.g, color.b));
    float initialLuminance = GetLuminance(color);

    vec3 a      = vec3(1.05);
    vec3 d      = vec3(1.0);
    vec3 hdrMax = vec3(8.0);
    vec3 midIn  = vec3(0.25);
    vec3 midOut = vec3(0.25);

    vec3 a_d = a * d;
    vec3 hdrMaxA = pow(hdrMax, a);
    vec3 hdrMaxAD = pow(hdrMax, a_d);
    vec3 midInA = pow(midIn, a);
    vec3 midInAD = pow(midIn, a_d);
    vec3 HM1 = hdrMaxA * midOut;
    vec3 HM2 = hdrMaxAD - midInAD;

    vec3 b = (-midInA + HM1) / (HM2 * midOut);
    vec3 c = (hdrMaxAD * midInA - HM1 * midInAD) / (HM2 * midOut);

    vec3 colorOut = pow(color, a) / (pow(color, a_d) * b + c);

    LinearToRGB(colorOut);

    const float darkLiftStart = 0.1;
    const float darkLiftMix = 0.75;
    float darkLift = smoothstep(darkLiftStart, 0.0, initialLuminance);
    vec3 smoothColor = pow(color, vec3(1.0 / 2.2));
    colorOut = mix(colorOut, smoothColor, darkLift * darkLiftMix * max0(0.55 - abs(1.05 - 1.05)) / 0.55);

    const float wpInputCurveStart = 0.0;
    const float wpInputCurveMax = 16.0;
    float modifiedLuminance = pow(initialLuminance / wpInputCurveMax, 2.0 - 1.00) * wpInputCurveMax;
    float whitePath = smoothstep(wpInputCurveStart, wpInputCurveMax, modifiedLuminance);
    colorOut = mix(colorOut, vec3(1.0), whitePath);

    const float dpInputCurveStart = 0.1;
    const float dpInputCurveMax = 0.0;
    float desaturatePath = smoothstep(dpInputCurveStart, dpInputCurveMax, initialLuminance);
    colorOut = mix(colorOut, vec3(GetLuminance(colorOut)), desaturatePath * 0.25);

    color = clamp01(colorOut);
}

void DoBSLColorSaturation(inout vec3 color) {
    float saturationFactor = 1.00 + 0.07;

    float grayVibrance = (color.r + color.g + color.b) / 3.0;
    float graySaturation = grayVibrance;
    if (saturationFactor < 1.00) graySaturation = dot(color, vec3(0.299, 0.587, 0.114));

    float mn = min(color.r, min(color.g, color.b));
    float mx = max(color.r, max(color.g, color.b));
    float sat = (1.0 - (mx - mn)) * (1.0 - mx) * grayVibrance * 5.0;
    vec3 lightness = vec3((mn + mx) * 0.5);

    color = mix(color, mix(color, lightness, 1.0 - 1.00), sat);
    color = mix(color, lightness, (1.0 - lightness) * (2.0 - 1.00) / 2.0 * abs(1.00 - 1.0));
    color = color * saturationFactor - graySaturation * (saturationFactor - 1.0);
}

static vec2 rescale;
    vec3 GetBloomTile(float lod, vec2 coord, vec2 offset) {
        float scale = exp2(lod);
        vec2 bloomCoord = coord / scale + offset;
        bloomCoord = clamp(bloomCoord, offset, 1.0 / scale + offset);

        vec3 bloom = texture2D(s_colortex3, bloomCoord / rescale).rgb;
        bloom *= bloom;
        bloom *= bloom;
        return bloom * 128.0;
    }

    void DoBloom(inout vec3 color, vec2 coord, float dither, float lViewPos) {
        vec3 blur1 = GetBloomTile(2.0, coord, vec2(0.0      , 0.0   ));
        vec3 blur2 = GetBloomTile(3.0, coord, vec2(0.0      , 0.26  ));
        vec3 blur3 = GetBloomTile(4.0, coord, vec2(0.135    , 0.26  ));
        vec3 blur4 = GetBloomTile(5.0, coord, vec2(0.2075   , 0.26  ));
        vec3 blur5 = GetBloomTile(6.0, coord, vec2(0.135    , 0.3325));
        vec3 blur6 = GetBloomTile(7.0, coord, vec2(0.160625 , 0.3325));
        vec3 blur7 = GetBloomTile(8.0, coord, vec2(0.1784375, 0.3325));

        vec3 blur = (blur1 + blur2 + blur3 + blur4 + blur5 + blur6 + blur7) * 0.14;

        float bloomStrength = 0.12 + 0.2 * darknessFactor;

        color = mix(color, blur, bloomStrength);

    }

    float GetCaveFactor() {
        float caveFactor = clamp(1.0 - cameraPosition.y / oceanAltitude, 0.0, 1.0 - eyeBrightnessM);
        return caveFactor;
    }

static const float rainBloomAdd   = 8.0;
static const float nightBloomAdd  = 3.0;
static const float caveBloomAdd   = 14.0;
static const float waterBloomAdd  = 14.0;

static const float netherBloomAdd = 14.0;

float GetBloomFog(float lViewPos) {
        float bloomFog = pow2(pow2(1.0 - exp(-lViewPos * (0.02 + 0.04 * float(isEyeInWater == 1)))));

        float bloomFogMult;
        if (isEyeInWater != 1) {
            bloomFogMult = (rainFactor2 * rainBloomAdd + nightBloomAdd * (1.0 - sunFactor)) * eyeBrightnessM;
                bloomFogMult += GetCaveFactor() * caveBloomAdd;
        } else {
            bloomFogMult = waterBloomAdd;
        }

    bloomFogMult *= 0.12 * 8.33333;

    return 1.0 + bloomFog * bloomFogMult;
}

    float Bayer2  (vec2 c) { c = 0.5 * floor(c); return fract(1.5 * fract(c.y) + c.x); }
    float Bayer4  (vec2 c) { return 0.25 * Bayer2  (0.5 * c) + Bayer2(c); }
    float Bayer8  (vec2 c) { return 0.25 * Bayer4  (0.5 * c) + Bayer2(c); }
    float Bayer16 (vec2 c) { return 0.25 * Bayer8  (0.5 * c) + Bayer2(c); }
    float Bayer32 (vec2 c) { return 0.25 * Bayer16 (0.5 * c) + Bayer2(c); }
    float Bayer64 (vec2 c) { return 0.25 * Bayer32 (0.5 * c) + Bayer2(c); }
    float Bayer128(vec2 c) { return 0.25 * Bayer64 (0.5 * c) + Bayer2(c); }
    float Bayer256(vec2 c) { return 0.25 * Bayer128(0.5 * c) + Bayer2(c); }

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
    pw = 1.0 / viewWidth;
    ph = 1.0 / viewHeight;
    view = vec2(viewWidth, viewHeight);
    SdotU = dot(sunVec, upVec);
    sunFactor = SdotU < 0.0 ? clamp(SdotU + 0.375, 0.0, 0.75) / 0.75 : clamp(SdotU + 0.03125, 0.0, 0.0625) / 0.0625;
    rescale = max(vec2(viewWidth, viewHeight) / vec2(1920.0, 1080.0), vec2(1.0));
    vec3 color = texture2D(s_colortex0, texCoord).rgb;

        float z0 = texture2D(s_sceneDepth, texCoord).r;
        vec4 screenPos = vec4(texCoord, z0, 1.0);
        vec4 viewPos = gbufferProjectionInverse * (screenPos * 2.0 - 1.0);
        viewPos /= viewPos.w;
        float lViewPos = length(viewPos.xyz);

    float dither = texture2DLod(s_noise, texCoord * view / 128.0, 0.0).b;
        dither = fract(dither + goldenRatio * mod(float(frameCounter), 3600.0));

        color /= GetBloomFog(lViewPos);

        DoBloom(color, texCoord, dither, lViewPos);

    DoCompTonemap(color);

    DoBSLColorSaturation(color);

    gl_FragData0 = vec4(color, 1.0);
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
    vs_stage::a_texcoord0 = vec2(input.a_position.x * 0.5 + 0.5, 0.5 - input.a_position.y * 0.5);
    vs_stage::a_texcoord1 = vec4(0.5, 0.5, 0.0, 1.0);
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
    vs_stage::u_jProjectionMatrix = u_jProjectionMatrix;
    vs_stage::u_jTextureMatrix0 = u_jTextureMatrix0;
    vs_stage::u_jTextureMatrix1 = u_jTextureMatrix1;
    vs_stage::stageMain();
    VsOut output;
    output.clipPosition = float4(input.a_position.xy, 0.0, 1.0);
    output.v_texCoord = vs_stage::texCoord;
    output.v_upVec = vs_stage::upVec;
    output.v_sunVec = vs_stage::sunVec;
    return output;
}

[shader("fragment")]
FsOut fsMain(FsIn input)
{
    fs_stage::gl_FragCoord = input.fragCoord;
    fs_stage::texCoord = input.v_texCoord;
    fs_stage::upVec = input.v_upVec;
    fs_stage::sunVec = input.v_sunVec;
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
    fs_stage::u_jProjectionMatrix = u_jProjectionMatrix;
    fs_stage::u_jTextureMatrix0 = u_jTextureMatrix0;
    fs_stage::u_jTextureMatrix1 = u_jTextureMatrix1;
    fs_stage::stageMain();
    FsOut output;
    output.t0 = fs_stage::gl_FragData0;
    return output;
}
