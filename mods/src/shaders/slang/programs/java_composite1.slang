// Converted from Java shaderpack program composite1 (world0).
// v1 converter output: uniform feed + renderer wiring still needed, see report.
#include "compat.slang"

// Iris uniforms: engine must feed these each frame/draw.
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

// java sampler colortex0 -> RHI slot s_colortex0 (see vv_resources.slang)
// java sampler colortex1 -> RHI slot s_colortex1 (see vv_resources.slang)
// java sampler colortex12 -> RHI slot s_colortex12 (see vv_resources.slang)
// java sampler colortex13 -> RHI slot s_colortex13 (see vv_resources.slang)
// java sampler colortex2 -> RHI slot s_colortex2 (see vv_resources.slang)
// java sampler colortex3 -> RHI slot s_colortex3 (see vv_resources.slang)
// java sampler colortex4 -> RHI slot s_colortex4 (see vv_resources.slang)
// java sampler colortex5 -> RHI slot s_colortex5 (see vv_resources.slang)
// java sampler colortex6 -> RHI slot s_colortex6 (see vv_resources.slang)
// java sampler colortex7 -> RHI slot s_colortex7 (see vv_resources.slang)
// java sampler colortex8 -> RHI slot s_colortex8 (see vv_resources.slang)
// java sampler depthtex0 -> RHI slot s_sceneDepth (declared here)
SAMPLER2D(s_sceneDepth, 10, 11);
// java sampler depthtex1 -> RHI slot s_depth (declared here)
SAMPLER2D(s_depth, 34, 35);
// java sampler gaux2 -> RHI slot s_gaux2 (see vv_resources.slang)
// java sampler gaux4 -> RHI slot s_gaux4 (see vv_resources.slang)
// java sampler lightmap -> RHI slot s_lightmap (see vv_resources.slang)
// java sampler noisetex -> RHI slot s_noise (see vv_resources.slang)
// java sampler normals -> RHI slot s_normals (see vv_resources.slang)
// java sampler shadowcolor0 -> RHI slot s_shadowTint (see vv_resources.slang)
// java sampler shadowcolor1 -> RHI slot s_shadowTintB (see vv_resources.slang)
// java sampler shadowtex0 -> RHI slot s_shadow (see vv_resources.slang)
// java sampler shadowtex1 -> RHI slot s_shadowB (see vv_resources.slang)
// java sampler specular -> RHI slot s_specular (see vv_resources.slang)
// java sampler tex -> RHI slot s_atlas (see vv_resources.slang)

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
    [[vk::location(3)]] nointerpolation float v_vlFactor : TEXCOORD3;
};
struct FsIn
{
    float4 fragCoord : SV_Position;
    [[vk::location(0)]] noperspective float2 v_texCoord : TEXCOORD0;
    [[vk::location(1)]] nointerpolation float3 v_upVec : TEXCOORD1;
    [[vk::location(2)]] nointerpolation float3 v_sunVec : TEXCOORD2;
    [[vk::location(3)]] nointerpolation float v_vlFactor : TEXCOORD3;
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
static float2 texCoord;
static float3 upVec;
static float3 sunVec;
static float vlFactor;
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


// @included "/program/composite1.glsl"
/////////////////////////////////////
// Complementary Shaders by EminGT //
/////////////////////////////////////

//Common//
// @included "/lib/common.glsl"
/*---------------------------------------------------------------------
         ___ __  __ ____   ___  ____ _____  _    _   _ _____
        |_ _|  \/  |  _ \ / _ \|  _ \_   _|/ \  | \ | |_   _|
         | || |\/| | |_) | | | | |_) || | / _ \ |  \| | | |
         | || |  | |  __/| |_| |  _ < | |/ ___ \| |\  | | |
        |___|_|  |_|_|    \___/|_| \_\|_/_/   \_\_| \_| |_|
         .
  -> -> -> EDITING THIS FILE HAS A HIGH CHANCE TO BREAK THE SHADER PACK
  -> -> -> DO NOT CHANGE ANYTHING UNLESS YOU KNOW WHAT YOU ARE DOING
  -> -> -> DO NOT EXPECT SUPPORT AFTER MODIFYING SHADER FILES
---------------------------------------------------------------------*/

//User Settings//


static const float shadowDistance = 192.0; //[64.0 80.0 96.0 112.0 128.0 160.0 192.0 224.0 256.0 320.0 384.0 512.0 768.0 1024.0]





    //#define CLOUD_SHADOWS


    //#define DOUBLE_REIM_CLOUDS



    //#define LIGHTSHAFT_SMOKE



    //#define WB_FOV_SCALED
    //#define WB_CHROMATIC
    //#define WB_ANAMORPHIC



    //#define EMISSIVE_REDSTONE_BLOCK
    //#define EMISSIVE_LAPIS_BLOCK
    //#define GLOWING_ARMOR_TRIM

    //#define IPBR_COMPAT_MODE_DEFINE

    //#define POM_ALLOW_CUTOUT


    //#define WAVING_LILY_PAD



    //#define SELECT_OUTLINE_AUTO_HIDE

    //#define WORLD_OUTLINE
    //#define WORLD_OUTLINE_SCALED
    //#define DARK_OUTLINE

    //#define REDUCE_CLOSE_PARTICLES
    //#define LESS_LAVA_FOG
    //#define SNOWY_WORLD
    //#define COLOR_CODED_PROGRAMS
    //#define GLOWING_COLORED_PARTICLES

    //#define MOON_PHASE_INF_LIGHT
    //#define MOON_PHASE_INF_ATMOSPHERE

    //#define PIXELATED_SHADOWS
    //#define PIXELATED_BLOCKLIGHT
    //#define PIXELATED_AO

    //#define COLORGRADING

    //#define LIGHT_COLOR_MULTS
    //#define ATM_COLOR_MULTS




//Internal Settings//




    //#define SAFER_GENERATED_NORMALS


//Extensions//

//Information//

//RP Mode, Visual Style and Performance Setting Handling//
        //#define GENERATED_NORMALS
        //#define COATED_TEXTURES
        //#define GREEN_SCREEN_LIME

        //#define WATER_CAUSTIC_STYLE_DEFAULT 1
    // Thanks to SpacEagle17 and isuewo for the sun angle handling
static const float sunPathRotation = 0.0;

static const int shadowMapResolution = 2048;





//Define Handling//


















//Activate Settings//

//Very Common Stuff//
// @included "/lib/uniforms.glsl"
/*----------------------------------------------------------------------------------------------
        _____                                                                    _____
        ( ___ )                                                                  ( ___ )
        |   |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|   |
        |   | ██╗   ██╗███╗   ██╗██╗███████╗ ██████╗ ██████╗ ███╗   ███╗███████╗ |   |
        |   | ██║   ██║████╗  ██║██║██╔════╝██╔═══██╗██╔══██╗████╗ ████║██╔════╝ |   |
        |   | ██║   ██║██╔██╗ ██║██║█████╗  ██║   ██║██████╔╝██╔████╔██║███████╗ |   |
        |   | ██║   ██║██║╚██╗██║██║██╔══╝  ██║   ██║██╔══██╗██║╚██╔╝██║╚════██║ |   |
        |   | ╚██████╔╝██║ ╚████║██║██║     ╚██████╔╝██║  ██║██║ ╚═╝ ██║███████║ |   |
        |   |  ╚═════╝ ╚═╝  ╚═══╝╚═╝╚═╝      ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝ |   |
        |___|~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|___|
        (_____)                              (thanks to isuewo and SpacEagle17)  (_____)

---------------------------------------------------------------------------------------------*/




















/*-----------------------------------------------------------------------------
  ___ _   _ ___ _____ ___  __  __   _   _ _  _ ___ ___ ___  ___ __  __ ___
 / __| | | / __|_   _/ _ \|  \/  | | | | | \| |_ _| __/ _ \| _ \  \/  / __|
| (__| |_| \__ \ | || (_) | |\/| | | |_| | .` || || _| (_) |   / |\/| \__ 
 \___|\___/|___/ |_| \___/|_|  |_|  \___/|_|\_|___|_| \___/|_|_\_|  |_|___/

-----------------------------------------------------------------------------*/


static float tAmin;
static float tAlin;
static float hA;
static float tAfrc;
static float tAfrs;
static float tAmix;
static float timeAngle;

// @included "/lib/util/commonFunctions.glsl"
// Do these via macro as Apple otherwise complains

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

bool CheckForColor(vec3 albedo, vec3 check) { // Thanks to Builderb0y - edited a bit later
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

float pow1_5(float x) { // Faster pow(x, 1.5) approximation (that isn't accurate at all) if x is between 0 and 1
    return x - x * pow2(1.0 - x); // Thanks to SixthSurge
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

float sqrt1(float x) { // Faster sqrt() approximation (that isn't accurate at all) if x is between 0 and 1
    return x * (2.0 - x); // Thanks to Builderb0y
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

static float nightVisionWithAddedSupport; // Add support for fullbright mods

static int modifiedWorldDay;
static float syncedTime;

static const float pi = 3.14159265359;
static const float goldenRatio = 1.61803398875;

static const float oceanAltitude = 61.9;

static ivec3 cameraPositionBestInt;
static vec3 cameraPositionBestFract;
static vec3 previousCameraPositionBestFract;

// @included "/lib/colors/blocklightColors.glsl"
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
	/* Please note that these colors do not determine the intensity of the
	final light. Instead; higher values of color change how long the color
	will travel, and also how dominant it will be next to other colors.*/
	/* Additional feature: An alpha value bigger than 0 will make that
	block cast extra light regardless of the vanilla s_lightmap. Use this
	with caution though because our floodfill isn't as accurate as vanilla.*/

	if (mat < 50) {
		if (mat < 26) {
			if (mat < 14) {
				if (mat < 8) {
					if (mat < 5) {
						if (mat == 2) return vec4(fireSpecialLightColor, 0.0); // Torch
							if (mat == 3) return vec4(vec3(1.0, 1.0, 1.0) * 4.0, 0.0); // End Rod - This is the base for all lights. Total value 12
						if (mat == 4) return vec4(vec3(0.7, 1.5, 2.0) * 3.0, 0.0); // Beacon
					} else {
						if (mat == 5) return vec4(fireSpecialLightColor, 0.0); // Fire
						if (mat == 6) return vec4(vec3(0.7, 1.5, 1.5) * 1.7, 0.0); // Sea Pickle:Waterlogged
						if (mat == 7) return vec4(vec3(1.1, 0.85, 0.35) * 5.0, 0.0); // Ochre Froglight
					}
				} else {
					if (mat < 11) {
						if (mat == 8) return vec4(vec3(0.6, 1.3, 0.6) * 4.5, 0.0); // Verdant Froglight
						if (mat == 9) return vec4(vec3(1.1, 0.5, 0.9) * 4.5, 0.0); // Pearlescent Froglight
						if (mat == 10) return vec4(vec3(1.7, 0.9, 0.4) * 4.0, 0.0); // Glowstone
					} else {
						if (mat == 11) return vec4(fireSpecialLightColor, 0.0); // Jack o'Lantern
						if (mat == 12) return vec4(fireSpecialLightColor, 0.0); // Lantern
						if (mat == 13) return vec4(lavaSpecialLightColor, 0.8); // Lava
					}
				}
			} else {
				if (mat < 20) {
					if (mat < 17) {
						if (mat == 14) return vec4(lavaSpecialLightColor, 0.0); // Lava Cauldron
						if (mat == 15) return vec4(fireSpecialLightColor, 0.0); // Campfire:Lit
						if (mat == 16) return vec4(vec3(1.7, 0.9, 0.4) * 4.0, 0.0); // Redstone Lamp:Lit
					} else {
						if (mat == 17) return vec4(vec3(1.7, 0.9, 0.4) * 2.0, 0.0); // Respawn Anchor:Lit
						if (mat == 18) return vec4(vec3(1.0, 1.25, 1.5) * 3.4, 0.0); // Sea Lantern
						if (mat == 19) return vec4(vec3(3.0, 0.9, 0.2) * 3.0, 0.0); // Shroomlight
					}
				} else {
					if (mat < 23) {
						if (mat == 20) return vec4(vec3(2.3, 0.9, 0.2) * 3.4, 0.0); // Cave Vines:With Glow Berries
						if (mat == 21) return vec4(fireSpecialLightColor * 0.7, 0.0); // Furnace:Lit
						if (mat == 22) return vec4(fireSpecialLightColor * 0.7, 0.0); // Smoker:Lit
					} else {
						if (mat == 23) return vec4(fireSpecialLightColor * 0.7, 0.0); // Blast Furnace:Lit
						if (mat == 24) return vec4(fireSpecialLightColor * 0.25 * candleColorMult, candleExtraLight); // Standard Candles:Lit
						if (mat == 25) return vec4(netherPortalSpecialLightColor * 2.0, 0.4); // Nether Portal
					}
				}
			}
		} else {
			if (mat < 38) {
				if (mat < 32) {
					if (mat < 29) {
						if (mat == 26) return vec4(netherPortalSpecialLightColor, 0.0); // Crying Obsidian
						if (mat == 27) return soulFireSpecialColor; // Soul Fire
						if (mat == 28) return soulFireSpecialColor; // Soul Torch
					} else {
						if (mat == 29) return soulFireSpecialColor; // Soul Lantern
						if (mat == 30) return soulFireSpecialColor; // Soul Campfire:Lit
						if (mat == 31) return vec4(redstoneSpecialLightColor * 0.5, 0.1); // Redstone Ores:Lit
					}
				} else {
					if (mat < 35) {
						if (mat == 32) return vec4(redstoneSpecialLightColor * 0.3, 0.1) * 1.00; // Redstone Ores:Unlit
						if (mat == 33) return vec4(vec3(1.4, 1.1, 0.5), 0.05); // Enchanting Table
							if (mat == 34) return vec4(vec3(0.8, 1.1, 1.1), 0.05); // Glow Lichen with IntegratedPBR
					} else {
						if (mat == 35) return vec4(redstoneSpecialLightColor * 0.25, 0.0); // Redstone Torch
						if (mat == 36) return vec4(vec3(0.325, 0.15, 0.425) * 2.0, 0.05); // Amethyst Cluster, Amethyst Buds, Calibrated Sculk Sensor
						if (mat == 37) return vec4(lavaSpecialLightColor * 0.1, 0.1); // Magma Block
					}
				}
			} else {
				if (mat < 44) {
					if (mat < 41) {
						if (mat == 38) return vec4(vec3(2.0, 0.5, 1.5) * 0.3, 0.1); // Dragon Egg
						if (mat == 39) return vec4(vec3(2.0, 1.0, 1.5) * 0.25, 0.1); // Chorus Flower
						if (mat == 40) return vec4(vec3(2.5, 1.2, 0.4) * 0.1, 0.1); // Brewing Stand
					} else {
						if (mat == 41) return vec4(redstoneSpecialLightColor * 0.4, 0.15); // Redstone Block
						if (mat == 42) return vec4(vec3(0.75, 0.75, 3.0) * 0.277, 0.15); // Lapis Block
						if (mat == 43) return vec4(vec3(1.7, 0.9, 0.4) * 0.45, 0.05) * 1.00; // Iron Ores
					}
				} else {
					if (mat < 47) {
						if (mat == 44) return vec4(vec3(1.7, 1.1, 0.2) * 0.45, 0.1) * 1.00; // Gold Ores
						if (mat == 45) return vec4(vec3(1.7, 0.8, 0.4) * 0.45, 0.05) * 1.00; // Copper Ores
						if (mat == 46) return vec4(vec3(0.75, 0.75, 3.0) * 0.2, 0.1) * 1.00; // Lapis Ores
					} else {
						if (mat == 47) return vec4(vec3(0.5, 3.5, 0.5) * 0.3, 0.1) * 1.00; // Emerald Ores
						if (mat == 48) return vec4(vec3(0.5, 2.0, 2.0) * 0.4, 0.15) * 1.00; // Diamond Ores
						if (mat == 49) return vec4(vec3(1.5, 1.5, 1.5) * 0.3, 0.05) * 1.00; // Nether Quartz Ore
					}
				}
			}
		}
	} else {
		if (mat < 74) {
			if (mat < 62) {
				if (mat < 56) {
					if (mat < 53) {
						if (mat == 50) return vec4(vec3(1.7, 1.1, 0.2) * 0.45, 0.05) * 1.00; // Nether Gold Ore
						if (mat == 51) return vec4(vec3(1.7, 1.1, 0.2) * 0.45, 0.05); // Gilded Blackstone
						if (mat == 52) return vec4(vec3(1.8, 0.8, 0.4) * 0.6, 0.15); // Ancient Debris
					} else {
						if (mat == 53) return vec4(vec3(1.4, 0.2, 1.4) * 0.3, 0.05); // Spawner
						if (mat == 54) return vec4(vec3(3.1, 1.1, 0.3) * 1.0, 0.1); // Trial Spawner:NotOminous:Active, Vault:NotOminous:Active
						if (mat == 55) return vec4(vec3(1.7, 0.9, 0.4) * 4.0, 0.0); // Copper Bulb:BrighterOnes:Lit
					}
				} else {
					if (mat < 59) {
						if (mat == 56) return vec4(vec3(1.7, 0.9, 0.4) * 2.0, 0.0); // Copper Bulb:DimmerOnes:Lit
						if (mat == 57) return vec4(vec3(0.1, 0.3, 0.4) * 0.5, 0.0005); // Sculk++
						if (mat == 58) return vec4(vec3(0.0, 1.4, 1.4) * 4.0, 0.15); // End Portal Frame:Active
					} else {
						if (mat == 59) return vec4(0.0); // Bedrock
						if (mat == 60) return vec4(vec3(3.1, 1.1, 0.3) * 0.125, 0.0125); // Command Block
						if (mat == 61) return vec4(vec3(3.0, 0.9, 0.2) * 0.125, 0.0125); // Warped Fungus, Crimson Fungus
					}
				}
			} else {
				if (mat < 68) {
					if (mat < 65) {
						if (mat == 62) return vec4(vec3(3.5, 0.6, 0.4) * 0.3, 0.05); // Crimson Stem, Crimson Hyphae
						if (mat == 63) return vec4(vec3(0.3, 1.9, 1.5) * 0.3, 0.05); // Warped Stem, Warped Hyphae
						if (mat == 64) return vec4(vec3(1.0, 1.0, 1.0) * 0.45, 0.1); // Structure Block, Jigsaw Block, Test Block, Test Instance Block
					} else {
						if (mat == 65) return vec4(vec3(3.0, 0.9, 0.2) * 0.125, 0.0125); // Weeping Vines Plant
						if (mat == 66) return vec4(redstoneSpecialLightColor * 0.05, 0.002); // Redstone Wire:Lit, Comparator:Unlit:Subtract
						if (mat == 67) return vec4(redstoneSpecialLightColor * 0.125, 0.0125); // Repeater:Lit, Comparator:Lit
					}
				} else {
					if (mat < 71) {
						if (mat == 68) return vec4(vec3(0.75), 0.0); // Vault:Inactive
						if (mat == 69) return vec4(vec3(1.3, 1.6, 1.6) * 1.0, 0.1); // Trial Spawner:Ominous:Active, Vault:Ominous:Active
						if (mat == 70) return vec4(vec3(1.0, 0.1, 0.1) * candleColorMult, candleExtraLight); // Red Candles:Lit
					} else {
						if (mat == 71) return vec4(vec3(1.0, 0.4, 0.1) * candleColorMult, candleExtraLight); // Orange Candles:Lit
						if (mat == 72) return vec4(vec3(1.0, 1.0, 0.1) * candleColorMult, candleExtraLight); // Yellow Candles:Lit
						if (mat == 73) return vec4(vec3(0.1, 1.0, 0.1) * candleColorMult, candleExtraLight); // Lime Candles:Lit
					}
				}
			}
		} else {
			if (mat < 86) {
				if (mat < 80) {
					if (mat < 77) {
						if (mat == 74) return vec4(vec3(0.3, 1.0, 0.3) * candleColorMult, candleExtraLight); // Green Candles:Lit
						if (mat == 75) return vec4(vec3(0.3, 0.8, 1.0) * candleColorMult, candleExtraLight); // Cyan Candles:Lit
						if (mat == 76) return vec4(vec3(0.5, 0.65, 1.0) * candleColorMult, candleExtraLight); // Light Blue Candles:Lit
					} else {
						if (mat == 77) return vec4(vec3(0.1, 0.15, 1.0) * candleColorMult, candleExtraLight); // Blue Candles:Lit
						if (mat == 78) return vec4(vec3(0.7, 0.3, 1.0) * candleColorMult, candleExtraLight); // Purple Candles:Lit
						if (mat == 79) return vec4(vec3(1.0, 0.1, 1.0) * candleColorMult, candleExtraLight); // Magenta Candles:Lit
					}
				} else {
					if (mat < 83) {
						if (mat == 80) return vec4(vec3(1.0, 0.4, 1.0) * candleColorMult, candleExtraLight); // Pink Candles:Lit
						if (mat == 81) return vec4(vec3(2.8, 1.1, 0.2) * 0.125, 0.0125); // Open Eyeblossom
						if (mat == 82) return vec4(vec3(2.8, 1.1, 0.2) * 0.3, 0.05); // Creaking Heart: Active
					} else {
						if (mat == 83) return vec4(vec3(1.6, 1.6, 0.7) * 0.3, 0.05); // Firefly Bush
						if (mat == 84) return vec4(vec3(0.85, 1.3, 1.0) * 3.9, 0.0); // Copper Torch, Copper Lantern
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
	// 200: White
	vec3(1.0),
	// 201: Orange
	vec3(1.0, 0.3, 0.1),
	// 202: Magenta
	vec3(1.0, 0.1, 1.0),
	// 203: Light Blue
	vec3(0.5, 0.65, 1.0),
	// 204: Yellow
	vec3(1.0, 1.0, 0.1),
	// 205: Lime
	vec3(0.1, 1.0, 0.1),
	// 206: Pink
	vec3(1.0, 0.4, 1.0),
	// 207: Gray
	vec3(1.0),
	// 208: Light Gray
	vec3(1.0),
	// 209: Cyan
	vec3(0.3, 0.8, 1.0),
	// 210: Purple
	vec3(0.7, 0.3, 1.0),
	// 211: Blue
	vec3(0.1, 0.15, 1.0),
	// 212: Brown
	vec3(1.0, 0.75, 0.5),
	// 213: Green
	vec3(0.3, 1.0, 0.3),
	// 214: Red
	vec3(1.0, 0.1, 0.1),
	// 215: Black
	vec3(1.0),
	// 216: Ice
	vec3(0.5, 0.65, 1.0),
	// 217: Glass
	vec3(1.0),
	// 218: Glass Pane
	vec3(1.0),
	// 219++
	vec3(0.0)
};;

static const vec3 caveFogColorRaw = vec3(0.13, 0.13, 0.15);
static vec3 caveFogColorRaw2; // Default
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


static float cloudHeightM; // iris returns "nan" if there are no clouds
static float cloudHeightOffset;
static int cloudAlt1i; // Old setting files can send float values
static int cloudAlt2i;

static const float OSIEBCA = 1.0 / 255.0; // One Step In Eight Bit Color Attachment
    /* materialMask steps
    0 to 240 - PBR Dependant:
        IntegratedPBR:
            0 to 99: deferredMaterials
                OSIEBCA * 0.0 = *Unused as 0.0 is the default value*
                OSIEBCA * 1.0 = Intense Fresnel
                OSIEBCA * 2.0 = Copper Fresnel
                OSIEBCA * 3.0 = Gold Fresnel
                OSIEBCA * 4.0 =
                OSIEBCA * 5.0 = Redstone Fresnel
            100 to 199: Exact copy of deferredMaterials but toned down reflection handling for entities
                materialMask += OSIEBCA * 100.0; // Entity Reflection Handling
            200 to 240: Random checks
                OSIEBCA * 240.0 = Green Screen Lime Blocks
        seuspbr:
            0 to 240: Increasing metalness
        labPBR:
            0 to 229: Increasing f0
            230 to 240: Consistent metalness with still increasing f0
    241 to 255 - PBR Independant:
        OSIEBCA * 241.0 = Water

        OSIEBCA * 251.0 = No SSAO, Reduce Reflection
        OSIEBCA * 252.0 = Versatile Selection Outline
        OSIEBCA * 253.0 = Reduced Edge 1 (Leaves)
        OSIEBCA * 254.0 = No SSAO, No 1, Reduce Reflection
        OSIEBCA * 255.0 = *Unused as 1.0 is the clear color*
    */

// 62 75 74 20 74 68 4F 73 65 20 77 68 6F 20 68 6F 70 65 20 69 6E 20 74 68 65 20 6C 69 6D 69 4E 61 6C 0A 77 69 6C 6C 20 72 65 6E 65 77 20 74 68 65 69 72 20 73 54 72 65 6E 67 74 48 2E 0A 74 68 65 79 20 77 69 6C 6C 20 73 6F 41 72 20 6F 6E 20 65 6C 79 54 72 61 73 20 6C 69 6B 65 20 70 68 61 6E 74 6F 6D 73 3B 0A 74 68 65 79 20 77 69 6C 6C 20 72 75 6E 20 61 6E 44 20 6E 6F 74 20 67 72 6F 77 20 77 65 41 72 79 2C 0A 74 68 65 59 20 77 69 6C 6C 20 77 61 6C 6B 20 61 6E 64 20 6E 6F 74 20 62 65 20 66 61 69 6E 74 2E

//////////Fragment Shader//////////Fragment Shader//////////Fragment Shader//////////

//////////Vertex Shader//////////Vertex Shader//////////Vertex Shader//////////




//Attributes//

//Common Variables//

//Common Functions//

//Includes//

//Program//
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

            vlFactor = texelFetch(s_colortex5, ivec2(viewWidth-1, viewHeight-1), 0).a;
}
}

namespace fs_stage
{
static float4 gl_FragCoord;
static float4 gl_FragData0;
static float4 gl_FragData1;
static float2 texCoord;
static float3 upVec;
static float3 sunVec;
static float vlFactor;
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
#define diagonal3(m) vec3((m)[0].x, (m)[1].y, m[2].z)
#define projMAD(m, v) (diagonal3(m) * (v) + transpose(m)[3].xyz)


// @included "/program/composite1.glsl"
/////////////////////////////////////
// Complementary Shaders by EminGT //
/////////////////////////////////////

//Common//
// @included "/lib/common.glsl"
/*---------------------------------------------------------------------
         ___ __  __ ____   ___  ____ _____  _    _   _ _____
        |_ _|  \/  |  _ \ / _ \|  _ \_   _|/ \  | \ | |_   _|
         | || |\/| | |_) | | | | |_) || | / _ \ |  \| | | |
         | || |  | |  __/| |_| |  _ < | |/ ___ \| |\  | | |
        |___|_|  |_|_|    \___/|_| \_\|_/_/   \_\_| \_| |_|
         .
  -> -> -> EDITING THIS FILE HAS A HIGH CHANCE TO BREAK THE SHADER PACK
  -> -> -> DO NOT CHANGE ANYTHING UNLESS YOU KNOW WHAT YOU ARE DOING
  -> -> -> DO NOT EXPECT SUPPORT AFTER MODIFYING SHADER FILES
---------------------------------------------------------------------*/

//User Settings//


static const float shadowDistance = 192.0; //[64.0 80.0 96.0 112.0 128.0 160.0 192.0 224.0 256.0 320.0 384.0 512.0 768.0 1024.0]





    //#define CLOUD_SHADOWS


    //#define DOUBLE_REIM_CLOUDS



    //#define LIGHTSHAFT_SMOKE



    //#define WB_FOV_SCALED
    //#define WB_CHROMATIC
    //#define WB_ANAMORPHIC



    //#define EMISSIVE_REDSTONE_BLOCK
    //#define EMISSIVE_LAPIS_BLOCK
    //#define GLOWING_ARMOR_TRIM

    //#define IPBR_COMPAT_MODE_DEFINE

    //#define POM_ALLOW_CUTOUT


    //#define WAVING_LILY_PAD



    //#define SELECT_OUTLINE_AUTO_HIDE

    //#define WORLD_OUTLINE
    //#define WORLD_OUTLINE_SCALED
    //#define DARK_OUTLINE

    //#define REDUCE_CLOSE_PARTICLES
    //#define LESS_LAVA_FOG
    //#define SNOWY_WORLD
    //#define COLOR_CODED_PROGRAMS
    //#define GLOWING_COLORED_PARTICLES

    //#define MOON_PHASE_INF_LIGHT
    //#define MOON_PHASE_INF_ATMOSPHERE

    //#define PIXELATED_SHADOWS
    //#define PIXELATED_BLOCKLIGHT
    //#define PIXELATED_AO

    //#define COLORGRADING

    //#define LIGHT_COLOR_MULTS
    //#define ATM_COLOR_MULTS




//Internal Settings//




    //#define SAFER_GENERATED_NORMALS


//Extensions//

//Information//

//RP Mode, Visual Style and Performance Setting Handling//
        //#define GENERATED_NORMALS
        //#define COATED_TEXTURES
        //#define GREEN_SCREEN_LIME

        //#define WATER_CAUSTIC_STYLE_DEFAULT 1
    // Thanks to SpacEagle17 and isuewo for the sun angle handling
static const float sunPathRotation = 0.0;

static const int shadowMapResolution = 2048;





//Define Handling//


















//Activate Settings//

//Very Common Stuff//
// @included "/lib/uniforms.glsl"
/*----------------------------------------------------------------------------------------------
        _____                                                                    _____
        ( ___ )                                                                  ( ___ )
        |   |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|   |
        |   | ██╗   ██╗███╗   ██╗██╗███████╗ ██████╗ ██████╗ ███╗   ███╗███████╗ |   |
        |   | ██║   ██║████╗  ██║██║██╔════╝██╔═══██╗██╔══██╗████╗ ████║██╔════╝ |   |
        |   | ██║   ██║██╔██╗ ██║██║█████╗  ██║   ██║██████╔╝██╔████╔██║███████╗ |   |
        |   | ██║   ██║██║╚██╗██║██║██╔══╝  ██║   ██║██╔══██╗██║╚██╔╝██║╚════██║ |   |
        |   | ╚██████╔╝██║ ╚████║██║██║     ╚██████╔╝██║  ██║██║ ╚═╝ ██║███████║ |   |
        |   |  ╚═════╝ ╚═╝  ╚═══╝╚═╝╚═╝      ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝ |   |
        |___|~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|___|
        (_____)                              (thanks to isuewo and SpacEagle17)  (_____)

---------------------------------------------------------------------------------------------*/




















/*-----------------------------------------------------------------------------
  ___ _   _ ___ _____ ___  __  __   _   _ _  _ ___ ___ ___  ___ __  __ ___
 / __| | | / __|_   _/ _ \|  \/  | | | | | \| |_ _| __/ _ \| _ \  \/  / __|
| (__| |_| \__ \ | || (_) | |\/| | | |_| | .` || || _| (_) |   / |\/| \__ 
 \___|\___/|___/ |_| \___/|_|  |_|  \___/|_|\_|___|_| \___/|_|_\_|  |_|___/

-----------------------------------------------------------------------------*/


static float tAmin;
static float tAlin;
static float hA;
static float tAfrc;
static float tAfrs;
static float tAmix;
static float timeAngle;

// @included "/lib/util/commonFunctions.glsl"
// Do these via macro as Apple otherwise complains

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

bool CheckForColor(vec3 albedo, vec3 check) { // Thanks to Builderb0y - edited a bit later
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

float pow1_5(float x) { // Faster pow(x, 1.5) approximation (that isn't accurate at all) if x is between 0 and 1
    return x - x * pow2(1.0 - x); // Thanks to SixthSurge
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

float sqrt1(float x) { // Faster sqrt() approximation (that isn't accurate at all) if x is between 0 and 1
    return x * (2.0 - x); // Thanks to Builderb0y
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

static float nightVisionWithAddedSupport; // Add support for fullbright mods

static int modifiedWorldDay;
static float syncedTime;

static const float pi = 3.14159265359;
static const float goldenRatio = 1.61803398875;

static const float oceanAltitude = 61.9;

static ivec3 cameraPositionBestInt;
static vec3 cameraPositionBestFract;
static vec3 previousCameraPositionBestFract;

// @included "/lib/colors/blocklightColors.glsl"
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
	/* Please note that these colors do not determine the intensity of the
	final light. Instead; higher values of color change how long the color
	will travel, and also how dominant it will be next to other colors.*/
	/* Additional feature: An alpha value bigger than 0 will make that
	block cast extra light regardless of the vanilla s_lightmap. Use this
	with caution though because our floodfill isn't as accurate as vanilla.*/

	if (mat < 50) {
		if (mat < 26) {
			if (mat < 14) {
				if (mat < 8) {
					if (mat < 5) {
						if (mat == 2) return vec4(fireSpecialLightColor, 0.0); // Torch
							if (mat == 3) return vec4(vec3(1.0, 1.0, 1.0) * 4.0, 0.0); // End Rod - This is the base for all lights. Total value 12
						if (mat == 4) return vec4(vec3(0.7, 1.5, 2.0) * 3.0, 0.0); // Beacon
					} else {
						if (mat == 5) return vec4(fireSpecialLightColor, 0.0); // Fire
						if (mat == 6) return vec4(vec3(0.7, 1.5, 1.5) * 1.7, 0.0); // Sea Pickle:Waterlogged
						if (mat == 7) return vec4(vec3(1.1, 0.85, 0.35) * 5.0, 0.0); // Ochre Froglight
					}
				} else {
					if (mat < 11) {
						if (mat == 8) return vec4(vec3(0.6, 1.3, 0.6) * 4.5, 0.0); // Verdant Froglight
						if (mat == 9) return vec4(vec3(1.1, 0.5, 0.9) * 4.5, 0.0); // Pearlescent Froglight
						if (mat == 10) return vec4(vec3(1.7, 0.9, 0.4) * 4.0, 0.0); // Glowstone
					} else {
						if (mat == 11) return vec4(fireSpecialLightColor, 0.0); // Jack o'Lantern
						if (mat == 12) return vec4(fireSpecialLightColor, 0.0); // Lantern
						if (mat == 13) return vec4(lavaSpecialLightColor, 0.8); // Lava
					}
				}
			} else {
				if (mat < 20) {
					if (mat < 17) {
						if (mat == 14) return vec4(lavaSpecialLightColor, 0.0); // Lava Cauldron
						if (mat == 15) return vec4(fireSpecialLightColor, 0.0); // Campfire:Lit
						if (mat == 16) return vec4(vec3(1.7, 0.9, 0.4) * 4.0, 0.0); // Redstone Lamp:Lit
					} else {
						if (mat == 17) return vec4(vec3(1.7, 0.9, 0.4) * 2.0, 0.0); // Respawn Anchor:Lit
						if (mat == 18) return vec4(vec3(1.0, 1.25, 1.5) * 3.4, 0.0); // Sea Lantern
						if (mat == 19) return vec4(vec3(3.0, 0.9, 0.2) * 3.0, 0.0); // Shroomlight
					}
				} else {
					if (mat < 23) {
						if (mat == 20) return vec4(vec3(2.3, 0.9, 0.2) * 3.4, 0.0); // Cave Vines:With Glow Berries
						if (mat == 21) return vec4(fireSpecialLightColor * 0.7, 0.0); // Furnace:Lit
						if (mat == 22) return vec4(fireSpecialLightColor * 0.7, 0.0); // Smoker:Lit
					} else {
						if (mat == 23) return vec4(fireSpecialLightColor * 0.7, 0.0); // Blast Furnace:Lit
						if (mat == 24) return vec4(fireSpecialLightColor * 0.25 * candleColorMult, candleExtraLight); // Standard Candles:Lit
						if (mat == 25) return vec4(netherPortalSpecialLightColor * 2.0, 0.4); // Nether Portal
					}
				}
			}
		} else {
			if (mat < 38) {
				if (mat < 32) {
					if (mat < 29) {
						if (mat == 26) return vec4(netherPortalSpecialLightColor, 0.0); // Crying Obsidian
						if (mat == 27) return soulFireSpecialColor; // Soul Fire
						if (mat == 28) return soulFireSpecialColor; // Soul Torch
					} else {
						if (mat == 29) return soulFireSpecialColor; // Soul Lantern
						if (mat == 30) return soulFireSpecialColor; // Soul Campfire:Lit
						if (mat == 31) return vec4(redstoneSpecialLightColor * 0.5, 0.1); // Redstone Ores:Lit
					}
				} else {
					if (mat < 35) {
						if (mat == 32) return vec4(redstoneSpecialLightColor * 0.3, 0.1) * 1.00; // Redstone Ores:Unlit
						if (mat == 33) return vec4(vec3(1.4, 1.1, 0.5), 0.05); // Enchanting Table
							if (mat == 34) return vec4(vec3(0.8, 1.1, 1.1), 0.05); // Glow Lichen with IntegratedPBR
					} else {
						if (mat == 35) return vec4(redstoneSpecialLightColor * 0.25, 0.0); // Redstone Torch
						if (mat == 36) return vec4(vec3(0.325, 0.15, 0.425) * 2.0, 0.05); // Amethyst Cluster, Amethyst Buds, Calibrated Sculk Sensor
						if (mat == 37) return vec4(lavaSpecialLightColor * 0.1, 0.1); // Magma Block
					}
				}
			} else {
				if (mat < 44) {
					if (mat < 41) {
						if (mat == 38) return vec4(vec3(2.0, 0.5, 1.5) * 0.3, 0.1); // Dragon Egg
						if (mat == 39) return vec4(vec3(2.0, 1.0, 1.5) * 0.25, 0.1); // Chorus Flower
						if (mat == 40) return vec4(vec3(2.5, 1.2, 0.4) * 0.1, 0.1); // Brewing Stand
					} else {
						if (mat == 41) return vec4(redstoneSpecialLightColor * 0.4, 0.15); // Redstone Block
						if (mat == 42) return vec4(vec3(0.75, 0.75, 3.0) * 0.277, 0.15); // Lapis Block
						if (mat == 43) return vec4(vec3(1.7, 0.9, 0.4) * 0.45, 0.05) * 1.00; // Iron Ores
					}
				} else {
					if (mat < 47) {
						if (mat == 44) return vec4(vec3(1.7, 1.1, 0.2) * 0.45, 0.1) * 1.00; // Gold Ores
						if (mat == 45) return vec4(vec3(1.7, 0.8, 0.4) * 0.45, 0.05) * 1.00; // Copper Ores
						if (mat == 46) return vec4(vec3(0.75, 0.75, 3.0) * 0.2, 0.1) * 1.00; // Lapis Ores
					} else {
						if (mat == 47) return vec4(vec3(0.5, 3.5, 0.5) * 0.3, 0.1) * 1.00; // Emerald Ores
						if (mat == 48) return vec4(vec3(0.5, 2.0, 2.0) * 0.4, 0.15) * 1.00; // Diamond Ores
						if (mat == 49) return vec4(vec3(1.5, 1.5, 1.5) * 0.3, 0.05) * 1.00; // Nether Quartz Ore
					}
				}
			}
		}
	} else {
		if (mat < 74) {
			if (mat < 62) {
				if (mat < 56) {
					if (mat < 53) {
						if (mat == 50) return vec4(vec3(1.7, 1.1, 0.2) * 0.45, 0.05) * 1.00; // Nether Gold Ore
						if (mat == 51) return vec4(vec3(1.7, 1.1, 0.2) * 0.45, 0.05); // Gilded Blackstone
						if (mat == 52) return vec4(vec3(1.8, 0.8, 0.4) * 0.6, 0.15); // Ancient Debris
					} else {
						if (mat == 53) return vec4(vec3(1.4, 0.2, 1.4) * 0.3, 0.05); // Spawner
						if (mat == 54) return vec4(vec3(3.1, 1.1, 0.3) * 1.0, 0.1); // Trial Spawner:NotOminous:Active, Vault:NotOminous:Active
						if (mat == 55) return vec4(vec3(1.7, 0.9, 0.4) * 4.0, 0.0); // Copper Bulb:BrighterOnes:Lit
					}
				} else {
					if (mat < 59) {
						if (mat == 56) return vec4(vec3(1.7, 0.9, 0.4) * 2.0, 0.0); // Copper Bulb:DimmerOnes:Lit
						if (mat == 57) return vec4(vec3(0.1, 0.3, 0.4) * 0.5, 0.0005); // Sculk++
						if (mat == 58) return vec4(vec3(0.0, 1.4, 1.4) * 4.0, 0.15); // End Portal Frame:Active
					} else {
						if (mat == 59) return vec4(0.0); // Bedrock
						if (mat == 60) return vec4(vec3(3.1, 1.1, 0.3) * 0.125, 0.0125); // Command Block
						if (mat == 61) return vec4(vec3(3.0, 0.9, 0.2) * 0.125, 0.0125); // Warped Fungus, Crimson Fungus
					}
				}
			} else {
				if (mat < 68) {
					if (mat < 65) {
						if (mat == 62) return vec4(vec3(3.5, 0.6, 0.4) * 0.3, 0.05); // Crimson Stem, Crimson Hyphae
						if (mat == 63) return vec4(vec3(0.3, 1.9, 1.5) * 0.3, 0.05); // Warped Stem, Warped Hyphae
						if (mat == 64) return vec4(vec3(1.0, 1.0, 1.0) * 0.45, 0.1); // Structure Block, Jigsaw Block, Test Block, Test Instance Block
					} else {
						if (mat == 65) return vec4(vec3(3.0, 0.9, 0.2) * 0.125, 0.0125); // Weeping Vines Plant
						if (mat == 66) return vec4(redstoneSpecialLightColor * 0.05, 0.002); // Redstone Wire:Lit, Comparator:Unlit:Subtract
						if (mat == 67) return vec4(redstoneSpecialLightColor * 0.125, 0.0125); // Repeater:Lit, Comparator:Lit
					}
				} else {
					if (mat < 71) {
						if (mat == 68) return vec4(vec3(0.75), 0.0); // Vault:Inactive
						if (mat == 69) return vec4(vec3(1.3, 1.6, 1.6) * 1.0, 0.1); // Trial Spawner:Ominous:Active, Vault:Ominous:Active
						if (mat == 70) return vec4(vec3(1.0, 0.1, 0.1) * candleColorMult, candleExtraLight); // Red Candles:Lit
					} else {
						if (mat == 71) return vec4(vec3(1.0, 0.4, 0.1) * candleColorMult, candleExtraLight); // Orange Candles:Lit
						if (mat == 72) return vec4(vec3(1.0, 1.0, 0.1) * candleColorMult, candleExtraLight); // Yellow Candles:Lit
						if (mat == 73) return vec4(vec3(0.1, 1.0, 0.1) * candleColorMult, candleExtraLight); // Lime Candles:Lit
					}
				}
			}
		} else {
			if (mat < 86) {
				if (mat < 80) {
					if (mat < 77) {
						if (mat == 74) return vec4(vec3(0.3, 1.0, 0.3) * candleColorMult, candleExtraLight); // Green Candles:Lit
						if (mat == 75) return vec4(vec3(0.3, 0.8, 1.0) * candleColorMult, candleExtraLight); // Cyan Candles:Lit
						if (mat == 76) return vec4(vec3(0.5, 0.65, 1.0) * candleColorMult, candleExtraLight); // Light Blue Candles:Lit
					} else {
						if (mat == 77) return vec4(vec3(0.1, 0.15, 1.0) * candleColorMult, candleExtraLight); // Blue Candles:Lit
						if (mat == 78) return vec4(vec3(0.7, 0.3, 1.0) * candleColorMult, candleExtraLight); // Purple Candles:Lit
						if (mat == 79) return vec4(vec3(1.0, 0.1, 1.0) * candleColorMult, candleExtraLight); // Magenta Candles:Lit
					}
				} else {
					if (mat < 83) {
						if (mat == 80) return vec4(vec3(1.0, 0.4, 1.0) * candleColorMult, candleExtraLight); // Pink Candles:Lit
						if (mat == 81) return vec4(vec3(2.8, 1.1, 0.2) * 0.125, 0.0125); // Open Eyeblossom
						if (mat == 82) return vec4(vec3(2.8, 1.1, 0.2) * 0.3, 0.05); // Creaking Heart: Active
					} else {
						if (mat == 83) return vec4(vec3(1.6, 1.6, 0.7) * 0.3, 0.05); // Firefly Bush
						if (mat == 84) return vec4(vec3(0.85, 1.3, 1.0) * 3.9, 0.0); // Copper Torch, Copper Lantern
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
	// 200: White
	vec3(1.0),
	// 201: Orange
	vec3(1.0, 0.3, 0.1),
	// 202: Magenta
	vec3(1.0, 0.1, 1.0),
	// 203: Light Blue
	vec3(0.5, 0.65, 1.0),
	// 204: Yellow
	vec3(1.0, 1.0, 0.1),
	// 205: Lime
	vec3(0.1, 1.0, 0.1),
	// 206: Pink
	vec3(1.0, 0.4, 1.0),
	// 207: Gray
	vec3(1.0),
	// 208: Light Gray
	vec3(1.0),
	// 209: Cyan
	vec3(0.3, 0.8, 1.0),
	// 210: Purple
	vec3(0.7, 0.3, 1.0),
	// 211: Blue
	vec3(0.1, 0.15, 1.0),
	// 212: Brown
	vec3(1.0, 0.75, 0.5),
	// 213: Green
	vec3(0.3, 1.0, 0.3),
	// 214: Red
	vec3(1.0, 0.1, 0.1),
	// 215: Black
	vec3(1.0),
	// 216: Ice
	vec3(0.5, 0.65, 1.0),
	// 217: Glass
	vec3(1.0),
	// 218: Glass Pane
	vec3(1.0),
	// 219++
	vec3(0.0)
};;

static const vec3 caveFogColorRaw = vec3(0.13, 0.13, 0.15);
static vec3 caveFogColorRaw2; // Default
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

static float cloudHeightM; // iris returns "nan" if there are no clouds
static float cloudHeightOffset;
static int cloudAlt1i; // Old setting files can send float values
static int cloudAlt2i;

static const float OSIEBCA = 1.0 / 255.0; // One Step In Eight Bit Color Attachment
    /* materialMask steps
    0 to 240 - PBR Dependant:
        IntegratedPBR:
            0 to 99: deferredMaterials
                OSIEBCA * 0.0 = *Unused as 0.0 is the default value*
                OSIEBCA * 1.0 = Intense Fresnel
                OSIEBCA * 2.0 = Copper Fresnel
                OSIEBCA * 3.0 = Gold Fresnel
                OSIEBCA * 4.0 =
                OSIEBCA * 5.0 = Redstone Fresnel
            100 to 199: Exact copy of deferredMaterials but toned down reflection handling for entities
                materialMask += OSIEBCA * 100.0; // Entity Reflection Handling
            200 to 240: Random checks
                OSIEBCA * 240.0 = Green Screen Lime Blocks
        seuspbr:
            0 to 240: Increasing metalness
        labPBR:
            0 to 229: Increasing f0
            230 to 240: Consistent metalness with still increasing f0
    241 to 255 - PBR Independant:
        OSIEBCA * 241.0 = Water

        OSIEBCA * 251.0 = No SSAO, Reduce Reflection
        OSIEBCA * 252.0 = Versatile Selection Outline
        OSIEBCA * 253.0 = Reduced Edge 1 (Leaves)
        OSIEBCA * 254.0 = No SSAO, No 1, Reduce Reflection
        OSIEBCA * 255.0 = *Unused as 1.0 is the clear color*
    */

// 62 75 74 20 74 68 4F 73 65 20 77 68 6F 20 68 6F 70 65 20 69 6E 20 74 68 65 20 6C 69 6D 69 4E 61 6C 0A 77 69 6C 6C 20 72 65 6E 65 77 20 74 68 65 69 72 20 73 54 72 65 6E 67 74 48 2E 0A 74 68 65 79 20 77 69 6C 6C 20 73 6F 41 72 20 6F 6E 20 65 6C 79 54 72 61 73 20 6C 69 6B 65 20 70 68 61 6E 74 6F 6D 73 3B 0A 74 68 65 79 20 77 69 6C 6C 20 72 75 6E 20 61 6E 44 20 6E 6F 74 20 67 72 6F 77 20 77 65 41 72 79 2C 0A 74 68 65 59 20 77 69 6C 6C 20 77 61 6C 6B 20 61 6E 64 20 6E 6F 74 20 62 65 20 66 61 69 6E 74 2E

//////////Fragment Shader//////////Fragment Shader//////////Fragment Shader//////////




//Pipeline Stuff//


//Common Variables//
static float SdotU;
static float sunFactor;
static float sunVisibility;
static float sunVisibility2;

static vec2 view;

static vec3 lightVec;

static float shadowTimeVar1;
static float shadowTimeVar2;
static float shadowTime;

//Common Functions//
float GetLinearDepth(float depth) {
    return (2.0 * near) / (far + near - depth * (far - near));
}

//Includes//
// @included "/lib/atmospherics/fog/waterFog.glsl"

    float GetWaterFog(float lViewPos) {

            float fog = lViewPos / 48.0;
            fog *= fog;

        return 1.0 - exp(-fog);
    }
// @included "/lib/atmospherics/fog/caveFactor.glsl"

    float GetCaveFactor() {
        float caveFactor = clamp(1.0 - cameraPosition.y / oceanAltitude, 0.0, 1.0 - eyeBrightnessM);
        return caveFactor;
    }
// @included "/lib/util/spaceConversion.glsl"

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
// @included "/lib/colors/lightAndAmbientColors.glsl"

static vec3 noonClearLightColor; //light shaft color
static vec3 noonClearAmbientColor;

static vec3 sunsetClearLightColor; //light shaft color
static vec3 sunsetClearAmbientColor;

static vec3 nightClearLightColor; //light shaft color
static vec3 nightClearAmbientColor;

static vec3 drlcSnowM;
static vec3 drlcDryM;
static vec3 drlcRainM;
static vec3 dayRainLightColor;
static vec3 dayRainAmbientColor;

static vec3 nightRainLightColor;
static vec3 nightRainAmbientColor;

static float noonFactorDM; //light shaft factor
static vec3 dayLightColor;
static vec3 dayAmbientColor;

static vec3 clearLightColor;
static vec3 clearAmbientColor;

static float rainShadowVisReduce;

static vec3 rainLightColor;
static vec3 rainAmbientColor;

static vec3 lightColor;
static vec3 ambientColor;


// @included "/lib/materials/materialMethods/reflectionBlurFilter.glsl"
vec4 sampleBlurFilteredReflection(vec4 centerCol, vec3 nViewPos, float dither, float z0) {
    vec4 texture4 = texture2D(s_colortex4, texCoord);

    const float spatialFactor = 2.5; // higher = smoother in space
    const float spatialFactorM = 2.0 * spatialFactor * spatialFactor;

    vec4 sum = vec4(0.0);
    float weightSum = 0.0;
    vec2 texelSize = (3.0 + 6.0 * dither) / view; // 1 pixel range doesn't seem to be enough to smooth things out

        vec3 texture6 = texelFetch(s_colortex6, texelCoord, 0).rgb;
        float smoothnessD = texture6.r;
        texelSize *= 1.0 - 0.75 * pow2(pow2(pow2(smoothnessD)));


    int k = 2;
    for (int dy = -k; dy <= k; dy++) {
        for (int dx = -k; dx <= k; dx++) {
            vec2 offset = vec2(float(dx), float(dy)) * texelSize;
            vec2 sampleCoord = texCoord + offset;
            vec4 sampleCol = texture2D(s_colortex7, sampleCoord);

            // Skip step if s_normals are too different
            vec4 texture1Sample = texture2D(s_colortex1, sampleCoord);
            if (length(texture4.rgb - texture1Sample.rgb) > 0.1 + 0.075 * dither) continue;

            // Skip if depth is too different (costs performance for a tiny fix)

            // Spatial weight (gaussian)
            float spatialDist2 = float(dx*dx + dy*dy);
            float weight = exp(-spatialDist2 / spatialFactorM);

            sum  += sampleCol * weight;
            weightSum += weight;
        }
    }

    return sum / weightSum;
}

// @included "/lib/atmospherics/fog/bloomFog.glsl"
// @included "/lib/atmospherics/fog/caveFactor.glsl"


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

// @included "/lib/lighting/shadowSampling.glsl"
vec3 GetShadowPos(vec3 playerPos) {
    vec3 shadowPos = PlayerToShadow(playerPos);
    float distb = sqrt(shadowPos.x * shadowPos.x + shadowPos.y * shadowPos.y);
    float distortFactor = distb * shadowMapBias + (1.0 - shadowMapBias);
    
    
    return shadowPos * 0.5 + 0.5;
}

// @included "/lib/atmospherics/volumetricLight/volumetricLight.glsl"
vec4 DistortShadow(vec4 shadowpos, float distortFactor) {
    
    
    shadowpos = shadowpos * 0.5 + 0.5;

    return shadowpos;
}

vec4 GetVolumetricLight(inout float vlFactor, vec3 translucentMult, float lViewPos0, float lViewPos1, vec3 nViewPos, float VdotL, float VdotU, float z0, float z1, float z1lod, float dither) {
// ============================== Step 1: Prepare Factors ============================== //
    float vlMult = 1.0 - maxBlindnessDarkness;

        vec3 vlColor = lightColor;
        vec3 vlColorReducer = vec3(1.0);
        float vlSceneIntensity = isEyeInWater != 1 ? vlFactor : 1.0;

            vlSceneIntensity = mix(vlSceneIntensity, 1.0, inDry * rainFactor);
            vlColor *= 1.0 + 0.6 * inDry * rainFactor;

        if (sunVisibility < 0.5) {
            vlSceneIntensity = 0.0;

            float vlMultNightModifier = (0.3 + 0.4 * rainFactor2 + 0.5 * max0(far - lViewPos1) / far);
                vlMultNightModifier = mix(vlMultNightModifier, 1.0, inPaleGarden);
            vlMult *= vlMultNightModifier;

            vlColor = normalize(pow(vlColor, vec3(1.0 - max0(1.0 - 1.5 * nightFactor) + rainFactor)));
            vlColor *= 0.0766 + 0.0766 * vsBrightness;
        } else {
            vlColorReducer = 1.0 / sqrt(vlColor);
        }

            vlSceneIntensity = mix(vlSceneIntensity, 1.0, inPaleGarden);
            vlMult *= 1.0 + (3.0 * inPaleGarden) * (1.0 - sunVisibility);

        float rainyNight = (1.0 - sunVisibility) * rainFactor;
        float VdotLM = max((VdotL + 1.0) / 2.0, 0.0);
        float VdotUmax0 = max(VdotU, 0.0);
        float VdotUM = mix(pow2(1.0 - VdotUmax0), 1.0, 0.5 * vlSceneIntensity);
              VdotUM = smoothstep1(VdotUM);
              VdotUM = pow(VdotUM, min(lViewPos1 / far, 1.0) * (3.0 - 2.0 * vlSceneIntensity));

        float vlTime = min(abs(SdotU) - 0.05, 0.15) / 0.15;
        vlMult *= mix(VdotUM * VdotLM, 1.0, 0.4 * rainyNight) * vlTime;

        vlMult *= mix(invNoonFactor2 * 0.875 + 0.125, 1.0, max(vlSceneIntensity, rainFactor2));
        vlMult *= 1.4 - 0.4 * rainFactor;

        float entityLinearDepth = texture2D(s_colortex13, texCoord).r;
              entityLinearDepth = entityLinearDepth == 0.0 ? 1000000.0 : pow2(entityLinearDepth) * far;

    if (vlMult < 0.0001) return vec4(0.0);
// ============================== End of Step 1 ============================== //

// ============================== Step 2: Prepare Tracing Variables  ============================== //
    vec4 volumetricLight = vec4(0.0);

        // Optifine for some reason doesn't provide correct shadowMapResolution if Shadow Quality isn't 1x
        vec2 shadowMapResolutionM = javaTextureSize(s_shadow, 0);

    bool sky = z1 == 1.0;

            int nearSamples = 10;

    // Balance Defining Variables
        int farSamples = 1;
        float fogCurve = 0.9; // 1.0 = linear fog
        float fogCurveIntense = 0.5;
        float sampleDistribution = 2.0; // sample distribution power (higher values push near samples closer to camera)
        float sampleDistributionFar = 2.5;

        float maxDistance = far * 0.98; // The distance where the fog is 1.0 and we stop tracing
        float qualityThreshold = min(far, shadowDistance) * 0.98; // Only use farSamples from this point on to hit our maxDistance. Set higher than maxDistance to disable far samples


        maxDistance = max(maxDistance, 128.0);

        // Tweaks for intense situations
        if (isEyeInWater == 1) {
            maxDistance = mix(maxDistance, 128.0, isEyeInWater == 1);
        }
        qualityThreshold = mix(qualityThreshold, 100.0, vlSceneIntensity);
        fogCurve = mix(fogCurve, fogCurveIntense, max(vlSceneIntensity, rainFactor));
        nearSamples = int(mix(float(nearSamples), 1.5 * nearSamples, vlSceneIntensity));
        sampleDistribution = mix(sampleDistribution, 1.5, vlSceneIntensity);


    vec3 nViewPosInSceneSpace = normalize(mat3(gbufferModelViewInverse) * nViewPos);
    vec3 rayDirection = nViewPosInSceneSpace;
    float rayEnd = !sky ? min(lViewPos1, maxDistance) : maxDistance;
    float lastDistance = 0.0;



    float activeThreshold = min(qualityThreshold, maxDistance);
    int totalSamples = (rayEnd <= qualityThreshold) ? nearSamples : (nearSamples + farSamples);
// ============================== End of Step 2 ============================== //

// ============================== Step 3: Execute Volumetric Tracing ============================== //
    for (int i = 0; i <= totalSamples; i++) {
        float nextDistance;
        vec3 scenePos;
        vec3 localDensity = vec3(1.0);

        if (i <= nearSamples) { // Actual light shaft sampling near-field section
            float t = (float(i) + dither) / float(nearSamples);
            nextDistance = pow(t, sampleDistribution) * activeThreshold;
            scenePos = rayDirection * nextDistance;

            if (nextDistance >= rayEnd) break;

            // Shadow Sampling
            localDensity = vec3(1.0);
                vec3 shadowPos = GetShadowPos(scenePos);
                if (length(shadowPos.xy * 2.0 - 1.0) < 1.0) {
                    // 28A3DK6 We need to use texelFetch here or a lot of Nvidia GPUs can't get a valid value
                    float shadowSample = texelFetch(s_shadow, ivec2(shadowPos.xy * shadowMapResolutionM), 0).x;
                          shadowSample = clamp((shadowSample - shadowPos.z) * 65536.0, 0.0, 1.0);

                    localDensity = vec3(shadowSample);

                        if (shadowSample == 0.0) {
                            float translucentShadowSample = javaShadow2D(s_shadowB, s_shadowB_s, shadowPos.xyz).z;
                            if (translucentShadowSample == 1.0) {
                                vec3 shadowColorSample = texture2D(s_shadowTintB, shadowPos.xy).rgb * 4.0;
                                localDensity = pow2(shadowColorSample) * vlColorReducer;
                            }
                        } else {
                                // For water-tinting the water surface when observed from below the surface
                                if (any((translucentMult) != (vec3(1.0))) &&  nextDistance > lViewPos0) {
                                    vec3 tinter = vec3(1.0);
                                    if (isEyeInWater == 1) {
                                        vec3 translucentMultM = translucentMult * 2.8;
                                        tinter = pow(translucentMultM, vec3(sunVisibility * 3.0 * clamp01(scenePos.y * 0.03)));
                                    } else {
                                        tinter = 0.1 + 0.9 * pow2(pow2(translucentMult * 1.7));
                                    }
                                    localDensity *= mix(vec3(1.0), tinter, clamp01(oceanAltitude - cameraPosition.y));
                                }

                            if (isEyeInWater == 1  && all((translucentMult) == (vec3(1.0)))) localDensity = vec3(0.0);
                        }
                }
        }

        else { // Far field low sample section
            float t = (float(i - nearSamples) + dither) / float(farSamples);
            nextDistance = mix(activeThreshold, maxDistance, pow(t, sampleDistributionFar));
            scenePos = rayDirection * nextDistance;

            if (nextDistance >= rayEnd) {
                nextDistance = rayEnd;
            }

                localDensity = vec3(eyeBrightnessM);
        }


        // For fog density to not move regardless of how we distribute the samples
        float currentWeight = smoothstep1(pow(lastDistance / maxDistance, fogCurve));
        float nextWeight    = smoothstep1(pow(nextDistance / maxDistance, fogCurve));
        float sliceWeight   = nextWeight - currentWeight;

        vec4 stepResult = vec4(localDensity * sliceWeight, sliceWeight);

        // Result Coloring
        if (nextDistance > lViewPos0) {
                if (nextDistance > entityLinearDepth) break; // fixes entities looking transparent behind glass
            stepResult.rgb *= translucentMult;
        }

        volumetricLight += stepResult;

        lastDistance = nextDistance;
    }
// ============================== End of Step 3 ============================== //

// ============================== Step 4: Calculate factor of Scene Aware Light Shafts ============================== //
        if (viewWidth + viewHeight - gl_FragCoord.x - gl_FragCoord.y < 1.5) {
            if (frameCounter % int(0.06666 / frameTimeSmooth + 0.5) == 0) { // Change speed is not too different above 10 fps
                int salsX = 5;
                int salsY = 5;
                float heightThreshold = 6.0;

                vec2 viewM = 1.0 / vec2(salsX, salsY);
                float salsSampleSum = 0.0;
                int salsSampleCount = 0;
                for (float i = 0.25; i < salsX; i++) {
                    for (float h = 0.45; h < salsY; h++) {
                        vec2 coord = 0.3 + 0.4 * viewM * vec2(i, h);
                        ivec2 icoord = ivec2(coord * shadowMapResolutionM);
                        float salsSample = texelFetch(s_shadow, icoord, 0).x; // read 28A3DK6
                        if (salsSample < 0.55) {
                            float sampledHeight = texture2D(s_shadowTintB, coord).a;
                            if (sampledHeight > 0.0) {
                                sampledHeight = max0(sampledHeight - 0.25) / 0.05; // consistencyMEJHRI7DG
                                salsSampleSum += sampledHeight;
                                salsSampleCount++;
                            }
                        }
                    }
                }

                float salsCheck = salsSampleSum / salsSampleCount;
                int reduceAmount = 2;

                int skyCheck = 0;
                float translucentDistanceTreshold = 5.0 / far;
                for (float i = 0.1; i < 1.0; i += 0.2) {
                    ivec2 checkCoord = ivec2(view.x * i, view.y * 0.9);
                    float depth0Check = texelFetch(s_sceneDepth, checkCoord, 0).x;

                    if (GetLinearDepth(depth0Check) < translucentDistanceTreshold) {
                        float depth1Check = texelFetch(s_depth, checkCoord, 0).x;

                        if (depth1Check == 1.0) {
                            skyCheck += 1;
                        }
                    } else if (depth0Check == 1.0) {
                        skyCheck += 1;
                    }
                }
                if (skyCheck >= 3) {
                    salsCheck = 0.0;
                    reduceAmount = 3;
                }

                if (salsCheck > heightThreshold) {
                    vlFactor = min(vlFactor + OSIEBCA, 1.0);
                } else {
                    vlFactor = max(vlFactor - OSIEBCA * reduceAmount, 0.0);
                }
            }
        } else vlFactor = 0.0;
// ============================== End of Step 4 ============================== //

// ============================== Step 5: Final Tweaks ============================== //
        vlColor = pow(vlColor, vec3(0.5 + (0.5 + 0.25 * sunVisibility) * invNoonFactor * invRainFactor + 0.3 * rainFactor));
        vlColor *= 1.0 - (0.3 + 0.3 * noonFactor) * rainFactor - 0.5 * rainyNight + sunVisibility * invNoonFactor2 * invRainFactor;


        volumetricLight.rgb *= vlColor;


    volumetricLight.rgb *= vlMult;

    volumetricLight = max(volumetricLight, vec4(0.0)); // Fixes rare nans
// ============================== End of Step 5 ============================== //

    return volumetricLight;
}




// @included "/lib/atmospherics/rainbow.glsl"

vec3 GetRainbow(vec3 translucentMult, vec3 nViewPos, float z0, float z1, float lViewPos, float lViewPos1, float VdotL, float VdotU, float dither) {
    vec3 rainbow = vec3(0.0);

    float rainbowTime = min1(max0(SdotU - 0.1) / 0.15);
    rainbowTime = clamp(rainbowTime - pow2(pow2(pow2(noonFactor))) * 8.0, 0.0, 0.85);
        rainbowTime *= sqrt2(max0(wetness - 0.333) * 1.5) * invRainFactor * inRainy;

    if (rainbowTime > 0.001) {
        float rainbowLength = far * 0.9;
        if (z1 == 1.0) lViewPos1 = rainbowLength;

        float cloudLinearDepth = texelFetch(s_colortex5, texelCoord, 0).a;
        float cloudDisMult = pow2(cloudLinearDepth + OSIEBCA * dither);
        lViewPos1 *= cloudDisMult;

            float pixelScale = 45.0;
            vec3 shadowDir = mat3(shadowModelView) * mat3(gbufferModelViewInverse) * nViewPos;

            shadowDir.z += 0.0065 * (dither - 0.5); // Blurs the pixelation

            shadowDir /= abs(shadowDir.z); // Corrects distortion
            shadowDir.xy = floor(shadowDir.xy * pixelScale) / pixelScale;

            VdotL = shadowDir.z * inversesqrt(dot(shadowDir, shadowDir));

        float rainbowCoord = clamp01(1.0 - (VdotL + 0.75) / (0.0625 * 1.00));
        float rainbowFactor = rainbowCoord * (1.0 - rainbowCoord);
              rainbowFactor = pow2(pow2(rainbowFactor * 3.7));
              rainbowFactor *= pow2(min1(lViewPos1 / rainbowLength));
              rainbowFactor *= rainbowTime;
              rainbowFactor *= 1.0 - GetCaveFactor();

        if (rainbowFactor > 0.0) {
            float rainbowCoordM = pow(rainbowCoord, 1.4 + max(rainbowCoord - 0.5, 0.0) * 1.6);
            rainbowCoordM = smoothstep(0.0, 1.0, rainbowCoordM) * 0.85;
            rainbowCoordM += (dither - 0.5) * 0.1;

            rainbow += clamp(abs(mod(rainbowCoordM * 6.0 + vec3(-0.55,4.3,2.2) ,6.0)-3.0)-1.0, 0.0, 1.0);
            rainbowCoordM += 0.1;
            rainbow += clamp(abs(mod(rainbowCoordM * 6.0 + vec3(-0.55,4.3,2.2) ,6.0)-3.0)-1.0, 0.0, 1.0);
            rainbowCoordM -= 0.2;
            rainbow += clamp(abs(mod(rainbowCoordM * 6.0 + vec3(-0.55,4.3,2.2) ,6.0)-3.0)-1.0, 0.0, 1.0);
            rainbow /= 3.0;

            rainbow.r += pow2(max(rainbowCoord - 0.5, 0.0)) * (max(1.0 - rainbowCoord, 0.0)) * 26.0;
            rainbow = pow(rainbow, vec3(2.2)) * vec3(0.25, 0.075, 0.25) * 3.0;

            if (z1 > z0  &&  lViewPos < rainbowLength)
            rainbow *= mix(translucentMult, vec3(1.0), lViewPos / rainbowLength);

            if (isEyeInWater != 0) rainbow *= sqrt1(max0(VdotU));

            rainbow *= rainbowFactor;
        }
    }

    return rainbow;
}


//Program//
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
    SdotU = dot(sunVec, upVec);
    sunFactor = SdotU < 0.0 ? clamp(SdotU + 0.375, 0.0, 0.75) / 0.75 : clamp(SdotU + 0.03125, 0.0, 0.0625) / 0.0625;
    sunVisibility = clamp(SdotU + 0.0625, 0.0, 0.125) / 0.125;
    sunVisibility2 = sunVisibility * sunVisibility;
    view = vec2(viewWidth, viewHeight);
    lightVec = sunVec * ((timeAngle < 0.5325 || timeAngle > 0.9675) ? 1.0 : -1.0);
    shadowTimeVar1 = abs(sunVisibility - 0.5) * 2.0;
    shadowTimeVar2 = shadowTimeVar1 * shadowTimeVar1;
    shadowTime = shadowTimeVar2 * shadowTimeVar2;
    noonClearLightColor = vec3(0.4, 0.75, 1.3);
    noonClearAmbientColor = pow(skyColor, vec3(0.75)) * 0.85;
    sunsetClearLightColor = pow(vec3(0.62, 0.39, 0.24), vec3(1.5 + invNoonFactor)) * 6.8;
    sunsetClearAmbientColor = noonClearAmbientColor * vec3(1.21, 0.92, 0.76) * 0.95;
    nightClearLightColor = vec3(0.08, 0.12, 0.23);
    nightClearAmbientColor = 0.9 * vec3(0.09, 0.12, 0.17) * (1.55 + vsBrightness * 0.77);
    drlcSnowM = inSnowy * vec3(-0.06, 0.0, 0.04);
    drlcDryM = inDry * vec3(0.01, -0.035, -0.06);
    drlcRainM = vec3(0.0);
    dayRainLightColor = vec3(0.21, 0.16, 0.13) * 0.85 + noonFactor * vec3(0.0, 0.02, 0.06)
                                 + drlcRainM + drlcSnowM + drlcDryM;
    dayRainAmbientColor = vec3(0.2, 0.2, 0.25) * (1.8 + 0.5 * vsBrightness);
    nightRainLightColor = vec3(0.03, 0.035, 0.05) * (0.5 + 0.5 * vsBrightness);
    nightRainAmbientColor = vec3(0.16, 0.20, 0.3) * (0.75 + 0.6 * vsBrightness);
    noonFactorDM = noonFactor * noonFactor;
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
    vec3 color = texelFetch(s_colortex0, texelCoord, 0).rgb;
    float z0 = texelFetch(s_sceneDepth, texelCoord, 0).r;
    float z1 = texelFetch(s_depth, texelCoord, 0).r;

    vec4 screenPos = vec4(texCoord, z0, 1.0);
    vec4 viewPos = gbufferProjectionInverse * (screenPos * 2.0 - 1.0);
    viewPos /= viewPos.w;
    float lViewPos = length(viewPos.xyz);
    vec3 nViewPos = normalize(viewPos.xyz);


    float dither = texture2DLod(s_noise, texCoord * view / 128.0, 0.0).b;
        dither = fract(dither + goldenRatio * mod(float(frameCounter), 3600.0));

    /* TM5723: The "1.0 - translucentMult" trick is done because of the default color attachment
    value being vec3(0.0). This makes it vec3(1.0) to avoid issues especially on improved glass */
    vec3 translucentMult = 1.0 - texelFetch(s_colortex3, texelCoord, 0).rgb; //TM5723
    vec4 volumetricEffect = vec4(0.0);

    vec2 texCoordM = texCoord;

        if (z0 < 1.0) {
            vec4 compositeReflection = texture2D(s_colortex7, texCoord);

            // Partial fix for half resolution WSR-only reflections having a lot of sky gaps

            float fresnelM = pow2(texture2D(s_colortex4, texCoord).a); // including attenuation through fog and clouds


            if (abs(fresnelM - 0.5) < 0.5) { // 0.0 fresnel doesnt need ref calculations, and 1.0 fresnel basically means error
                if (z0 == z1  ||  z0 <= 0.56) { // Solids
                        if (fresnelM > 0.00001) {
                            compositeReflection = sampleBlurFilteredReflection(compositeReflection, nViewPos, dither, z0);

                            compositeReflection.rgb = max(compositeReflection.rgb, vec3(0.0)); // We seem to have some negative values for some reason

                            // This physically doesn't make sense but fits Minecraft
                            const float texturePreservation = 0.7;
                            compositeReflection.rgb = mix(compositeReflection.rgb, max(color, compositeReflection.rgb), texturePreservation);

                            color = mix(color, compositeReflection.rgb, fresnelM);
                        }
                }
            }
        }

    vec4 screenPos1 = vec4(texCoord, z1, 1.0);
    vec4 viewPos1 = gbufferProjectionInverse * (screenPos1 * 2.0 - 1.0);
    viewPos1 /= viewPos1.w;
    float lViewPos1 = length(viewPos1.xyz);

    float z1lod = 1.0;

        float VdotL = dot(nViewPos, lightVec);
        float VdotU = dot(nViewPos, upVec);


        color += GetRainbow(translucentMult, nViewPos, z0, z1, lViewPos, lViewPos1, VdotL, VdotU, dither);

        float vlFactorM = vlFactor;

        volumetricEffect = GetVolumetricLight(vlFactorM, translucentMult, lViewPos, lViewPos1, nViewPos, VdotL, VdotU, z0, z1, z1lod, dither);




        vec3 lightFog = vec3(0.0);

    if (isEyeInWater == 1) {
        if (z0 == 1.0) color.rgb = waterFogColor;

        vec3 underwaterMult = vec3(0.80, 0.87, 0.97);
        color.rgb *= underwaterMult * 0.85;
        volumetricEffect.rgb *= pow2(underwaterMult * 0.55);

    } else if (isEyeInWater == 2) {
        if (z1 == 1.0) color.rgb = fogColor * 5.0;

        volumetricEffect.rgb *= 0.0;

    }



    color = pow(color, vec3(2.2));

        // We add volumetric effect AFTER the "pow color by 2.2" line to get nicer blending
        color += volumetricEffect.rgb;

        color *= GetBloomFog(lViewPos); // Reminder: Bloom Fog can move between composite1-2-3

    /* DRAWBUFFERS:0 */
    gl_FragData0 = vec4(color, 1.0);

    // supposed to be #if defined 1 && (1 == 1 && 2 >= 1 || defined END)

        /* DRAWBUFFERS:05 */
        gl_FragData1 = vec4(0.0, 0.0, 0.0, vlFactorM);
}
}

// the pack spells matrix math M * v; slang only defines mul():
// cover the pack's combinations so bodies stay untouched.
float2 operator*(float2x2 m, float2 v) { return mul(m, v); }
float2 operator*(float2 v, float2x2 m) { return mul(v, m); }
float3 operator*(float3x3 m, float3 v) { return mul(m, v); }
float3 operator*(float3 v, float3x3 m) { return mul(v, m); }
float4 operator*(float4x4 m, float4 v) { return mul(m, v); }
float4 operator*(float4 v, float4x4 m) { return mul(v, m); }
float2x2 operator*(float2x2 a, float2x2 b) { return mul(a, b); }
float3x3 operator*(float3x3 a, float3x3 b) { return mul(a, b); }
float4x4 operator*(float4x4 a, float4x4 b) { return mul(a, b); }

// manual depth compare keeps the pack's own PCF taps; works with the
// existing non-comparison depth samplers until an RHI compare sampler exists.
float4 javaShadow2D(Texture2D t, SamplerState s, float3 uvw)
{ if (u_shadowInfo.w <= 0.0 || any(uvw.xyz < 0.0) || any(uvw.xyz > 1.0)) return float4(1.0); return float4(step(uvw.z, t.Sample(s, uvw.xy).r)); }
float4 javaShadow2D(Texture2D t, SamplerState s, float4 uvw)
{ if (u_shadowInfo.w <= 0.0 || any(uvw.xyz < 0.0) || any(uvw.xyz > 1.0)) return float4(1.0); return float4(step(uvw.z, t.Sample(s, uvw.xy).r)); }
float javaShadow2DProj(Texture2D t, SamplerState s, float4 uvw)
{ float w = max(uvw.w, 1e-6); return step(uvw.z / w, t.Sample(s, uvw.xy / w).r); }
float javaTextureProj(Texture2D t, SamplerState s, float4 uvw)
{ float w = max(uvw.w, 1e-6); return t.Sample(s, uvw.xy / w).r; }
int2 javaTextureSize(Texture2D t, int lod) { uint w = 0, h = 0, n = 0; t.GetDimensions(uint(lod), w, h, n); return int2(w, h); }

// engine light bytes are 0..15 levels; the pack wants 0..1
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
    output.v_vlFactor = vs_stage::vlFactor;
    return output;
}

[shader("fragment")]
FsOut fsMain(FsIn input)
{
    fs_stage::gl_FragCoord = input.fragCoord;
    fs_stage::texCoord = input.v_texCoord;
    fs_stage::upVec = input.v_upVec;
    fs_stage::sunVec = input.v_sunVec;
    fs_stage::vlFactor = input.v_vlFactor;
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
    output.t1 = fs_stage::gl_FragData1;
    return output;
}
