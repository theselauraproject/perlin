// Converted from Java shaderpack program gbuffers_terrain (world0).
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
    float3x3 u_jNormalMatrix; \
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
    [[vk::location(5)]] nointerpolation float2 v_midCoord : TEXCOORD5;
    [[vk::location(6)]] float3 v_normal : TEXCOORD6;
    [[vk::location(7)]] float3 v_vertexPos : TEXCOORD7;
    [[vk::location(8)]] float4 v_glColorRaw : TEXCOORD8;
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
    [[vk::location(5)]] nointerpolation float2 v_midCoord : TEXCOORD5;
    [[vk::location(6)]] float3 v_normal : TEXCOORD6;
    [[vk::location(7)]] float3 v_vertexPos : TEXCOORD7;
    [[vk::location(8)]] float4 v_glColorRaw : TEXCOORD8;
    [[vk::location(9)]] float v_jLayer : TEXCOORD9;
};
struct FsOut
{
    float4 t0 : SV_Target0;
    float4 t1 : SV_Target1;
    float4 t2 : SV_Target2;
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
static float2 midCoord;
static float3 normal;
static float3 vertexPos;
static float4 glColorRaw;
static float jLayer;
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


// @included "/program/gbuffers_terrain.glsl"
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
static vec4 glColor;

//Common Functions//

//Includes//
// @included "/lib/antialiasing/jitter.glsl"
// Jitter offset from Chocapic13
static vec2 jitterOffsets[8];

vec2 TAAJitter(vec2 coord, float w) {
        vec2 offset = jitterOffsets[int(framemod8)] * (w / vec2(viewWidth, viewHeight));
            offset *= 0.125;
        return coord + offset;
}

// @included "/lib/materials/materialMethods/wavingBlocks.glsl"

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

    wave *= 1.0 - inSnowy; // Leaves with snow on top look wrong

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

            if (mat == 10005 // Grounded Foliage
            ) {
                if (vec4(a_texcoord0, 0.0, 1.0).y < a_mcMidTexCoord.y  ||  fract(worldPos.y + 0.21) > 0.26)
                DoWave_Foliage(playerPos.xyz, worldPos, 1.0);
            }

            else if (mat == 10021) { // Upper Layer Foliage
                DoWave_Foliage(playerPos.xyz, worldPos, 1.0);
            }


                else

            if (mat == 10009) { // Leaves
                DoWave_Leaves(playerPos.xyz, worldPos, 1.0);
            } else if (mat == 11009) { // Vine, Pale Hanging Moss
                // Reduced waving on vines to prevent clipping through blocks
                DoWave_Leaves(playerPos.xyz, worldPos, 0.75);
            }

                else

            if (mat == 10068) { // Lava
                DoWave_Lava(playerPos.xyz, worldPos);

                    // G8FL735 Fixes Optifine-Iris parity. Optifine has 0.9 a_color0.rgb on a lot of versions
                    glColorRaw.rgb = min(glColorRaw.rgb, vec3(0.9));
            }



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
    cloudHeightM = isnan(cloudHeight) ? 192.0 : cloudHeight;
    cloudHeightOffset = cloudHeightM - 192.0;
    cloudAlt1i = int(192 + cloudHeightOffset);
    cloudAlt2i = int(288 + cloudHeightOffset);
    glColor = vec4(1.0);
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

    glColorRaw = a_color0;
    if (glColorRaw.a < 0.1) glColorRaw.a = 1.0;
    glColor = glColorRaw;

    normal = normalize(u_jNormalMatrix * a_normal);

    midCoord = (u_jTextureMatrix0 * a_mcMidTexCoord).xy;
    vec2 texMinMidCoord = texCoord - midCoord;
    signMidCoordPos = sign(texMinMidCoord);
    absMidCoordPos  = abs(texMinMidCoord);

    mat = int(a_mcEntity.x + 0.5);

    vec4 position = mul(gbufferModelViewInverse, u_jModelViewMatrix) * vec4(a_position, 1.0);
    vertexPos = position.xyz;

        DoWave(position.xyz, mat);

    gl_Position = mul(u_jProjectionMatrix, gbufferModelView) * position;

        if (mat == 10257) gl_Position.z -= 0.00001; // Iron Bars

        gl_Position.xy = TAAJitter(gl_Position.xy, gl_Position.w);




}
}

namespace fs_stage
{
static float4 gl_FragCoord;
static float4 gl_FragData0;
static float4 gl_FragData1;
static float4 gl_FragData2;
static int mat;
static float2 texCoord;
static float2 lmCoord;
static float2 signMidCoordPos;
static float2 absMidCoordPos;
static float2 midCoord;
static float3 normal;
static float3 vertexPos;
static float4 glColorRaw;
static float jLayer;
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


// @included "/program/gbuffers_terrain.glsl"
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









//Pipeline Constants//

//Common Variables//
static vec3 upVec;
static vec3 eastVec;
static vec3 northVec;

static vec3 sunVec;

static float NdotU;
static float geoNdotU;
static float NdotUmax0;
static float SdotU;
static float sunFactor;
static float sunVisibility;
static float sunVisibility2;
static float shadowTimeVar1;
static float shadowTimeVar2;
static float shadowTime;

static vec4 glColor;

static vec3 lightVec;


//Common Functions//
void DoFoliageColorTweaks(inout vec3 color, inout vec3 shadowMult, inout float snowMinNdotU, vec3 viewPos, vec3 nViewPos, float lViewPos, float dither) {
    float factor = max(80.0 - lViewPos, 0.0);
    shadowMult *= 1.0 + 0.004 * noonFactor * factor;

        color.rgb *= 0.97 - 0.2 * signMidCoordPos.x;

    //#define FOLIAGE_ALT_SUBSURFACE


}

void DoBrightBlockTweaks(vec3 color, float minLight, inout vec3 shadowMult, inout float highlightMult) {
    float factor = mix(minLight * 0.5 + 0.5, 1.0, pow2(pow2(color.r)));
    shadowMult = vec3(factor);
    highlightMult /= factor;
}

void DoOceanBlockTweaks(inout float smoothnessD) {
    smoothnessD *= max0(lmCoord.y - 0.95) * 20.0;
}

//Includes//
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
// @included "/lib/lighting/mainLighting.glsl"
//Lighting Includes//
// @included "/lib/colors/lightAndAmbientColors.glsl"

static vec3 noonClearLightColor; //ground and cloud color
static vec3 noonClearAmbientColor;

static vec3 sunsetClearLightColor; //ground and cloud color
static vec3 sunsetClearAmbientColor;

static vec3 nightClearLightColor; //ground color
static vec3 nightClearAmbientColor;

static vec3 drlcSnowM;
static vec3 drlcDryM;
static vec3 drlcRainM;
static vec3 dayRainLightColor;
static vec3 dayRainAmbientColor;

static vec3 nightRainLightColor;
static vec3 nightRainAmbientColor;

static float noonFactorDM; //ground and cloud factor
static vec3 dayLightColor;
static vec3 dayAmbientColor;

static vec3 clearLightColor;
static vec3 clearAmbientColor;

static float rainShadowVisReduce;

static vec3 rainLightColor;
static vec3 rainAmbientColor;

static vec3 lightColor;
static vec3 ambientColor;

// @included "/lib/lighting/ggx.glsl"
// GGX area light approximation from Horizon Zero Dawn
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
// @included "/lib/lighting/minimumLighting.glsl"
vec3 GetMinimumLighting(float lightmapYM) {
        vec3 minLighting = vec3(0.005625 + vsBrightness * 0.043);
        minLighting *= vec3(0.45, 0.475, 0.6);
        minLighting *= 1.0 - lightmapYM;

    minLighting += nightVisionWithAddedSupport * vec3(0.5, 0.5, 0.75);

    return max(minLighting, vec3(0.0));
}

// @included "/lib/lighting/shadowSampling.glsl"
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

        float colorMult = 2.5 + 5.5 * pow1_5(lightmapY) + 2.0 * lightmapY2; // 423HDSS: Shadow color strength is stored 10 times lower to allow for water shadows going above 1.0
        float colorPow = mix(1.5 + 0.5 * float(isEyeInWater == 0), 0.5, lightmapY4);


            vec3 shadow = SampleTAAFilteredShadow(shadowPos, offset, shadowSamples, leaves, colorMult, colorPow);

        return shadow;
    }

// @included "/lib/lighting/heldLighting.glsl"
vec3 GetHeldLighting(vec3 playerPos, vec3 color, float emission) {
    float heldLight = heldBlockLightValue; float heldLight2 = heldBlockLightValue2;

        if (heldLight > 15.1) heldLight = 0.0;
        if (heldLight2 > 15.1) heldLight2 = 0.0;

        vec3 heldLightCol = blocklightCol; vec3 heldLightCol2 = blocklightCol;

        if (heldItemId == 45032) heldLight = 15; if (heldItemId2 == 45032) heldLight2 = 15; // Lava Bucket

    vec3 playerPosLightM = playerPos + relativeEyePosition;
         playerPosLightM.y += 0.7;
    float lViewPosL = length(playerPosLightM) + 6.0;

    heldLight = pow2(pow2(heldLight * 0.47 / lViewPosL));
    heldLight2 = pow2(pow2(heldLight2 * 0.47 / lViewPosL));

    vec3 heldLighting = pow2(heldLight * DoLuminanceCorrection(heldLightCol))
                        + pow2(heldLight2 * DoLuminanceCorrection(heldLightCol2));


    return heldLighting;
}



// @included "/lib/colors/moonPhaseInfluence.glsl"

static float moonPhaseFactor;

static float halfCheck;
static float firstHalf;
static float secondHalf;

static float moonPhaseFactor2;
static float moonPhaseInfluence;



static vec3 highlightColor;

//Lighting//
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


    // Shadows
        float NdotLmax0 = max0(NdotL);
        float NdotLM = NdotLmax0 * 0.9999;

                if (subsurfaceMode != 0) {
                    NdotLM = 1.0;
                }
                    else
                NdotLM = max0(NdotL + 0.4) * 0.714;



        if (shadowMult.r > 0.00001) {
                if (NdotLM > 0.0001) {
                    vec3 shadowMultBeforeLighting = shadowMult;
                    float shadowLength = shadowDistance * 0.9166667 - lViewPos; //consistent08JJ622

                    if (shadowLength > 0.000001) {
                            float offset = 0.00098;

                        vec3 playerPosM = playerPos;
                        vec3 centerPlayerPos = floor(playerPos + cameraPosition + worldGeoNormal * 0.01) - cameraPosition + 0.5;


                        // Fix light leaking in caves //
                            if (centerShadowBias  ||  subsurfaceMode == 1) {
                                    playerPosM = mix(centerPlayerPos, playerPosM, 0.5 + 0.5 * lightmapYM);
                            } else {
                                float centerFactor = max(glColor.a, lightmapYM);

                                    // Fake Variable Penumbra Shadows
                                    // Making centerFactor also work in daylight if AO gradient is facing towards sun
                                    if (geoNdotU > 0.99) {
                                        float dFdxGLCA = dFdx(glColor.a);
                                        float dFdyGLCA = dFdy(glColor.a);

                                        if (abs(dFdxGLCA) + abs(dFdyGLCA) > 0.00001) {
                                            vec3 aoGradView = dFdxGLCA * normalize(dFdx(playerPos.xyz))
                                                            + dFdyGLCA * normalize(dFdy(playerPos.xyz));
                                            if (dot(normalize(aoGradView.xz), normalize(ViewToPlayer(lightVec).xz)) < 0.3 + 0.4 * dither)
                                                if (dot(lightVec, upVec) < 0.99999) centerFactor = sqrt1(max0(glColor.a - 0.55) / 0.45);
                                        }
                                    }

                                playerPosM = mix(playerPosM, centerPlayerPos, 0.2 * (1.0 - pow2(pow2(centerFactor))));
                            }

                        // Shadow bias without peter-panning //
                                if (subsurfaceMode != 1)
                            {
                                float distanceBias = pow(dot(playerPos, playerPos), 0.75);
                                vec3 bias = worldGeoNormal * (0.12 + 0.0008 * distanceBias) * (2.0 - 0.95 * NdotLmax0); // 0.95 fixes pink petals noon shadows

                                    // WS752GH42G: "Water shadow outside water" fix. This calculation needs to be similar (but not the exact same) as water up-bias
                                    bias.y += lightmapYM * (1.0 - abs(geoNdotU)) * 0.015 * max0(lViewPos - 40.0);

                                    if (subsurfaceMode == 2) {
                                        bias *= vec3(0.0, 0.0, -0.5);
                                        bias.z += 0.25 * signMidCoordPos.x * NdotE;
                                    }

                                playerPosM += bias;
                            }

                        vec3 shadowPos = GetShadowPos(playerPosM);

                        bool leaves = false;
                            if (subsurfaceMode == 0) {
                                    offset *= 1.0 + pow2(absNdotN);
                            } else {
                                float VdotL = dot(nViewPos, lightVec);
                                float lightFactor = pow(max(VdotL, 0.0), 10.0) * float(isEyeInWater == 0);
                                if (subsurfaceMode == 1) {
                                    offset = 0.0005235 * lightmapYM + 0.0009765;
                                    shadowPos.z -= max(NdotL * 0.0001, 0.0) * lightmapYM;
                                    subsurfaceHighlight = lightFactor * 0.8;
                                } else if (subsurfaceMode == 2) {
                                    leaves = true;
                                    offset = 0.0005235 * lightmapYM + 0.0009765;
                                    shadowPos.z -= 0.000175 * lightmapYM;
                                    subsurfaceHighlight = lightFactor * 0.6;
                                } else {

                                }
                            }

                        int shadowSampleBooster = int(subsurfaceMode > 0 && lViewPos < 10.0);
                            int shadowSamples = 2 + 2 * shadowSampleBooster;

                        shadowMult *= GetShadow(shadowPos, s_lightmap.y, offset, shadowSamples, leaves, playerPos);
                    }

                    float shadowSmooth = 16.0;
                    if (shadowLength < shadowSmooth) {
                        float shadowMixer = max0(shadowLength / shadowSmooth);

                            if (subsurfaceMode != 0) {
                                float shadowMixerM = pow2(shadowMixer);

                                if (subsurfaceMode == 1) skyLightShadowMult *= mix(0.6 + 0.3 * pow2(noonFactor), 1.0, shadowMixerM);
                                else skyLightShadowMult *= mix(NdotL * 0.4999 + 0.5, 1.0, shadowMixerM);

                                subsurfaceHighlight *= shadowMixer;
                            }

                        shadowMult = mix(vec3(skyLightShadowMult * shadowMultBeforeLighting), shadowMult, shadowMixer);
                    }
                }


            shadowMult *= max(NdotLM * shadowTime, 0.0);
        }

    // Blocklight
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
    emission *= 1.0 - 0.25 * daylightFactor; // Less emission under direct skylight



    vec3 blockLighting = lightmapXM * blocklightCol;


            vec3 playerPosForHeldLighting = playerPos;

        vec3 heldLighting = GetHeldLighting(playerPosForHeldLighting, color.rgb, emission);


    vec3 minLighting = GetMinimumLighting(lightmapYM);
    vec3 shadowLightMult = shadowMult;
    float shadowMultFloat = min1(GetLuminance(shadowMult));

    // Lighting Tweaks
        ambientMult = mix(lightmapYM, pow2(lightmapYM) * lightmapYM, rainFactor);



        if (isEyeInWater != 1) {
            float lxFactor = (sunVisibility2 * 0.4 + (0.6 - 0.6 * invNoonFactor2)) * (6.0 - 5.0 * rainFactor);
            lxFactor *= lightmapY2 + lightmapY2 * 2.0 * pow2(shadowMultFloat);
            lxFactor = max0(lxFactor - emission * 1000000.0);
            blockLighting *= pow(lightmapXM / 60.0 + 0.001, 0.09 * lxFactor);

            // Less light in the distance / more light closer to the camera during rain or night to simulate thicker fog
            float rainLF = 0.1 * rainFactor;
            float lightFogTweaks = 1.0 + max0(96.0 - lViewPos) * (0.002 * (1.0 - sunVisibility2) + 0.0104 * rainLF) - rainLF;
            ambientMult *= lightFogTweaks;
            lightColorM *= lightFogTweaks;
        }


    // Directional Shading
    float directionShade = 1.0;
        if (!noDirectionalShading) {
            float absNdotE2 = pow2(absNdotE);

                float NdotUM = 0.75 + NdotU * 0.25;
            float NdotNM = 1.0 + 0.075 * absNdotN;
            float NdotEM = 1.0 - 0.1 * absNdotE2;
            directionShade = NdotUM * NdotEM * NdotNM;

                lightColorM *= 1.0 + absNdotE2 * 0.75;


                // Fake bounced light
                ambientColorM = mix(ambientColorM, lightColorM, (0.05 + 0.03 * subsurfaceMode) * absNdotN * lightmapY2);

                // Get a bit more natural looking lighting during noon
                lightColorM *= 1.0 + max0(1.0 - subsurfaceMode) * pow(noonFactor, 20.0) * (pow2(absNdotN) * 0.8 - absNdotE2 * 0.2);
        }

    // Scene Lighting Stuff
    vec3 sceneLighting = lightColorM * shadowLightMult + ambientColorM * ambientMult;
    float dotSceneLighting = dot(sceneLighting, sceneLighting);

        blockLighting = sqrt(pow2(blockLighting) + heldLighting);

    blockLighting *= 1.00;


    // Vanilla Ambient Occlusion
    float vanillaAO = 1.0;
        vanillaAO = glColor.a;


        if (subsurfaceMode != 0) vanillaAO = mix(min1(vanillaAO * 1.15), 1.0, shadowMultFloat);
        else if (!noVanillaAO) {
                vanillaAO = min1(vanillaAO + 0.08);
                    vanillaAO = pow(
                        pow1_5(vanillaAO),
                        1.0 + dotSceneLighting * 0.02 + NdotUmax0 * (0.15 + 0.25 * pow2(noonFactor * pow2(lightmapY2)))
                    );
            vanillaAO = vanillaAO * 0.9 + 0.1;

        }

    // Light Highlight
    vec3 lightHighlight = vec3(0.0);
        float specularHighlight = GGX(normalM, nViewPos, lightVec, NdotLmax0, smoothnessG);

        specularHighlight *= highlightMult;

        lightHighlight = isEyeInWater != 1 ? shadowMult : pow(shadowMult, vec3(0.25)) * 0.35;
        lightHighlight *= (subsurfaceHighlight + specularHighlight) * highlightColor;

            lightHighlight *= pow2(moonPhaseInfluence);

    // Mix Colors
    vec3 finalDiffuse = pow2(directionShade * vanillaAO) * (blockLighting + pow2(sceneLighting) + minLighting) + pow2(emission);
    finalDiffuse = sqrt(max(finalDiffuse, vec3(0.0))); // sqrt() for a bit more realistic light mix, max() to prevent NaNs

    // Apply Lighting
    color.rgb *= finalDiffuse;
    color.rgb += lightHighlight;
    color.rgb *= pow2(1.0 - darknessLightFactor);
}
// @included "/lib/util/dither.glsl"

    // Thanks to Jessie for dithering
    float Bayer2  (vec2 c) { c = 0.5 * floor(c); return fract(1.5 * fract(c.y) + c.x); }
    float Bayer4  (vec2 c) { return 0.25 * Bayer2  (0.5 * c) + Bayer2(c); }
    float Bayer8  (vec2 c) { return 0.25 * Bayer4  (0.5 * c) + Bayer2(c); }
    float Bayer16 (vec2 c) { return 0.25 * Bayer8  (0.5 * c) + Bayer2(c); }
    float Bayer32 (vec2 c) { return 0.25 * Bayer16 (0.5 * c) + Bayer2(c); }
    float Bayer64 (vec2 c) { return 0.25 * Bayer32 (0.5 * c) + Bayer2(c); }
    float Bayer128(vec2 c) { return 0.25 * Bayer64 (0.5 * c) + Bayer2(c); }
    float Bayer256(vec2 c) { return 0.25 * Bayer128(0.5 * c) + Bayer2(c); }

// @included "/lib/antialiasing/jitter.glsl"
// Jitter offset from Chocapic13
static vec2 jitterOffsets[8];

vec2 TAAJitter(vec2 coord, float w) {
        vec2 offset = jitterOffsets[int(framemod8)] * (w / vec2(viewWidth, viewHeight));
            offset *= 0.125;
        return coord + offset;
}

// @included "/lib/util/miplevel.glsl"
static vec2 midCoordPos;

// @included "/lib/util/dFdxdFdy.glsl"

static vec2 dcdx;
static vec2 dcdy;

static vec2 mipx;
static vec2 mipy;

static float mipDelta;
static float miplevel;

static vec2 atlasSizeM;









// @included "/lib/misc/distantLightBokeh.glsl"
float GetDistantLightBokehMix(float lViewPos) {
    //if (heldItemId == 40000 || heldItemId2 == 40000) return 0.0; // Hold spider eye to disable;
    return clamp01(0.005 * (lViewPos - 60.0));
}

    float GetDistantLightBokehMixMipmapped(float lViewPos) {
        float dlbMix = GetDistantLightBokehMix(lViewPos);
            dlbMix *= min1(miplevel * 0.4);
        return dlbMix;
    }

    void DoDistantLightBokehMaterial(inout vec4 color, vec4 distantColor, inout float emission, float distantEmission, float lViewPos) {
        float dlbMix = GetDistantLightBokehMixMipmapped(lViewPos);
        color = mix(color, distantColor, dlbMix);
        emission = mix(emission, distantEmission, dlbMix);
    }
    void DoDistantLightBokehMaterial(inout float emission, float distantEmission, float lViewPos) {
        float dlbMix = GetDistantLightBokehMixMipmapped(lViewPos);
        emission = mix(emission, distantEmission, dlbMix);
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
    upVec = normalize(transpose(gbufferModelView)[1].xyz);
    eastVec = normalize(transpose(gbufferModelView)[0].xyz);
    northVec = normalize(transpose(gbufferModelView)[2].xyz);
    sunVec = GetSunVector();
    NdotU = dot(normal, upVec);
    geoNdotU = NdotU;
    NdotUmax0 = max(NdotU, 0.0);
    SdotU = dot(sunVec, upVec);
    sunFactor = SdotU < 0.0 ? clamp(SdotU + 0.375, 0.0, 0.75) / 0.75 : clamp(SdotU + 0.03125, 0.0, 0.0625) / 0.0625;
    sunVisibility = clamp(SdotU + 0.0625, 0.0, 0.125) / 0.125;
    sunVisibility2 = sunVisibility * sunVisibility;
    shadowTimeVar1 = abs(sunVisibility - 0.5) * 2.0;
    shadowTimeVar2 = shadowTimeVar1 * shadowTimeVar1;
    shadowTime = shadowTimeVar2 * shadowTimeVar2;
    glColor = glColorRaw;
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
    midCoordPos = absMidCoordPos * signMidCoordPos;
    dcdx = dFdx(texCoord.xy);
    dcdy = dFdy(texCoord.xy);
    mipx = dcdx / absMidCoordPos * 8.0;
    mipy = dcdy / absMidCoordPos * 8.0;
    mipDelta = max(dot(mipx, mipx), dot(mipy, mipy));
    miplevel = max(0.5 * log2(mipDelta), 0.0);
    atlasSizeM = atlasSize;
        vec4 color = perlinAtlas(float3(texCoord, jLayer));

    float smoothnessD = 0.0, materialMask = 0.0;

        if (color.a <= 0.5) discard;

    vec3 colorP = color.rgb;

        color.rgb *= glColor.rgb;

    vec3 screenPos = vec3(gl_FragCoord.xy / vec2(viewWidth, viewHeight), gl_FragCoord.z);
        vec3 viewPos = ScreenToView(vec3(TAAJitter(screenPos.xy, -0.5), screenPos.z));
    float lViewPos = length(viewPos);
    vec3 nViewPos = normalize(viewPos);
    vec3 playerPos = vertexPos;

    float dither = Bayer64(gl_FragCoord.xy);
        dither = fract(dither + goldenRatio * mod(float(frameCounter), 3600.0));

    int subsurfaceMode = 0;
    bool noSmoothLighting = false, noDirectionalShading = false, noVanillaAO = false, centerShadowBias = false, noGeneratedNormals = false, doTileRandomisation = true;
    float smoothnessG = 0.0, highlightMult = 1.0, emission = 0.0, noiseFactor = 1.0, snowFactor = 1.0, snowMinNdotU = 0.0, noPuddles = 0.0;
    vec2 lmCoordM = lmCoord;
    vec3 normalM = normal, geoNormal = normal, shadowMult = vec3(1.0);
    vec3 worldGeoNormal = normalize(ViewToPlayer(geoNormal * 10000.0));

// @included "/lib/materials/materialHandling/terrainMaterials.glsl"

    vec3 maRecolor = vec3(0.0);
// @included "/lib/materials/materialHandling/terrainIPBR.glsl"
if (mat >= 10000) {
if (mat < 11024) {
    if (mat < 10512) {
        if (mat < 10256) {
            if (mat < 10128) {
                if (mat < 10064) {
                    if (mat < 10032) {
                        if (mat < 10016) {
                            if (mat < 10008) {
                                if (mat < 10004) { // No directional shading
                                    noDirectionalShading = true;
                                }
                                else /*if (mat < 10008)*/ { // Grounded Waving Foliage
                                    subsurfaceMode = 1, noSmoothLighting = true, noDirectionalShading = true;

                                        DoFoliageColorTweaks(color.rgb, shadowMult, snowMinNdotU, viewPos, nViewPos, lViewPos, dither);


                                }
                            } else {
                                if (mat < 10012) { // Leaves
// @included "/lib/materials/specificMaterials/terrain/leaves.glsl"
subsurfaceMode = 2;

    materialMask = OSIEBCA * 253.0; // Reduced Edge 1 (Leaves)


    float factor = min1(pow2(color.g - 0.15 * (color.r + color.b)) * 2.5);
    smoothnessG = factor * 0.4;
    highlightMult = factor * 4.0 + 2.0;
        float fresnel = clamp(1.0 + dot(normalM, normalize(viewPos)), 0.0, 1.0);
        highlightMult *= 1.0 - pow2(pow2(fresnel));


                                }
                                else /*if (mat < 10016)*/ { //

                                }
                            }
                        } else {
                            if (mat < 10024) {
                                if (mat < 10020) { // Non-waving Foliage
                                    subsurfaceMode = 1, noSmoothLighting = true, noDirectionalShading = true;
                                }
                                else /*if (mat < 10024)*/ { // Upper Waving Foliage
                                    subsurfaceMode = 1, noSmoothLighting = true, noDirectionalShading = true;

                                        DoFoliageColorTweaks(color.rgb, shadowMult, snowMinNdotU, viewPos, nViewPos, lViewPos, dither);


                                }
                            } else {
                                if (mat < 10028) { // Modded Ores
                                }
                                else /*if (mat < 10032)*/ { // Modded Light Sources
                                    noSmoothLighting = true; noDirectionalShading = true;
                                    emission = GetLuminance(color.rgb) * 2.5;
                                }
                            }
                        }
                    } else {
                        if (mat < 10048) {
                            if (mat < 10040) {
                                if (mat < 10036) { // Stone Bricks++
// @included "/lib/materials/specificMaterials/terrain/stone.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;


                                }
                                else /*if (mat < 10040)*/ { // Anvil+
// @included "/lib/materials/specificMaterials/terrain/anvil.glsl"
smoothnessG = color.r;
smoothnessD = color.r;

                                }
                            } else {
                                if (mat < 10044) { // Rails
                                    color = perlinAtlasLod(float3(texCoord, jLayer), 0);

                                    noSmoothLighting = true;
                                    if (color.r > 0.1  &&  color.g + color.b < 0.1) { // Redstone Parts
                                        noSmoothLighting = true; noDirectionalShading = true;
                                        lmCoordM.x = min(lmCoordM.x * 0.9, 0.77);

                                        if (color.r > 0.5) {
                                            color.rgb *= color.rgb;
                                            emission = 8.0 * color.r;
                                        } else if (color.r > color.g * 2.0) {
                                            materialMask = OSIEBCA * 5.0; // Redstone Fresnel

                                            float factor = pow2(color.r);
                                            smoothnessG = 0.4;
                                            highlightMult = factor + 0.4;

                                            smoothnessD = factor * 0.7 + 0.3;
                                        }
                                    } else if (abs(color.r - color.b) < 0.15) { // Iron Parts
// @included "/lib/materials/specificMaterials/terrain/ironBlock.glsl"
    smoothnessG = pow2(pow2(color.r));
highlightMult = smoothnessG * 3.0;
smoothnessD = smoothnessG;
materialMask = OSIEBCA; // Intense Fresnel

color.rgb *= 0.6 + 0.5 * GetLuminance(color.rgb);

                                    } else if (color.g > color.b * 2.0) { // Gold Parts
// @included "/lib/materials/specificMaterials/terrain/goldBlock.glsl"
materialMask = OSIEBCA * 3.0; // Gold Fresnel

    float colorG2 = pow2(color.g);
float colorG4 = pow2(colorG2);
float factor = max(color.g, 0.8);

smoothnessG = min1(factor - colorG4 * 0.5);
highlightMult = 3.5 * max(colorG4, 0.2);

smoothnessD = colorG4;

color.rgb *= 0.5 + 0.4 * GetLuminance(color.rgb);

                                    } else { // Wood Parts
// @included "/lib/materials/specificMaterials/planks/oakPlanks.glsl"
smoothnessG = pow2(dot(color.rgb, color.rgb)) * 0.105;
smoothnessD = smoothnessG;

                                    }
                                }
                                else /*if (mat < 10048)*/ { // Empty Cauldron, Hopper
                                    noSmoothLighting = true;
                                    lmCoordM.x = min(lmCoordM.x, 0.9333);

// @included "/lib/materials/specificMaterials/terrain/anvil.glsl"
smoothnessG = color.r;
smoothnessD = color.r;

                                }
                            }
                        } else {
                            if (mat < 10056) {
                                if (mat < 10052) { // Water Cauldron
                                    noSmoothLighting = true;
                                    lmCoordM.x = min(lmCoordM.x, 0.9333);

                                    vec3 worldPos = playerPos + cameraPosition;
                                    vec3 fractPos = fract(worldPos.xyz);
                                    vec2 coordM = abs(fractPos.xz - 0.5);
                                    bool cauldronInteriorCheck = max(coordM.x, coordM.y) < 0.375 && fractPos.y > 0.3;
                                    if (cauldronInteriorCheck  &&  NdotU > 0.9) {
                                            vec3 colorP = color.rgb / glColor.rgb;
                                            smoothnessG = min(pow2(pow2(dot(colorP.rgb, colorP.rgb) * 0.4)), 1.0);
                                            highlightMult = 3.25;
                                            smoothnessD = 0.8;

// @included "/lib/materials/specificMaterials/translucents/water.glsl"
// ============================== Step 1: Color Prep ============================== //
        color.rgb = mix(color.rgb, vec3(GetLuminance(color.rgb)), 0.88);
        color.rgb = pow2(color.rgb) * vec3(2.3, 3.5, 3.1) * 0.9;

// ============================== End of Step 1 ============================== //



                                    } else {
// @included "/lib/materials/specificMaterials/terrain/anvil.glsl"
smoothnessG = color.r;
smoothnessD = color.r;

                                    }
                                }
                                else /*if (mat < 10056)*/ { // Powder Snow Cauldron
                                    noSmoothLighting = true;
                                    lmCoordM.x = min(lmCoordM.x, 0.9333);

                                    vec3 worldPos = playerPos + cameraPosition;
                                    vec3 fractPos = fract(worldPos.xyz);
                                    vec2 coordM = abs(fractPos.xz - 0.5);
                                    bool cauldronInteriorCheck = max(coordM.x, coordM.y) < 0.375 && fractPos.y > 0.3;
                                    if (cauldronInteriorCheck  &&  NdotU > 0.9) {

// @included "/lib/materials/specificMaterials/terrain/snow.glsl"
smoothnessG = (1.0 - pow(color.g, 64.0) * 0.3) * 0.4;
highlightMult = 2.0;

smoothnessD = smoothnessG;

    DoBrightBlockTweaks(color.rgb, 0.5, shadowMult, highlightMult);

                                    } else {
// @included "/lib/materials/specificMaterials/terrain/anvil.glsl"
smoothnessG = color.r;
smoothnessD = color.r;

                                    }
                                }
                            } else {
                                if (mat < 10060) { // Lava Cauldron
                                    noSmoothLighting = true;
                                    lmCoordM.x = min(lmCoordM.x, 0.9333);

                                    vec3 worldPos = playerPos + cameraPosition;
                                    vec3 fractPos = fract(worldPos.xyz);
                                    vec2 coordM = abs(fractPos.xz - 0.5);
                                    bool cauldronInteriorCheck = max(coordM.x, coordM.y) < 0.375 && fractPos.y > 0.3;
                                    if (cauldronInteriorCheck  &&  NdotU > 0.9) {

// @included "/lib/materials/specificMaterials/terrain/lava.glsl"
color.rgb *= 0.84;


noDirectionalShading = true;
lmCoordM = vec2(0.0);
emission = GetLuminance(color.rgb) * 7.48 + 0.5;

maRecolor = vec3(clamp(pow2(pow2(pow2(smoothstep1(emission * 0.28)))), 0.12, 0.4) * 1.3) * vec3(0.25, vec2(0.175));

                                    } else {
// @included "/lib/materials/specificMaterials/terrain/anvil.glsl"
smoothnessG = color.r;
smoothnessD = color.r;

                                    }
                                }
                                else /*if (mat < 10064)*/ { // Lever
                                    if (color.r > color.g + color.b) {
                                        color.rgb *= color.rgb;
                                        emission = 4.0;
                                    } else {
// @included "/lib/materials/specificMaterials/terrain/cobblestone.glsl"
smoothnessG = dot(color.rgb, color.rgb) * 0.17;
smoothnessD = smoothnessG;
smoothnessG = max(smoothnessG, 0.3 * color.g * float(color.g > color.b * 1.5));
                                    }
                                }
                            }
                        }
                    }
                } else {
                    if (mat < 10096) {
                        if (mat < 10080) {
                            if (mat < 10072) {
                                if (mat < 10068) { // Lectern
// @included "/lib/materials/specificMaterials/planks/oakPlanks.glsl"
smoothnessG = pow2(dot(color.rgb, color.rgb)) * 0.105;
smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10072)*/ { // Lava
// @included "/lib/materials/specificMaterials/terrain/lava.glsl"
color.rgb *= 0.84;


noDirectionalShading = true;
lmCoordM = vec2(0.0);
emission = GetLuminance(color.rgb) * 7.48 + 0.5;

maRecolor = vec3(clamp(pow2(pow2(pow2(smoothstep1(emission * 0.28)))), 0.12, 0.4) * 1.3) * vec3(0.25, vec2(0.175));



                                        DoDistantLightBokehMaterial(emission, 4.5, lViewPos);
                                }
                            } else {
                                if (mat < 10076) { // Fire
                                    noSmoothLighting = true, noDirectionalShading = true;
                                    emission = 2.35;
                                    color.rgb *= sqrt1(GetLuminance(color.rgb));
                                }
                                else /*if (mat < 10080)*/ { // Soul Fire
                                    noSmoothLighting = true, noDirectionalShading = true;
                                    emission = 1.5;
                                    color.rgb = pow1_5(color.rgb);
                                }
                            }
                        } else {
                            if (mat < 10088) {
                                if (mat < 10084) { // Stone+, Coal Ore, Smooth Stone+, Grindstone, Stonecutter
// @included "/lib/materials/specificMaterials/terrain/stone.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10088)*/ { // Granite+
                                    smoothnessG = pow2(pow2(color.r)) * 0.75;
                                    smoothnessD = smoothnessG;
                                }
                            } else {
                                if (mat < 10092) { // Diorite+
                                    smoothnessG = pow2(pow2(color.g)) * 0.5;
                                    smoothnessD = smoothnessG;

                                        DoBrightBlockTweaks(color.rgb, 0.75, shadowMult, highlightMult);
                                }
                                else /*if (mat < 10096)*/ { // Andesite+
                                    smoothnessG = pow2(pow2(color.g)) * 1.3;
                                    smoothnessG = min1(smoothnessG);
                                    smoothnessD = smoothnessG;
                                }
                            }
                        }
                    } else {
                        if (mat < 10112) {
                            if (mat < 10104) {
                                if (mat < 10100) { // Polished Granite+
                                    smoothnessG = 0.1 + color.r * 0.4;
                                    smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10104)*/ { // Polished Diorite+
                                    smoothnessG = pow2(color.g) * 0.7;
                                    smoothnessD = smoothnessG;

                                        DoBrightBlockTweaks(color.rgb, 0.75, shadowMult, highlightMult);

                                }
                            } else {
                                if (mat < 10108) { // Polished Andesite+, Packed Mud, Mud Bricks+, Bricks+
                                    smoothnessG = pow2(color.g);
                                    smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10112)*/ { // Deepslate:Non-polished Variants, Deepslate Coal Ore
// @included "/lib/materials/specificMaterials/terrain/deepslate.glsl"
smoothnessG = pow(color.g, 1.5) * 0.55;
smoothnessG = smoothnessG;
smoothnessD = smoothnessG;

materialMask = OSIEBCA; // Intense Fresnel
                                }
                            }
                        } else {
                            if (mat < 10120) {
                                if (mat < 10116) { // Deepslate:Polished Variants, Mud, Mangrove Roots, Muddy Mangrove Roots
// @included "/lib/materials/specificMaterials/terrain/deepslate.glsl"
smoothnessG = pow(color.g, 1.5) * 0.55;
smoothnessG = smoothnessG;
smoothnessD = smoothnessG;

materialMask = OSIEBCA; // Intense Fresnel

                                }
                                else /*if (mat < 10120)*/ { // Calcite
                                    highlightMult = pow2(color.g) + 1.0;
                                    smoothnessG = 1.0 - color.g * 0.5;
                                    smoothnessD = smoothnessG;

                                        DoBrightBlockTweaks(color.rgb, 0.75, shadowMult, highlightMult);
                                }
                            } else {
                                if (mat < 10124) { // Dripstone+, Daylight Detector
                                    smoothnessG = pow2(GetLuminance(color.rgb)) * 0.9 + 0.1;
                                    smoothnessD = smoothnessG;

                                    if (mat == 10123) { // Pointed Dripstone
                                        noDirectionalShading = true;
                                    }

                                }
                                else /*if (mat < 10128)*/ { // Snowy Variants of Grass Block, Podzol, Mycelium
                                    float dotColor = dot(color.rgb, color.rgb);
                                    if (dotColor > 1.5) { // Snowy Variants:Snowy Part
// @included "/lib/materials/specificMaterials/terrain/snow.glsl"
smoothnessG = (1.0 - pow(color.g, 64.0) * 0.3) * 0.4;
highlightMult = 2.0;

smoothnessD = smoothnessG;

    DoBrightBlockTweaks(color.rgb, 0.5, shadowMult, highlightMult);

                                    } else { // Snowy Variants:Dirt Part
// @included "/lib/materials/specificMaterials/terrain/dirt.glsl"
smoothnessG = color.r * 0.1 + 0.1;
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                if (mat < 10192) {
                    if (mat < 10160) {
                        if (mat < 10144) {
                            if (mat < 10136) {
                                if (mat < 10132) { // Dirt, Coarse Dirt, Rooted Dirt, Podzol:Normal, Mycelium:Normal, Farmland:Dry
// @included "/lib/materials/specificMaterials/terrain/dirt.glsl"
smoothnessG = color.r * 0.1 + 0.1;
                                }
                                else /*if (mat < 10136)*/ { // Grass Block:Normal
                                    if (glColor.b < 0.98) { // Grass Block:Normal:Grass Part
                                        smoothnessG = pow2(color.g);

                                    } else { //Grass Block:Normal:Dirt Part
// @included "/lib/materials/specificMaterials/terrain/dirt.glsl"
smoothnessG = color.r * 0.1 + 0.1;

                                    }
                                }
                            } else {
                                if (mat < 10140) { // Farmland:Wet
                                    if (NdotU > 0.99) { // Farmland:Wet:Top Part
                                            smoothnessG = 0.5 * (1.0 + abs(color.r - color.b) + color.b);
                                            smoothnessD = smoothnessG * 0.5;
                                    } else { // Farmland:Wet:Dirt Part
// @included "/lib/materials/specificMaterials/terrain/dirt.glsl"
smoothnessG = color.r * 0.1 + 0.1;
                                    }
                                }
                                else /*if (mat < 10144)*/ { // Netherrack
// @included "/lib/materials/specificMaterials/terrain/netherrack.glsl"
    smoothnessG = color.r * 0.4 + 0.2;
smoothnessD = smoothnessG;
                                }
                            }
                        } else {
                            if (mat < 10152) {
                                if (mat < 10148) { // Warped Nylium, Warped Wart Block
                                    if (color.g == color.b  &&  color.g > 0.0001) { // Warped Nylium:Netherrack Part
// @included "/lib/materials/specificMaterials/terrain/netherrack.glsl"
    smoothnessG = color.r * 0.4 + 0.2;
smoothnessD = smoothnessG;
                                    } else { // Warped Nylium:Nylium Part, Warped Wart Block
                                        smoothnessG = color.g * 0.5;
                                        smoothnessD = smoothnessG;

                                    }
                                }
                                else /*if (mat < 10152)*/ { // Crimson Nylium, Nether Wart Block
                                    if (color.g == color.b  &&  color.g > 0.0001  &&  color.r < 0.522) { // Crimson Nylium:Netherrack Part
// @included "/lib/materials/specificMaterials/terrain/netherrack.glsl"
    smoothnessG = color.r * 0.4 + 0.2;
smoothnessD = smoothnessG;
                                    } else { // Crimson Nylium:Nylium Part, Nether Wart Block
                                        smoothnessG = color.r * 0.5;
                                        smoothnessD = smoothnessG;

                                    }
                                }
                            } else {
                                if (mat < 10156) { // Cobblestone+, Mossy Cobblestone+, Furnace:Unlit, Smoker:Unlit, Blast Furnace:Unlit, Lodestone, Piston, Sticky Piston, Dispenser, Dropper
// @included "/lib/materials/specificMaterials/terrain/cobblestone.glsl"
smoothnessG = dot(color.rgb, color.rgb) * 0.17;
smoothnessD = smoothnessG;
smoothnessG = max(smoothnessG, 0.3 * color.g * float(color.g > color.b * 1.5));
                                }
                                else /*if (mat < 10160)*/ { // Oak Planks++:Clean Variants, Bookshelf, Crafting Table, Tripwire Hook
// @included "/lib/materials/specificMaterials/planks/oakPlanks.glsl"
smoothnessG = pow2(dot(color.rgb, color.rgb)) * 0.105;
smoothnessD = smoothnessG;

                                }
                            }
                        }
                    } else {
                        if (mat < 10176) {
                            if (mat < 10168) {
                                if (mat < 10164) { // Oak Log, Oak Wood
                                    if (color.g > 0.48  || 
                                        CheckForColor(color.rgb, vec3(126, 98, 55))  || 
                                        CheckForColor(color.rgb, vec3(150, 116, 65))) { // Oak Log:Clean Part
// @included "/lib/materials/specificMaterials/planks/oakPlanks.glsl"
smoothnessG = pow2(dot(color.rgb, color.rgb)) * 0.105;
smoothnessD = smoothnessG;

                                    } else { // Oak Log:Wood Part, Oak Wood
// @included "/lib/materials/specificMaterials/terrain/oakWood.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.5;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;
                                    }
                                }
                                else /*if (mat < 10168)*/ { // Spruce Planks++:Clean Variants
// @included "/lib/materials/specificMaterials/planks/sprucePlanks.glsl"
smoothnessG = pow2(dot(color.rgb, vec3(0.3)));
smoothnessD = smoothnessG;

                                }
                            } else {
                                if (mat < 10172) { // Spruce Log, Spruce Wood
                                    if (color.g > 0.25) { // Spruce Log:Clean Part
// @included "/lib/materials/specificMaterials/planks/sprucePlanks.glsl"
smoothnessG = pow2(dot(color.rgb, vec3(0.3)));
smoothnessD = smoothnessG;

                                    } else { // Spruce Log:Wood Part, Spruce Wood
                                        smoothnessG = pow2(color.g) * 2.5;
                                        smoothnessG = min1(smoothnessG);
                                        smoothnessD = smoothnessG;
                                    }
                                }
                                else /*if (mat < 10176)*/ { // Birch Planks++:Clean Variants, Scaffolding, Loom
// @included "/lib/materials/specificMaterials/planks/birchPlanks.glsl"
smoothnessG = pow2(pow2(color.g)) * 0.55;
smoothnessD = smoothnessG;

                                }
                            }
                        } else {
                            if (mat < 10184) {
                                if (mat < 10180) { // Birch Log, Birch Wood
                                    if (color.r - color.b > 0.15) { // Birch Log:Clean Part
// @included "/lib/materials/specificMaterials/planks/birchPlanks.glsl"
smoothnessG = pow2(pow2(color.g)) * 0.55;
smoothnessD = smoothnessG;

                                    } else { // Birch Log:Wood Part, Birch Wood
                                        smoothnessG = pow2(color.g) * 0.25;
                                        smoothnessD = smoothnessG;

                                    }
                                }
                                else /*if (mat < 10184)*/ { // Jungle Planks++:Clean Variants, Composter
// @included "/lib/materials/specificMaterials/planks/junglePlanks.glsl"
smoothnessG = pow2(pow2(pow2(color.g))) * 12.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

                                }
                            } else {
                                if (mat < 10188) { // Jungle Log, Jungle Wood
                                    if (color.g > 0.405) { // Jungle Log:Clean Part
// @included "/lib/materials/specificMaterials/planks/junglePlanks.glsl"
smoothnessG = pow2(pow2(pow2(color.g))) * 12.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

                                    } else { // Jungle Log:Wood Part, Jungle Wood
                                        smoothnessG = pow2(pow2(color.g)) * 5.0;
                                        smoothnessG = min1(smoothnessG);
                                        smoothnessD = smoothnessG;

                                    }
                                }
                                else /*if (mat < 10192)*/ { // Acacia Planks++:Clean Variants
// @included "/lib/materials/specificMaterials/planks/acaciaPlanks.glsl"
smoothnessG = pow2(pow2(color.r)) * 0.45;
smoothnessD = smoothnessG;

                                }
                            }
                        }
                    }
                } else {
                    if (mat < 10224) {
                        if (mat < 10208) {
                            if (mat < 10200) {
                                if (mat < 10196) { // Acacia Log, Acacia Wood
                                    if (color.r - color.b > 0.2) { // Acacia Log:Clean Part
// @included "/lib/materials/specificMaterials/planks/acaciaPlanks.glsl"
smoothnessG = pow2(pow2(color.r)) * 0.45;
smoothnessD = smoothnessG;

                                    } else { // Acacia Log:Wood Part, Acacia Wood
                                        smoothnessG = pow2(color.b) * 1.3;
                                        smoothnessG = min1(smoothnessG);
                                        smoothnessD = smoothnessG;

                                    }
                                }
                                else /*if (mat < 10200)*/ { // Dark Oak Planks++:Clean Variants
// @included "/lib/materials/specificMaterials/planks/darkOakPlanks.glsl"
smoothnessG = dot(color.rgb, vec3(0.2));
smoothnessD = smoothnessG;

                                }
                            } else {
                                if (mat < 10204) { // Dark Oak Log, Dark Oak Wood
                                    if (color.r - color.g > 0.08  || 
                                        CheckForColor(color.rgb, vec3(48, 30, 14))) { // Dark Oak Log:Clean Part
// @included "/lib/materials/specificMaterials/planks/darkOakPlanks.glsl"
smoothnessG = dot(color.rgb, vec3(0.2));
smoothnessD = smoothnessG;

                                    } else { // Dark Oak Log:Wood Part, Dark Oak Wood
                                        smoothnessG = color.r * 0.4;
                                        smoothnessD = smoothnessG;
                                    }
                                }
                                else /*if (mat < 10208)*/ { // Mangrove Planks++:Clean Variants
// @included "/lib/materials/specificMaterials/planks/mangrovePlanks.glsl"
smoothnessG = pow2(color.r) * 0.5;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;
                                }
                            }
                        } else {
                            if (mat < 10216) {
                                if (mat < 10212) { // Mangrove Log, Mangrove Wood
                                    if (color.r - color.g > 0.2) { // Mangrove Log:Clean Part
// @included "/lib/materials/specificMaterials/planks/mangrovePlanks.glsl"
smoothnessG = pow2(color.r) * 0.5;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;
                                    } else { // Mangrove Log:Wood Part, Mangrove Wood
                                        smoothnessG = pow2(color.r) * 0.6;
                                        smoothnessD = smoothnessG;
                                    }
                                }
                                else /*if (mat < 10216)*/ { // Crimson Planks++:Clean Variants
// @included "/lib/materials/specificMaterials/planks/crimsonPlanks.glsl"
smoothnessG = pow2(color.r) * 0.5;
smoothnessD = smoothnessG;

                                }
                            } else {
                                if (mat < 10220) { // Crimson Stem, Crimson Hyphae
                                    if (color.r / color.b > 2.5) { // Emissive Part
                                        emission = pow2(color.r) * 6.5;
                                        color.gb *= 0.5;
                                    } else { // Flat Part
// @included "/lib/materials/specificMaterials/planks/crimsonPlanks.glsl"
smoothnessG = pow2(color.r) * 0.5;
smoothnessD = smoothnessG;

                                    }
                                }
                                else /*if (mat < 10224)*/ { // Warped Planks++:Clean Variants
// @included "/lib/materials/specificMaterials/planks/warpedPlanks.glsl"
smoothnessG = pow2(color.g) * 0.5;
smoothnessD = smoothnessG;

                                }
                            }
                        }
                    } else {
                        if (mat < 10240) {
                            if (mat < 10232) {
                                if (mat < 10228) { // Warped Stem, Warped Hyphae
                                    //if (color.r < 0.12 || color.r + color.g * 3.0 < 3.4 * color.b) { // Emissive Part
                                    if (color.r < 0.37 * color.b  ||  color.r + color.g * 3.0 < 3.4 * color.b) { // Emissive Part
                                        emission = pow2(color.g + 0.2 * color.b) * 4.5 + 0.15;
                                    } else { // Flat Part
// @included "/lib/materials/specificMaterials/planks/warpedPlanks.glsl"
smoothnessG = pow2(color.g) * 0.5;
smoothnessD = smoothnessG;

                                    }
                                }
                                else /*if (mat < 10232)*/ { // Bedrock
                                    smoothnessG = color.b * 0.2 + 0.1;
                                    smoothnessD = smoothnessG;

                                }
                            } else {
                                if (mat < 10236) { // Sand, Suspicious Sand
                                    smoothnessG = pow(color.g, 16.0) * 2.0;
                                    smoothnessG = min1(smoothnessG);
                                    smoothnessD = smoothnessG;
                                    highlightMult = 2.0;

                                        DoBrightBlockTweaks(color.rgb, 0.5, shadowMult, highlightMult);

                                        DoOceanBlockTweaks(smoothnessD);

                                }
                                else /*if (mat < 10240)*/ { // Red Sand
                                    smoothnessG = pow(color.r, 10.0);
                                    smoothnessG = min1(smoothnessG);
                                    smoothnessD = smoothnessG;
                                    highlightMult = 2.0;


                                }
                            }
                        } else {
                            if (mat < 10248) {
                                if (mat < 10244) { // Sandstone+
                                    highlightMult = 2.0;
                                    smoothnessG = pow2(pow2(color.g)) * 0.5;
                                    smoothnessG = min1(smoothnessG);
                                    smoothnessD = smoothnessG;

                                        DoBrightBlockTweaks(color.rgb, 0.5, shadowMult, highlightMult);

                                }
                                else /*if (mat < 10248)*/ { // Red Sandstone+
                                    highlightMult = 2.0;
                                    smoothnessG = pow2(pow2(color.r)) * 0.35;
                                    smoothnessG = min1(smoothnessG);
                                    smoothnessD = smoothnessG;

                                }
                            } else {
                                if (mat < 10252) { // Netherite Block
// @included "/lib/materials/specificMaterials/terrain/netheriteBlock.glsl"
smoothnessG = pow2(color.r * 2.0);
smoothnessG = min1(smoothnessG);
highlightMult = smoothnessG * 2.0;
smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10256)*/ { // Ancient Debris
                                    smoothnessG = pow2(color.r);
                                    smoothnessG = min1(smoothnessG);
                                    smoothnessD = smoothnessG;


                                }
                            }
                        }
                    }
                }
            }
        } else {
            if (mat < 10384) {
                if (mat < 10320) {
                    if (mat < 10288) {
                        if (mat < 10272) {
                            if (mat < 10264) {
                                if (mat < 10260) { // Iron Bars
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/terrain/ironBlock.glsl"
    smoothnessG = pow2(pow2(color.r));
highlightMult = smoothnessG * 3.0;
smoothnessD = smoothnessG;
materialMask = OSIEBCA; // Intense Fresnel

color.rgb *= 0.6 + 0.5 * GetLuminance(color.rgb);

                                }
                                else /*if (mat < 10264)*/ { // ACT solid blocks with no properties

                                }
                            } else {
                                if (mat < 10268) { // Iron Block, Heavy Weighted Pressure Plate
// @included "/lib/materials/specificMaterials/terrain/ironBlock.glsl"
    smoothnessG = pow2(pow2(color.r));
highlightMult = smoothnessG * 3.0;
smoothnessD = smoothnessG;
materialMask = OSIEBCA; // Intense Fresnel

color.rgb *= 0.6 + 0.5 * GetLuminance(color.rgb);

                                    color.rgb *= max(color.r, 0.85) * 0.9;
                                }
                                else /*if (mat < 10272)*/ { // Raw Iron Block
// @included "/lib/materials/specificMaterials/terrain/rawIronBlock.glsl"
materialMask = OSIEBCA; // Intense Fresnel
smoothnessG = pow2(pow2(color.r)) * 0.7;
smoothnessD = smoothnessG * 0.6;
                                }
                            }
                        } else {
                            if (mat < 10280) {
                                if (mat < 10276) { // Iron Ore
                                    if (color.r != color.g) { // Iron Ore:Raw Iron Part
// @included "/lib/materials/specificMaterials/terrain/rawIronBlock.glsl"
materialMask = OSIEBCA; // Intense Fresnel
smoothnessG = pow2(pow2(color.r)) * 0.7;
smoothnessD = smoothnessG * 0.6;
                                    } else { // Iron Ore:Stone Part
// @included "/lib/materials/specificMaterials/terrain/stone.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

                                    }
                                }
                                else /*if (mat < 10280)*/ { // Deepslate Iron Ore
                                    if (color.r != color.g) { // Deepslate Iron Ore:Raw Iron Part
// @included "/lib/materials/specificMaterials/terrain/rawIronBlock.glsl"
materialMask = OSIEBCA; // Intense Fresnel
smoothnessG = pow2(pow2(color.r)) * 0.7;
smoothnessD = smoothnessG * 0.6;
                                    } else { // Deepslate Iron Ore:Deepslate Part
// @included "/lib/materials/specificMaterials/terrain/deepslate.glsl"
smoothnessG = pow(color.g, 1.5) * 0.55;
smoothnessG = smoothnessG;
smoothnessD = smoothnessG;

materialMask = OSIEBCA; // Intense Fresnel
                                    }
                                }
                            } else {
                                if (mat < 10284) { // Raw Copper Block
// @included "/lib/materials/specificMaterials/terrain/rawCopperBlock.glsl"
materialMask = OSIEBCA * 2.0; // Copper Fresnel
smoothnessG = pow2(color.r + color.g * 0.25) * 0.4;
smoothnessG = min1(smoothnessG);
smoothnessD = min1(smoothnessG * smoothnessG * 2.0);
                                }
                                else /*if (mat < 10288)*/ { // Copper Ore
                                    if (color.r != color.g) { // Copper Ore:Raw Copper Part
// @included "/lib/materials/specificMaterials/terrain/rawCopperBlock.glsl"
materialMask = OSIEBCA * 2.0; // Copper Fresnel
smoothnessG = pow2(color.r + color.g * 0.25) * 0.4;
smoothnessG = min1(smoothnessG);
smoothnessD = min1(smoothnessG * smoothnessG * 2.0);
                                    } else { // Copper Ore:Stone Part
// @included "/lib/materials/specificMaterials/terrain/stone.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

                                    }
                                }
                            }
                        }
                    } else {
                        if (mat < 10304) {
                            if (mat < 10296) {
                                if (mat < 10292) { // Deepslate Copper Ore
                                    if (color.r != color.g) { // Deepslate Copper Ore:Raw Copper Part
// @included "/lib/materials/specificMaterials/terrain/rawCopperBlock.glsl"
materialMask = OSIEBCA * 2.0; // Copper Fresnel
smoothnessG = pow2(color.r + color.g * 0.25) * 0.4;
smoothnessG = min1(smoothnessG);
smoothnessD = min1(smoothnessG * smoothnessG * 2.0);
                                    } else { // Deepslate Copper Ore:Deepslate Part
// @included "/lib/materials/specificMaterials/terrain/deepslate.glsl"
smoothnessG = pow(color.g, 1.5) * 0.55;
smoothnessG = smoothnessG;
smoothnessD = smoothnessG;

materialMask = OSIEBCA; // Intense Fresnel
                                    }
                                }
                                else /*if (mat < 10296)*/ { // Copper Block++:All Non-raw Variants
// @included "/lib/materials/specificMaterials/terrain/copperBlock.glsl"
materialMask = OSIEBCA * 2.0; // Copper Fresnel
smoothnessG = pow2(pow2(color.r)) + pow2(max0(color.g - color.r * 0.5)) * 0.3;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

color.rgb *= min1(0.6 + 0.7 * GetLuminance(color.rgb));

                                }
                            } else {
                                if (mat < 10300) { // Raw Gold Block
// @included "/lib/materials/specificMaterials/terrain/rawGoldBlock.glsl"
materialMask = OSIEBCA * 3.0; // Gold Fresnel
smoothnessG = pow2(pow2(color.g));
smoothnessD = 0.5 * (smoothnessG + color.b);
                                }
                                else /*if (mat < 10304)*/ { // Gold Ore
                                    if ((color.g - color.b > 0.15 || color.r > 0.99)  &&  (color.r + color.g > 1.9 || color.r + color.g + color.b < 1.9)) { // Gold Ore:Raw Gold Part
// @included "/lib/materials/specificMaterials/terrain/rawGoldBlock.glsl"
materialMask = OSIEBCA * 3.0; // Gold Fresnel
smoothnessG = pow2(pow2(color.g));
smoothnessD = 0.5 * (smoothnessG + color.b);
                                    } else { // Gold Ore:Stone Part
// @included "/lib/materials/specificMaterials/terrain/stone.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

                                    }
                                }
                            }
                        } else {
                            if (mat < 10312) {
                                if (mat < 10308) { // Deepslate Gold Ore
                                    if (color.r != color.g  ||  color.r > 0.99) { // Deepslate Gold Ore:Raw Gold Part
// @included "/lib/materials/specificMaterials/terrain/rawGoldBlock.glsl"
materialMask = OSIEBCA * 3.0; // Gold Fresnel
smoothnessG = pow2(pow2(color.g));
smoothnessD = 0.5 * (smoothnessG + color.b);
                                    } else { // Deepslate Gold Ore:Deepslate Part
// @included "/lib/materials/specificMaterials/terrain/deepslate.glsl"
smoothnessG = pow(color.g, 1.5) * 0.55;
smoothnessG = smoothnessG;
smoothnessD = smoothnessG;

materialMask = OSIEBCA; // Intense Fresnel
                                    }
                                }
                                else /*if (mat < 10312)*/ { // Nether Gold Ore
                                    if (color.g != color.b) { // Nether Gold Ore:Raw Gold Part
// @included "/lib/materials/specificMaterials/terrain/rawGoldBlock.glsl"
materialMask = OSIEBCA * 3.0; // Gold Fresnel
smoothnessG = pow2(pow2(color.g));
smoothnessD = 0.5 * (smoothnessG + color.b);
                                    } else { // Nether Gold Ore:Netherrack Part
// @included "/lib/materials/specificMaterials/terrain/netherrack.glsl"
    smoothnessG = color.r * 0.4 + 0.2;
smoothnessD = smoothnessG;
                                    }
                                }
                            } else {
                                if (mat < 10316) { // Gold Block, Light Weighted Pressure Plate
// @included "/lib/materials/specificMaterials/terrain/goldBlock.glsl"
materialMask = OSIEBCA * 3.0; // Gold Fresnel

    float colorG2 = pow2(color.g);
float colorG4 = pow2(colorG2);
float factor = max(color.g, 0.8);

smoothnessG = min1(factor - colorG4 * 0.5);
highlightMult = 3.5 * max(colorG4, 0.2);

smoothnessD = colorG4;

color.rgb *= 0.5 + 0.4 * GetLuminance(color.rgb);

                                }
                                else /*if (mat < 10320)*/ { // Diamond Block
// @included "/lib/materials/specificMaterials/terrain/diamondBlock.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = max(color.g, 0.8);
float factor2 = pow2(factor);
    float factor4 = pow2(factor2);

smoothnessG = factor - pow2(pow2(color.g)) * 0.4;
highlightMult = 3.0 * max(pow2(factor4), 0.2);

smoothnessD = factor4 * 0.75;

    highlightMult *= 2.0;
    smoothnessD /= 0.75;

color.rgb *= 0.7 + 0.4 * GetLuminance(color.rgb);

                                }
                            }
                        }
                    }
                } else {
                    if (mat < 10352) {
                        if (mat < 10336) {
                            if (mat < 10328) {
                                if (mat < 10324) { // Diamond Ore
                                    if (color.b / color.r > 1.5  ||  color.b > 0.8) { // Diamond Ore:Diamond Part
// @included "/lib/materials/specificMaterials/terrain/diamondBlock.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = max(color.g, 0.8);
float factor2 = pow2(factor);
    float factor4 = pow2(factor2);

smoothnessG = factor - pow2(pow2(color.g)) * 0.4;
highlightMult = 3.0 * max(pow2(factor4), 0.2);

smoothnessD = factor4 * 0.75;

    highlightMult *= 2.0;
    smoothnessD /= 0.75;

color.rgb *= 0.7 + 0.4 * GetLuminance(color.rgb);

                                    } else { // Diamond Ore:Stone Part, Diamond Ore:StoneToDiamond part
// @included "/lib/materials/specificMaterials/terrain/stone.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

                                    }
                                }
                                else /*if (mat < 10328)*/ { // Deepslate Diamond Ore
                                    if (color.b / color.r > 1.5  ||  color.b > 0.8) { // Deepslate Diamond Ore:Diamond Part
// @included "/lib/materials/specificMaterials/terrain/diamondBlock.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = max(color.g, 0.8);
float factor2 = pow2(factor);
    float factor4 = pow2(factor2);

smoothnessG = factor - pow2(pow2(color.g)) * 0.4;
highlightMult = 3.0 * max(pow2(factor4), 0.2);

smoothnessD = factor4 * 0.75;

    highlightMult *= 2.0;
    smoothnessD /= 0.75;

color.rgb *= 0.7 + 0.4 * GetLuminance(color.rgb);

                                    } else { // Deepslate Diamond Ore:Deepslate Part, Deepslate Diamond Ore:DeepslateToDiamond part
// @included "/lib/materials/specificMaterials/terrain/deepslate.glsl"
smoothnessG = pow(color.g, 1.5) * 0.55;
smoothnessG = smoothnessG;
smoothnessD = smoothnessG;

materialMask = OSIEBCA; // Intense Fresnel
                                    }
                                }
                            } else {
                                if (mat < 10332) { // Amethyst Block, Budding Amethyst
// @included "/lib/materials/specificMaterials/terrain/amethyst.glsl"
materialMask = OSIEBCA; // Intense Fresnel
float factor = pow2(color.r);
smoothnessG = 0.8 - factor * 0.3;
highlightMult = factor * 3.0;
smoothnessD = factor;


                                    color.rgb *= 0.7 + 0.3 * GetLuminance(color.rgb);

                                }
                                else /*if (mat < 10336)*/ { // Amethyst Cluster, Amethyst Buds
// @included "/lib/materials/specificMaterials/terrain/amethyst.glsl"
materialMask = OSIEBCA; // Intense Fresnel
float factor = pow2(color.r);
smoothnessG = 0.8 - factor * 0.3;
highlightMult = factor * 3.0;
smoothnessD = factor;

                                    noSmoothLighting = true;
                                    lmCoordM.x *= 0.85;

                                            vec3 worldPos = playerPos.xyz + cameraPosition.xyz;
                                            vec3 blockPos = abs(fract(worldPos) - vec3(0.5));
                                            float maxBlockPos = max(blockPos.x, max(blockPos.y, blockPos.z));
                                            emission = pow2(max0(1.0 - maxBlockPos * 1.85) * color.g) * 7.0;

                                            if (CheckForColor(color.rgb, vec3(254, 203, 230)))
                                                emission = pow(emission, max0(1.0 - 0.2 * max0(emission - 1.0)));

                                            color.g *= 1.0 - emission * 0.07;

                                            emission *= 1.3;

                                }
                            }
                        } else {
                            if (mat < 10344) {
                                if (mat < 10340) { // Emerald Block
// @included "/lib/materials/specificMaterials/terrain/emeraldBlock.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = pow2(sqrt2(GetLuminance(color.rgb)));
float factor2 = pow2(factor);
float factor4 = pow2(factor2);

smoothnessG = factor - factor4 * 0.4;
highlightMult = 3.0 * factor4;

smoothnessD = factor4 * 0.75;

                                }
                                else /*if (mat < 10344)*/ { // Emerald Ore
                                    float dif = GetMaxColorDif(color.rgb);
                                    if (dif > 0.4  ||  color.b > 0.85) { // Emerald Ore:Emerald Part
// @included "/lib/materials/specificMaterials/terrain/emeraldBlock.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = pow2(sqrt2(GetLuminance(color.rgb)));
float factor2 = pow2(factor);
float factor4 = pow2(factor2);

smoothnessG = factor - factor4 * 0.4;
highlightMult = 3.0 * factor4;

smoothnessD = factor4 * 0.75;

                                    } else { // Emerald Ore:Stone Part
// @included "/lib/materials/specificMaterials/terrain/stone.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

                                    }
                                }
                            } else {
                                if (mat < 10348) { // Deepslate Emerald Ore
                                    float dif = GetMaxColorDif(color.rgb);
                                    if (dif > 0.4  ||  color.b > 0.85) { // Deepslate Emerald Ore:Emerald Part
// @included "/lib/materials/specificMaterials/terrain/emeraldBlock.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = pow2(sqrt2(GetLuminance(color.rgb)));
float factor2 = pow2(factor);
float factor4 = pow2(factor2);

smoothnessG = factor - factor4 * 0.4;
highlightMult = 3.0 * factor4;

smoothnessD = factor4 * 0.75;

                                    } else { // Deepslate Emerald Ore:Deepslate Part
// @included "/lib/materials/specificMaterials/terrain/deepslate.glsl"
smoothnessG = pow(color.g, 1.5) * 0.55;
smoothnessG = smoothnessG;
smoothnessD = smoothnessG;

materialMask = OSIEBCA; // Intense Fresnel
                                    }
                                }
                                else /*if (mat < 10352)*/ { // Azalea, Flowering Azalea
// @included "/lib/materials/specificMaterials/terrain/cobblestone.glsl"
smoothnessG = dot(color.rgb, color.rgb) * 0.17;
smoothnessD = smoothnessG;
smoothnessG = max(smoothnessG, 0.3 * color.g * float(color.g > color.b * 1.5));
                                }
                            }
                        }
                    } else {
                        if (mat < 10368) {
                            if (mat < 10360) {
                                if (mat < 10356) { // Lapis Block
// @included "/lib/materials/specificMaterials/terrain/lapisBlock.glsl"
smoothnessG = pow2(color.b) * 0.8;
smoothnessD = smoothnessG;


                                }
                                else /*if (mat < 10360)*/ { // Lapis Ore
                                    if (color.r != color.g) { // Lapis Ore:Lapis Part
// @included "/lib/materials/specificMaterials/terrain/lapisBlock.glsl"
smoothnessG = pow2(color.b) * 0.8;
smoothnessD = smoothnessG;

                                        smoothnessG *= 0.5;
                                        smoothnessD *= 0.5;
                                    } else { // Lapis Ore:Stone Part
// @included "/lib/materials/specificMaterials/terrain/stone.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

                                    }
                                }
                            } else {
                                if (mat < 10364) { // Deepslate Lapis Ore
                                    if (color.r != color.g) { // Deepslate Lapis Ore:Lapis Part
// @included "/lib/materials/specificMaterials/terrain/lapisBlock.glsl"
smoothnessG = pow2(color.b) * 0.8;
smoothnessD = smoothnessG;

                                        smoothnessG *= 0.5;
                                        smoothnessD *= 0.5;
                                    } else { // Deepslate Lapis Ore:Deepslate Part
// @included "/lib/materials/specificMaterials/terrain/deepslate.glsl"
smoothnessG = pow(color.g, 1.5) * 0.55;
smoothnessG = smoothnessG;
smoothnessD = smoothnessG;

materialMask = OSIEBCA; // Intense Fresnel
                                    }
                                }
                                else /*if (mat < 10368)*/ { // Quartz Block++
// @included "/lib/materials/specificMaterials/terrain/quartzBlock.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = color.g;
float factor2 = pow2(factor);
float factor4 = pow2(factor2);
float factor8 = pow2(factor4);

smoothnessG = factor - factor8 * 0.5;
highlightMult = 3.5 * factor8;

smoothnessD = factor8;

    DoBrightBlockTweaks(color.rgb, 0.5, shadowMult, highlightMult);

                                }
                            }
                        } else {
                            if (mat < 10376) {
                                if (mat < 10372) { // Nether Quartz Ore
                                    if (color.g != color.b) { // Nether Quartz Ore:Quartz Part
// @included "/lib/materials/specificMaterials/terrain/quartzBlock.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = color.g;
float factor2 = pow2(factor);
float factor4 = pow2(factor2);
float factor8 = pow2(factor4);

smoothnessG = factor - factor8 * 0.5;
highlightMult = 3.5 * factor8;

smoothnessD = factor8;

    DoBrightBlockTweaks(color.rgb, 0.5, shadowMult, highlightMult);

                                    } else { // Nether Quartz Ore:Netherrack Part
// @included "/lib/materials/specificMaterials/terrain/netherrack.glsl"
    smoothnessG = color.r * 0.4 + 0.2;
smoothnessD = smoothnessG;
                                    }
                                }
                                else /*if (mat < 10376)*/ { // Obsidian
// @included "/lib/materials/specificMaterials/terrain/obsidian.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = max0(0.3 - abs(color.r - 0.3)) * 1.5;

smoothnessG = factor;
highlightMult = 2.0 + min1(smoothnessG * 2.0) * 1.5;
smoothnessG = min1(smoothnessG);

smoothnessD = min1(factor + 0.07);

                                }
                            } else {
                                if (mat < 10380) { // Purpur Block+
                                    highlightMult = 2.0;
                                    smoothnessG = pow2(color.r) * 0.6;
                                    smoothnessG = min1(smoothnessG);
                                    smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10384)*/ { // 8 Layer Snow, Snow Block, Powder Snow
// @included "/lib/materials/specificMaterials/terrain/snow.glsl"
smoothnessG = (1.0 - pow(color.g, 64.0) * 0.3) * 0.4;
highlightMult = 2.0;

smoothnessD = smoothnessG;

    DoBrightBlockTweaks(color.rgb, 0.5, shadowMult, highlightMult);

                                }
                            }
                        }
                    }
                }
            } else {
                if (mat < 10448) {
                    if (mat < 10416) {
                        if (mat < 10400) {
                            if (mat < 10392) {
                                if (mat < 10388) { // Packed Ice
                                    materialMask = OSIEBCA; // Intense Fresnel
                                    float factor = pow2(color.g);
                                    float factor2 = pow2(factor);
                                    smoothnessG = 1.0 - 0.5 * factor;
                                    highlightMult = factor2 * 3.5;
                                    smoothnessD = factor;

                                }
                                else /*if (mat < 10392)*/ { // Blue Ice
                                    materialMask = OSIEBCA; // Intense Fresnel
                                    float factor = min1(pow2(color.g) * 1.38);
                                    float factor2 = pow2(factor);
                                    smoothnessG = 1.0 - 0.5 * factor;
                                    highlightMult = factor2 * 3.5;
                                    smoothnessD = pow1_5(color.g);

                                }
                            } else {
                                if (mat < 10396) { // Pumpkin, Carved Pumpkin
// @included "/lib/materials/specificMaterials/terrain/pumpkin.glsl"
smoothnessG = color.g * 0.25;
                                }
                                else /*if (mat < 10400)*/ { // Jack o'Lantern
// @included "/lib/materials/specificMaterials/terrain/pumpkin.glsl"
smoothnessG = color.g * 0.25;
                                    noSmoothLighting = true, noDirectionalShading = true;
                                    lmCoordM.y = 0.0;
                                    lmCoordM.x = 1.0;

                                        if (color.b < 0.4)
                                            emission = clamp01(color.g * 1.3 - color.r) * 5.0;
                                }
                            }
                        } else {
                            if (mat < 10408) {
                                if (mat < 10404) { // Sea Pickle:Not Waterlogged
                                    noSmoothLighting = true;
                                }
                                else /*if (mat < 10408)*/ { // Sea Pickle:Waterlogged
                                    noSmoothLighting = true;

                                        color = perlinAtlasLod(float3(texCoord, jLayer), 0);

                                    if (color.b > 0.5) { // Sea Pickle:Emissive Part
                                            color.g *= 1.1;
                                            emission = 5.0;
                                    }
                                }
                            } else {
                                if (mat < 10412) { // Basalt+
                                    smoothnessG = color.r * 0.45;
                                    smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10416)*/ { // Glowstone
                                    noSmoothLighting = true; noDirectionalShading = true;
                                    lmCoordM = vec2(0.9, 0.0);

                                    emission = max0(color.g - 0.3) * 4.6;
                                    color.rg += emission * vec2(0.15, 0.05);

                                        DoDistantLightBokehMaterial(emission, 2.0, lViewPos);
                                }
                            }
                        }
                    } else {
                        if (mat < 10432) {
                            if (mat < 10424) {
                                if (mat < 10420) { // Nether Bricks+
                                    float factor = smoothstep1(min1(color.r * 1.5));
                                    factor = factor > 0.12 ? factor : factor * 0.5;
                                    smoothnessG = factor;
                                    smoothnessD = factor;
                                }
                                else /*if (mat < 10424)*/ { // Red Nether Bricks+
                                    float factor = color.r * 0.9;
                                    factor = color.r > 0.215 ? factor : factor * 0.25;
                                    smoothnessG = factor;
                                    smoothnessD = factor;

                                }
                            } else {
                                if (mat < 10428) { // Melon
                                    smoothnessG = color.r * 0.75;
                                    smoothnessD = color.r * 0.5;

                                }
                                else /*if (mat < 10432)*/ { // End Stone++,
// @included "/lib/materials/specificMaterials/terrain/endStone.glsl"
float factor = pow2(pow2(color.r));
smoothnessG = factor * 0.65;
smoothnessD = smoothnessG * 0.6;

                                }
                            }
                        } else {
                            if (mat < 10440) {
                                if (mat < 10436) { // Terracotta+
                                    smoothnessG = 0.25;
                                    highlightMult = 1.5;
                                    smoothnessD = 0.17;

                                }
                                else /*if (mat < 10440)*/ { // Glazed Terracotta+
                                    smoothnessG = 0.75;
                                    smoothnessD = 0.35;

                                }
                            } else {
                                if (mat < 10444) { // Prismarine+, Prismarine Bricks+
                                    smoothnessG = pow2(color.g) * 0.8;
                                    highlightMult = 1.5;
                                    smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10448)*/ { // Dark Prismarine+
                                    smoothnessG = min1(pow2(color.g) * 2.0);
                                    highlightMult = 1.5;
                                    smoothnessD = smoothnessG;

                                }
                            }
                        }
                    }
                } else {
                    if (mat < 10480) {
                        if (mat < 10464) {
                            if (mat < 10456) {
                                if (mat < 10452) { // Sea Lantern
                                    noSmoothLighting = true; noDirectionalShading = true;
                                    lmCoordM.x = 0.85;

                                    smoothnessD = min1(max0(0.5 - color.r) * 2.0);
                                    smoothnessG = color.g;

                                        float blockRes = absMidCoordPos.x * atlasSize.x;
                                        vec2 signMidCoordPosM = (floor((signMidCoordPos + 1.0) * blockRes) + 0.5) / blockRes - 1.0;
                                        float dotsignMidCoordPos = dot(signMidCoordPosM, signMidCoordPosM);
                                        float factor = pow2(max0(1.0 - 1.7 * pow2(pow2(dotsignMidCoordPos))));

                                    emission = pow2(color.b) * 1.6 + 2.2 * factor;
                                    emission *= 0.4 + max0(0.6 - 0.006 * lViewPos);

                                    color.rb *= vec2(1.13, 1.1);

                                        DoDistantLightBokehMaterial(emission, 2.0, lViewPos);

                                }
                                else /*if (mat < 10456)*/ { // Magma Block
                                    noSmoothLighting = true; noDirectionalShading = true;
                                    lmCoordM = vec2(0.75, 0.0);

                                    if (color.g > 0.22) { // Emissive Part
                                        emission = pow2(pow2(color.r)) * 4.0;


                                        color.gb *= max(2.0 - 11.0 * pow2(color.g), 0.5);

                                        maRecolor = vec3(emission * 0.075);
                                    } else { // Netherrack Part
// @included "/lib/materials/specificMaterials/terrain/netherrack.glsl"
    smoothnessG = color.r * 0.4 + 0.2;
smoothnessD = smoothnessG;

                                        emission = 0.2;
                                    }

                                }
                            } else {
                                if (mat < 10460) { // Command Block+
                                        color = perlinAtlasLod(float3(texCoord, jLayer), 0);

                                    vec2 coord = signMidCoordPos;
                                    float blockRes = absMidCoordPos.x * atlasSize.x;
                                    vec2 absCoord = abs(coord);
                                    float maxCoord = max(absCoord.x, absCoord.y);

                                    float dif = GetMaxColorDif(color.rgb);

                                    if ( // This mess exists because Iris' midCoord is slightly inaccurate
                                        dif > 0.1  &&  maxCoord < 0.375  && 
                                        !CheckForColor(color.rgb, vec3(111, 73, 43))  && 
                                        !CheckForColor(color.rgb, vec3(207, 166, 139))  && 
                                        !CheckForColor(color.rgb, vec3(155, 139, 207))  && 
                                        !CheckForColor(color.rgb, vec3(161, 195, 180))  && 
                                        !CheckForColor(color.rgb, vec3(201, 143, 107))  && 
                                        !CheckForColor(color.rgb, vec3(135, 121, 181))  && 
                                        !CheckForColor(color.rgb, vec3(131, 181, 145))
                                    ) {
                                        emission = 6.0;
                                        color.rgb *= color.rgb;
                                        highlightMult = 2.0;
                                        maRecolor = vec3(0.5);
                                    } else {
                                        smoothnessG = dot(color.rgb, color.rgb) * 0.33;
                                        smoothnessD = smoothnessG;
                                    }
                                }
                                else /*if (mat < 10464)*/ { // Concrete+ except Lime
                                    smoothnessG = 0.4;
                                    highlightMult = 1.5;
                                    smoothnessD = 0.3;

                                }
                            }
                        } else {
                            if (mat < 10472) {
                                if (mat < 10468) { // Concrete Powder+
                                    smoothnessG = 0.2;
                                    smoothnessD = 0.1;

                                }
                                else /*if (mat < 10472)*/ { // Coral Block+
// @included "/lib/materials/specificMaterials/terrain/coral.glsl"
float lColor = length(color.rgb);
smoothnessG = lColor * 0.2;
smoothnessD = lColor * 0.15;

                                }
                            } else {
                                if (mat < 10476) { // Coral Fan+, Coral+
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/terrain/coral.glsl"
float lColor = length(color.rgb);
smoothnessG = lColor * 0.2;
smoothnessD = lColor * 0.15;

                                }
                                else /*if (mat < 10480)*/ { // Crying Obsidian
// @included "/lib/materials/specificMaterials/terrain/cryingObsidian.glsl"
// @included "/lib/materials/specificMaterials/terrain/obsidian.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = max0(0.3 - abs(color.r - 0.3)) * 1.5;

smoothnessG = factor;
highlightMult = 2.0 + min1(smoothnessG * 2.0) * 1.5;
smoothnessG = min1(smoothnessG);

smoothnessD = min1(factor + 0.07);


highlightMult *= 0.5;

float factor0 = sqrt2(max0(color.b - color.g * 6.0));
float factor1 = pow2(color.b);
emission = 1.35 + pow2(pow2(factor1)) * 7.5;
emission *= factor0;
color.r *= 1.15;

maRecolor = vec3(factor0 * min(max0(factor1 * 0.7 - 0.1) * 1.3, 0.5));

                                }
                            }
                        }
                    } else {
                        if (mat < 10496) {
                            if (mat < 10488) {
                                if (mat < 10484) { // Blackstone++
// @included "/lib/materials/specificMaterials/terrain/blackstone.glsl"
smoothnessG = color.r;
smoothnessD = color.r * 0.65;
                                }
                                else /*if (mat < 10488)*/ { // Gilded Blackstone
                                    if (color.r > color.b * 3.0) { // Gilded Blackstone:Gilded Part
// @included "/lib/materials/specificMaterials/terrain/rawGoldBlock.glsl"
materialMask = OSIEBCA * 3.0; // Gold Fresnel
smoothnessG = pow2(pow2(color.g));
smoothnessD = 0.5 * (smoothnessG + color.b);
                                    } else { // Gilded Blackstone:Blackstone Part
// @included "/lib/materials/specificMaterials/terrain/blackstone.glsl"
smoothnessG = color.r;
smoothnessD = color.r * 0.65;
                                    }

                                }
                            } else {
                                if (mat < 10492) { // Lily Pad
                                    noSmoothLighting = true;
                                    subsurfaceMode = 2;


                                        float factor = min1(color.g * 2.0);
                                        smoothnessG = factor * 0.5;
                                        highlightMult = factor;
                                }
                                else /*if (mat < 10496)*/ { // Dirt Path
// @included "/lib/materials/specificMaterials/terrain/dirt.glsl"
smoothnessG = color.r * 0.1 + 0.1;
                                        glColor.a = sqrt(glColor.a);
                                }
                            }
                        } else {
                            if (mat < 10504) {
                                if (mat < 10500) { // Torch
                                    noDirectionalShading = true;

                                    if (color.r > 0.95) {
                                        noSmoothLighting = true;
                                        lmCoordM.x = 1.0;
                                        emission = GetLuminance(color.rgb);
                                            emission *= 3.5;
                                        color.r *= 1.3;
                                        color.b *= 0.5;
                                    }

                                        else if (abs(NdotU) < 0.5) {
                                                    lmCoordM.x = min1(0.7 + 0.3 * pow2(1.0 - signMidCoordPos.y));
                                        } else {
                                            lmCoordM.x = 0.7;
                                        }

                                        DoDistantLightBokehMaterial(color, vec4(1.0, 0.6, 0.2, 1.0), emission, 5.0, lViewPos);

                                    emission += 0.0001; // No light reducing during noon
                                }
                                else /*if (mat < 10504)*/ { // End Rod
                                    noDirectionalShading = true;

                                        noSmoothLighting = true;
                                        lmCoordM.x = 0.92;

                                    float dotColor = dot(color.rgb, color.rgb);
                                    if (dotColor > 2.0) {
                                        emission = 3.3;
                                        emission *= 0.4 + max0(0.6 - 0.006 * lViewPos);

                                        color.rgb = pow2(color.rgb);
                                        color.g *= 0.95;
                                    }

                                        else { // Directional Self-light Effect
                                            vec3 fractPos = abs(fract(playerPos + cameraPosition) - 0.5);
                                            float maxCoord = max(fractPos.x, max(fractPos.y, fractPos.z));
                                            if (maxCoord < 0.4376) {
                                                lmCoordM.x = 1.0;
                                                color.rgb *= 1.8;
                                            }
                                        }

                                        DoDistantLightBokehMaterial(emission, 4.0, lViewPos);
                                }
                            } else {
                                if (mat < 10508) { // Chorus Plant

                                }
                                else /*if (mat < 10512)*/ { // Chorus Flower:Alive
                                    float dotColor = dot(color.rgb, color.rgb);
                                    if (dotColor > 1.0)
                                        emission = pow2(pow2(pow2(dotColor * 0.33))) + 0.2 * dotColor;
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        if (mat < 10768) {
            if (mat < 10640) {
                if (mat < 10576) {
                    if (mat < 10544) {
                        if (mat < 10528) {
                            if (mat < 10520) {
                                if (mat < 10516) { // Chorus Flower:Dead
                                        vec3 checkColor = perlinAtlasLod(float3(texCoord, jLayer), 0).rgb;

                                    if (CheckForColor(checkColor, vec3(164, 157, 126))  || 
                                        CheckForColor(checkColor, vec3(201, 197, 176))  || 
                                        CheckForColor(checkColor, vec3(226, 221, 188))  || 
                                        CheckForColor(checkColor, vec3(153, 142, 95))
                                    ) {
                                        emission = min(GetLuminance(color.rgb), 0.75) / 0.75;
                                        emission = pow2(pow2(emission)) * 6.5;
                                        color.gb *= 0.85;
                                    }

                                    else {
                                        emission = max0(GetLuminance(color.rgb) - 0.5) * 3.0;

                                    }
                                }
                                else /*if (mat < 10520)*/ { // Furnace:Lit
                                    lmCoordM.x *= 0.95;

// @included "/lib/materials/specificMaterials/terrain/cobblestone.glsl"
smoothnessG = dot(color.rgb, color.rgb) * 0.17;
smoothnessD = smoothnessG;
smoothnessG = max(smoothnessG, 0.3 * color.g * float(color.g > color.b * 1.5));

                                    float dotColor = dot(color.rgb, color.rgb);
                                    emission = 2.5 * dotColor * max0(pow2(pow2(pow2(color.r))) - color.b) + pow(dotColor * 0.35, 32.0);
                                    color.r *= 1.0 + 0.1 * emission;
                                }
                            } else {
                                if (mat < 10524) { // Cactus
                                    float factor = sqrt1(color.r);
                                    smoothnessG = factor * 0.5;
                                    highlightMult = factor;
                                }
                                else /*if (mat < 10528)*/ { // Note Block, Jukebox
                                    float factor = color.r * 0.5;
                                    smoothnessG = factor;
                                    smoothnessD = factor;

                                }
                            }
                        } else {
                            if (mat < 10536) {
                                if (mat < 10532) { // Soul Torch
                                    noSmoothLighting = true; noDirectionalShading = true;
                                    lmCoordM.x = min(lmCoordM.x * 0.9, 0.77);

                                    if (color.b > 0.6) {
                                        emission = 2.7;
                                        color.rgb = pow1_5(color.rgb);
                                        color.r = min1(color.r + 0.1);
                                    }
                                    emission += 0.0001; // No light reducing during noon

                                        DoDistantLightBokehMaterial(color, vec4(0.5, 1.0, 1.0, 1.0), emission, 3.0, lViewPos);

                                }
                                else /*if (mat < 10536)*/ { // Brown Mushroom Block
                                    if (color.r > color.g  &&  color.g > color.b  &&  color.b > 0.37) {
// @included "/lib/materials/specificMaterials/terrain/cobblestone.glsl"
smoothnessG = dot(color.rgb, color.rgb) * 0.17;
smoothnessD = smoothnessG;
smoothnessG = max(smoothnessG, 0.3 * color.g * float(color.g > color.b * 1.5));
                                    } else {
                                        float factor = pow2(color.r) * color.r * 0.8;
                                        highlightMult = 1.5;
                                        smoothnessG = factor;
                                        smoothnessD = factor * 0.9;

                                    }
                                }
                            } else {
                                if (mat < 10540) { // Red Mushroom Block
                                    if (color.r > color.g  &&  color.g > color.b  &&  color.b > 0.37) {
// @included "/lib/materials/specificMaterials/terrain/cobblestone.glsl"
smoothnessG = dot(color.rgb, color.rgb) * 0.17;
smoothnessD = smoothnessG;
smoothnessG = max(smoothnessG, 0.3 * color.g * float(color.g > color.b * 1.5));
                                    } else {
                                        float factor = min1(pow2(color.g) + 0.25);
                                        highlightMult = 1.5;
                                        smoothnessG = factor;
                                        smoothnessD = factor * 0.7;

                                    }
                                }
                                else /*if (mat < 10544)*/ { // Mushroom Stem,
                                    if (color.r > color.g  &&  color.g > color.b  &&  color.b < 0.6) {
// @included "/lib/materials/specificMaterials/terrain/cobblestone.glsl"
smoothnessG = dot(color.rgb, color.rgb) * 0.17;
smoothnessD = smoothnessG;
smoothnessG = max(smoothnessG, 0.3 * color.g * float(color.g > color.b * 1.5));
                                    } else {
                                        float factor = pow2(pow2(color.g));
                                        highlightMult = 1.5;
                                        smoothnessG = factor;
                                        smoothnessD = factor * 0.5;

                                    }
                                }
                            }
                        }
                    } else {
                        if (mat < 10560) {
                            if (mat < 10552) {
                                if (mat < 10548) { // Glow Lichen
                                    noSmoothLighting = true;

                                        float dotColor = dot(color.rgb, color.rgb);
                                        emission = min(pow2(pow2(dotColor) * dotColor) * 1.4 + dotColor * 0.9, 6.0);
                                        emission = mix(emission, dotColor * 1.5, min1(lViewPos / 96.0)); // Less noise in the distance

                                            float lichenSkyLightFactor = pow2(1.0 - min1(lmCoord.y * 2.9));
                                            emission *= lichenSkyLightFactor;

                                            color.r *= 1.0 + 0.15 * lichenSkyLightFactor;
                                }
                                else /*if (mat < 10552)*/ { // Enchanting Table:Base
                                    float dotColor = dot(color.rgb, color.rgb);
                                    if (dotColor < 0.19  &&  color.r < color.b) {
// @included "/lib/materials/specificMaterials/terrain/obsidian.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = max0(0.3 - abs(color.r - 0.3)) * 1.5;

smoothnessG = factor;
highlightMult = 2.0 + min1(smoothnessG * 2.0) * 1.5;
smoothnessG = min1(smoothnessG);

smoothnessD = min1(factor + 0.07);

                                    } else if (color.g >= color.r) {
// @included "/lib/materials/specificMaterials/terrain/diamondBlock.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = max(color.g, 0.8);
float factor2 = pow2(factor);
    float factor4 = pow2(factor2);

smoothnessG = factor - pow2(pow2(color.g)) * 0.4;
highlightMult = 3.0 * max(pow2(factor4), 0.2);

smoothnessD = factor4 * 0.75;

    highlightMult *= 2.0;
    smoothnessD /= 0.75;

color.rgb *= 0.7 + 0.4 * GetLuminance(color.rgb);

                                    } else {
                                        smoothnessG = color.r * 0.3 + 0.1;
                                    }

                                }
                            } else {
                                if (mat < 10556) { // End Portal Frame:Inactive
                                    noSmoothLighting = true;

                                    if (abs(color.r - color.g - 0.05) < 0.10) {
// @included "/lib/materials/specificMaterials/terrain/endStone.glsl"
float factor = pow2(pow2(color.r));
smoothnessG = factor * 0.65;
smoothnessD = smoothnessG * 0.6;

                                    } else {
// @included "/lib/materials/specificMaterials/terrain/endPortalFrame.glsl"
// End Portal Frame:Green Parts
smoothnessG = 0.25;
smoothnessD = 0.45;
                                    }

                                }
                                else /*if (mat < 10560)*/ { // End Portal Frame:Active
                                    noSmoothLighting = true;

                                    if (abs(color.r - color.g - 0.05) < 0.10) {
// @included "/lib/materials/specificMaterials/terrain/endStone.glsl"
float factor = pow2(pow2(color.r));
smoothnessG = factor * 0.65;
smoothnessD = smoothnessG * 0.6;

                                    } else {
// @included "/lib/materials/specificMaterials/terrain/endPortalFrame.glsl"
// End Portal Frame:Green Parts
smoothnessG = 0.25;
smoothnessD = 0.45;

                                        vec2 absCoord = abs(fract(playerPos.xz + cameraPosition.xz) - 0.5);
                                        float maxCoord = max(absCoord.x, absCoord.y);
                                        if (maxCoord < 0.2505) { // End Portal Frame:Eye of Ender
                                            smoothnessG = 0.5;
                                            smoothnessD = 0.5;
                                            emission = pow2(min(color.g, 0.25)) * 170.0 * (0.28 - maxCoord);
                                        } else {
                                            float minCoord = min(absCoord.x, absCoord.y);
                                            if (CheckForColor(color.rgb, vec3(153, 198, 147))
                                             &&  minCoord > 0.25) { // End Portal Frame:Emissive Corner Bits
                                                emission = 1.4;
                                                color.rgb = vec3(0.45, 1.0, 0.6);
                                            }
                                        }
                                    }

                                }
                            }
                        } else {
                            if (mat < 10568) {
                                if (mat < 10564) { // Lantern
                                    noSmoothLighting = true;
                                    lmCoordM.x = 0.77;

// @included "/lib/materials/specificMaterials/terrain/lanternMetal.glsl"
smoothnessG = color.b;
smoothnessD = color.b;


                                    emission = 4.3 * max0(color.r - color.b);
                                    emission += min(pow2(pow2(0.75 * dot(color.rgb, color.rgb))), 5.0);
                                    color.gb *= pow(vec2(0.8, 0.7), vec2(sqrt(emission) * 0.5));

                                        DoDistantLightBokehMaterial(color, vec4(1.0, 0.6, 0.2, 1.0), emission, 5.0, lViewPos);

                                }
                                else /*if (mat < 10568)*/ { // Soul Lantern
                                    noSmoothLighting = true;
                                    lmCoordM.x = min(lmCoordM.x, 0.77); // consistency748523

// @included "/lib/materials/specificMaterials/terrain/lanternMetal.glsl"
smoothnessG = color.b;
smoothnessD = color.b;


                                    emission = 1.45 * max0(color.g - color.r * 2.0);
                                    emission += 1.17 * min(pow2(pow2(0.55 * dot(color.rgb, color.rgb))), 3.5);

                                        DoDistantLightBokehMaterial(color, vec4(0.5, 1.0, 1.0, 1.0), emission, 3.0, lViewPos);

                                }
                            } else {
                                if (mat < 10572) { // Turtle Egg, Sniffer Egg
                                    smoothnessG = (color.r + color.g) * 0.35;
                                    smoothnessD = (color.r + color.g) * 0.25;
                                }
                                else /*if (mat < 10576)*/ { // Dragon Egg
                                    emission = float(color.b > 0.1) * 10.0 + 1.25;
                                }
                            }
                        }
                    }
                } else {
                    if (mat < 10608) {
                        if (mat < 10592) {
                            if (mat < 10584) {
                                if (mat < 10580) { // Smoker:Lit
                                    lmCoordM.x *= 0.95;

                                    float dotColor = dot(color.rgb, color.rgb);
                                    if (color.r > color.b * 2.0  &&  dotColor > 0.7) {
                                        emission = 2.5 * dotColor;
                                        color.r *= 1.5;
                                    } else {
// @included "/lib/materials/specificMaterials/terrain/cobblestone.glsl"
smoothnessG = dot(color.rgb, color.rgb) * 0.17;
smoothnessD = smoothnessG;
smoothnessG = max(smoothnessG, 0.3 * color.g * float(color.g > color.b * 1.5));
                                    }
                                }
                                else /*if (mat < 10584)*/ { // Blast Furnace:Lit
                                    lmCoordM.x *= 0.95;

                                    float dotColor = dot(color.rgb, color.rgb);
                                    if (color.r > color.b * 2.0  &&  dotColor > 0.7) {
                                        emission = pow2(color.g) * (20.0 - 13.7 * float(color.b > 0.25));
                                        color.r *= 1.5;
                                    } else {
// @included "/lib/materials/specificMaterials/terrain/cobblestone.glsl"
smoothnessG = dot(color.rgb, color.rgb) * 0.17;
smoothnessD = smoothnessG;
smoothnessG = max(smoothnessG, 0.3 * color.g * float(color.g > color.b * 1.5));
                                    }
                                }
                            } else {
                                if (mat < 10588) { // Coal Block
                                    smoothnessG = dot(color.rgb, vec3(0.5));
                                    smoothnessG = min1(smoothnessG);
                                    smoothnessD = smoothnessG;
                                }
                                else /*if (mat < 10592)*/ { // Respawn Anchor:Unlit
                                    noSmoothLighting = true;

// @included "/lib/materials/specificMaterials/terrain/cryingObsidian.glsl"
// @included "/lib/materials/specificMaterials/terrain/obsidian.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = max0(0.3 - abs(color.r - 0.3)) * 1.5;

smoothnessG = factor;
highlightMult = 2.0 + min1(smoothnessG * 2.0) * 1.5;
smoothnessG = min1(smoothnessG);

smoothnessD = min1(factor + 0.07);


highlightMult *= 0.5;

float factor0 = sqrt2(max0(color.b - color.g * 6.0));
float factor1 = pow2(color.b);
emission = 1.35 + pow2(pow2(factor1)) * 7.5;
emission *= factor0;
color.r *= 1.15;

maRecolor = vec3(factor0 * min(max0(factor1 * 0.7 - 0.1) * 1.3, 0.5));
                                    emission += 0.2;

                                }
                            }
                        } else {
                            if (mat < 10600) {
                                if (mat < 10596) { // Respawn Anchor:Lit
                                    noSmoothLighting = true;

// @included "/lib/materials/specificMaterials/terrain/cryingObsidian.glsl"
// @included "/lib/materials/specificMaterials/terrain/obsidian.glsl"
materialMask = OSIEBCA; // Intense Fresnel

float factor = max0(0.3 - abs(color.r - 0.3)) * 1.5;

smoothnessG = factor;
highlightMult = 2.0 + min1(smoothnessG * 2.0) * 1.5;
smoothnessG = min1(smoothnessG);

smoothnessD = min1(factor + 0.07);


highlightMult *= 0.5;

float factor0 = sqrt2(max0(color.b - color.g * 6.0));
float factor1 = pow2(color.b);
emission = 1.35 + pow2(pow2(factor1)) * 7.5;
emission *= factor0;
color.r *= 1.15;

maRecolor = vec3(factor0 * min(max0(factor1 * 0.7 - 0.1) * 1.3, 0.5));

                                    vec2 absCoord = abs(signMidCoordPos);
                                    if (NdotU > 0.9  &&  max(absCoord.x, absCoord.y) < 0.754) { // Portal
                                        highlightMult = 0.0;
                                        smoothnessD = 0.0;
                                        emission = pow2(color.r) * color.r * 16.0;
                                        maRecolor = vec3(0.0);
                                    } else if (color.r + color.g > 1.3) { // Respawn Anchor:Glowstone Part
                                        emission = 4.5 * sqrt3(max0(color.r + color.g - 1.3));
                                    }

                                    emission += 0.3;

                                }
                                else /*if (mat < 10600)*/ { // Redstone Wire:Lit
// @included "/lib/materials/specificMaterials/terrain/redstoneBlock.glsl"
materialMask = OSIEBCA * 5.0; // Redstone Fresnel

float factor = pow2(color.r);
smoothnessG = 0.4;
highlightMult = factor + 0.2;

smoothnessD = factor * 0.5 + 0.1;


                                        emission = pow2(min(color.r, 0.9)) * 4.0;

                                    color.gb *= 0.25;
                                }
                            } else {
                                if (mat < 10604) { // Redstone Wire:Unlit
// @included "/lib/materials/specificMaterials/terrain/redstoneBlock.glsl"
materialMask = OSIEBCA * 5.0; // Redstone Fresnel

float factor = pow2(color.r);
smoothnessG = 0.4;
highlightMult = factor + 0.2;

smoothnessD = factor * 0.5 + 0.1;

                                }
                                else /*if (mat < 10608)*/ { // Redstone Torch
                                    noSmoothLighting = true; noDirectionalShading = true;
                                    lmCoordM.x = min(lmCoordM.x * 0.9, 0.77);

// @included "/lib/materials/specificMaterials/terrain/redstoneTorch.glsl"
if (color.r > 0.65  &&  abs(color.g - color.b) < 0.1) {
    emission = (3.5 - 2.25 * color.g) * 0.97;
    color.rgb *= color.rgb;

} else if (color.r > color.g * 2.0) {
    materialMask = OSIEBCA * 5.0; // Redstone Fresnel

    float factor = pow2(color.r);
    smoothnessG = 0.4;
    highlightMult = factor + 0.4;

    smoothnessD = factor * 0.7 + 0.3;
}
                                    emission += 0.0001; // No light reducing during noon

                                        DoDistantLightBokehMaterial(color, vec4(1.0, 0.0, 0.0, 1.0), emission, 5.0, lViewPos);
                                }
                            }
                        }
                    } else {
                        if (mat < 10624) {
                            if (mat < 10616) {
                                if (mat < 10612) { // Redstone Block
// @included "/lib/materials/specificMaterials/terrain/redstoneBlock.glsl"
materialMask = OSIEBCA * 5.0; // Redstone Fresnel

float factor = pow2(color.r);
smoothnessG = 0.4;
highlightMult = factor + 0.2;

smoothnessD = factor * 0.5 + 0.1;

                                }
                                else /*if (mat < 10616)*/ { // Redstone Ore:Unlit
                                    if (color.r - color.g > 0.2) { // Redstone Ore:Unlit:Redstone Part
// @included "/lib/materials/specificMaterials/terrain/redstoneBlock.glsl"
materialMask = OSIEBCA * 5.0; // Redstone Fresnel

float factor = pow2(color.r);
smoothnessG = 0.4;
highlightMult = factor + 0.2;

smoothnessD = factor * 0.5 + 0.1;

                                    } else { // Redstone Ore:Unlit:Stone Part
// @included "/lib/materials/specificMaterials/terrain/stone.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

                                    }
                                }
                            } else {
                                if (mat < 10620) { // Redstone Ore:Lit
                                    if (color.r - color.g > 0.2) { // Redstone Ore:Lit:Redstone Part
// @included "/lib/materials/specificMaterials/terrain/redstoneBlock.glsl"
materialMask = OSIEBCA * 5.0; // Redstone Fresnel

float factor = pow2(color.r);
smoothnessG = 0.4;
highlightMult = factor + 0.2;

smoothnessD = factor * 0.5 + 0.1;

                                        emission = color.r * pow1_5(color.r) * 4.5;
                                        color.gb *= 0.1;
                                    } else { // Redstone Ore:Lit:Stone Part
// @included "/lib/materials/specificMaterials/terrain/stone.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

                                    }
                                    noSmoothLighting = true;
                                }
                                else /*if (mat < 10624)*/ { // Deepslate Redstone Ore:Unlit
                                    if (color.r - color.g > 0.2) { // Deepslate Redstone Ore:Unlit:Redstone Part
// @included "/lib/materials/specificMaterials/terrain/redstoneBlock.glsl"
materialMask = OSIEBCA * 5.0; // Redstone Fresnel

float factor = pow2(color.r);
smoothnessG = 0.4;
highlightMult = factor + 0.2;

smoothnessD = factor * 0.5 + 0.1;

                                    } else { // Deepslate Redstone Ore:Unlit:Deepslate Part
// @included "/lib/materials/specificMaterials/terrain/deepslate.glsl"
smoothnessG = pow(color.g, 1.5) * 0.55;
smoothnessG = smoothnessG;
smoothnessD = smoothnessG;

materialMask = OSIEBCA; // Intense Fresnel
                                    }
                                }
                            }
                        } else {
                            if (mat < 10632) {
                                if (mat < 10628) { // Deepslate Redstone Ore:Lit
                                    if (color.r - color.g > 0.2) { // Deepslate Redstone Ore:Lit:Redstone Part
// @included "/lib/materials/specificMaterials/terrain/redstoneBlock.glsl"
materialMask = OSIEBCA * 5.0; // Redstone Fresnel

float factor = pow2(color.r);
smoothnessG = 0.4;
highlightMult = factor + 0.2;

smoothnessD = factor * 0.5 + 0.1;

                                        emission = color.r * pow1_5(color.r) * 4.7;
                                        color.gb *= 0.05;
                                    } else { // Deepslate Redstone Ore:Lit:Deepslate Part
// @included "/lib/materials/specificMaterials/terrain/deepslate.glsl"
smoothnessG = pow(color.g, 1.5) * 0.55;
smoothnessG = smoothnessG;
smoothnessD = smoothnessG;

materialMask = OSIEBCA; // Intense Fresnel
                                    }
                                    noSmoothLighting = true;
                                }
                                else /*if (mat < 10632)*/ { // Cave Vines:No Glow Berries
                                    subsurfaceMode = 1;
                                    lmCoordM.x *= 0.875;
                                }
                            } else {
                                if (mat < 10636) { // Cave Vines:With Glow Berries
                                    subsurfaceMode = 1;
                                    lmCoordM.x *= 0.875;

                                    if (color.r > 0.64) {
                                        emission = color.r < 0.75 ? 2.5 : 8.0;
                                        color.rgb = color.rgb * vec3(1.0, 0.8, 0.6);
                                    }
                                }
                                else /*if (mat < 10640)*/ { // Redstone Lamp:Unlit
                                    materialMask = OSIEBCA; // Intense Fresnel
                                    smoothnessG = color.r * 0.5 + 0.2;
                                    float factor = pow2(smoothnessG);
                                    highlightMult = factor * 2.0 + 1.0;
                                    smoothnessD = min1(factor * 2.0);
                                }
                            }
                        }
                    }
                }
            } else {
                if (mat < 10704) {
                    if (mat < 10672) {
                        if (mat < 10656) {
                            if (mat < 10648) {
                                if (mat < 10644) { // Redstone Lamp:Lit
                                    noDirectionalShading = true;
                                    lmCoordM.x = 0.84;

                                    materialMask = OSIEBCA; // Intense Fresnel
                                    smoothnessG = color.r * 0.35 + 0.2;
                                    float factor = pow2(smoothnessG);
                                    highlightMult = factor * 2.0 + 1.0;
                                    smoothnessD = min1(factor * 2.0);

                                    if (color.b > 0.1) {
                                        float dotColor = dot(color.rgb, color.rgb);
                                            emission = dotColor * 1.2;
                                        color.rgb = pow1_5(color.rgb);
                                        maRecolor = vec3(emission * 0.2);
                                    }

                                        DoDistantLightBokehMaterial(emission, 5.0, lViewPos);
                                }
                                else /*if (mat < 10648)*/ { // Repeater, Comparator
                                    noSmoothLighting = true;


                                    vec3 absDif = abs(vec3(color.r - color.g, color.g - color.b, color.r - color.b));
                                    float maxDif = max(absDif.r, max(absDif.g, absDif.b));
                                    if (maxDif > 0.125  ||  color.b > 0.99) { // Redstone Parts
                                        if (color.r < 0.999  &&  color.b > 0.4) color.gb *= 0.5;  // Comparator:Emissive Wire

// @included "/lib/materials/specificMaterials/terrain/redstoneTorch.glsl"
if (color.r > 0.65  &&  abs(color.g - color.b) < 0.1) {
    emission = (3.5 - 2.25 * color.g) * 0.97;
    color.rgb *= color.rgb;

} else if (color.r > color.g * 2.0) {
    materialMask = OSIEBCA * 5.0; // Redstone Fresnel

    float factor = pow2(color.r);
    smoothnessG = 0.4;
    highlightMult = factor + 0.4;

    smoothnessD = factor * 0.7 + 0.3;
}
                                    } else { // Quartz Base
                                        float factor = pow2(color.g) * 0.6;

                                        smoothnessG = factor;
                                        highlightMult = 1.0 + 2.5 * factor;
                                        smoothnessD = factor;
                                    }
                                }
                            } else {
                                if (mat < 10652) { // Shroomlight
                                    noSmoothLighting = true; noDirectionalShading = true;
                                    lmCoordM = vec2(1.0, 0.0);

                                    float dotColor = dot(color.rgb, color.rgb);
                                    emission = min(pow2(pow2(pow2(dotColor * 0.6))), 6.0) * 0.8 + 0.5;

                                        DoDistantLightBokehMaterial(emission, 2.5, lViewPos);
                                }
                                else /*if (mat < 10656)*/ { // Campfire:Lit
                                        vec3 fractPos = fract(playerPos + cameraPosition) - 0.5;
                                        lmCoordM.x = pow2(pow2(smoothstep1(1.0 - 0.4 * dot(fractPos.xz, fractPos.xz))));

                                    float dotColor = dot(color.rgb, color.rgb);
                                    if (color.r > color.b  &&  color.r - color.g < 0.15  &&  dotColor < 1.4) {
// @included "/lib/materials/specificMaterials/terrain/oakWood.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.5;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;
                                    } else if (color.r > color.b  ||  dotColor > 2.9) {
                                        noDirectionalShading = true;
                                        emission = 3.5;
                                        color.rgb *= sqrt1(GetLuminance(color.rgb));
                                    }
                                }
                            }
                        } else {
                            if (mat < 10664) {
                                if (mat < 10660) { // Soul Campfire:Lit
                                        noSmoothLighting = true;

                                    float dotColor = dot(color.rgb, color.rgb);
                                    if (color.r > color.b) {
// @included "/lib/materials/specificMaterials/terrain/oakWood.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.5;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;
                                    } else if (color.g - color.r > 0.1  ||  dotColor > 2.9) {
                                        noDirectionalShading = true;
                                        emission = 2.1;
                                        color.rgb *= sqrt1(GetLuminance(color.rgb));
                                    }

                                }
                                else /*if (mat < 10664)*/ { // Campfire:Unlit, Soul Campfire:Unlit
                                    noSmoothLighting = true;

                                    if (color.r > color.b) {
// @included "/lib/materials/specificMaterials/terrain/oakWood.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.5;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;
                                    }
                                }
                            } else {
                                if (mat < 10668) { // Observer
                                    if (color.r > 0.1  &&  color.g + color.b < 0.1) {
// @included "/lib/materials/specificMaterials/terrain/redstoneTorch.glsl"
if (color.r > 0.65  &&  abs(color.g - color.b) < 0.1) {
    emission = (3.5 - 2.25 * color.g) * 0.97;
    color.rgb *= color.rgb;

} else if (color.r > color.g * 2.0) {
    materialMask = OSIEBCA * 5.0; // Redstone Fresnel

    float factor = pow2(color.r);
    smoothnessG = 0.4;
    highlightMult = factor + 0.4;

    smoothnessD = factor * 0.7 + 0.3;
}
                                    } else {
// @included "/lib/materials/specificMaterials/terrain/cobblestone.glsl"
smoothnessG = dot(color.rgb, color.rgb) * 0.17;
smoothnessD = smoothnessG;
smoothnessG = max(smoothnessG, 0.3 * color.g * float(color.g > color.b * 1.5));
                                    }
                                }
                                else /*if (mat < 10672)*/ { // Wool+, Carpet+ except Lime
                                }
                            }
                        }
                    } else {
                        if (mat < 10688) {
                            if (mat < 10680) {
                                if (mat < 10676) { // Bone Block
                                    smoothnessG = color.r * 0.2;
                                    smoothnessD = smoothnessG;

                                        DoBrightBlockTweaks(color.rgb, 0.5, shadowMult, highlightMult);

                                }
                                else /*if (mat < 10680)*/ { // Barrel, Beehive, Bee Nest, Honeycomb Block
// @included "/lib/materials/specificMaterials/terrain/cobblestone.glsl"
smoothnessG = dot(color.rgb, color.rgb) * 0.17;
smoothnessD = smoothnessG;
smoothnessG = max(smoothnessG, 0.3 * color.g * float(color.g > color.b * 1.5));

                                }
                            } else {
                                if (mat < 10684) { // Ochre Froglight
                                    float frogPow = 8.0;
// @included "/lib/materials/specificMaterials/terrain/froglights.glsl"
noSmoothLighting = true; noDirectionalShading = true;
lmCoordM = vec2(1.0, 0.0);

        float blockRes = max(absMidCoordPos.x * atlasSize.x, 1.0);
        vec2 signMidCoordPosM = abs((floor((signMidCoordPos + 1.0) * blockRes) + 0.5) / blockRes - 1.0);
    float value = 1.0 - max(signMidCoordPosM.x, signMidCoordPosM.y);
emission = 0.3 + value + pow(dot(color.rgb, color.rgb) * 0.33, frogPow);
emission *= 1.7;

    DoDistantLightBokehMaterial(emission, 2.0, lViewPos);

color.rgb = pow2(color.rgb);
                                }
                                else /*if (mat < 10688)*/ { // Verdant Froglight
                                    float frogPow = 16.0;
// @included "/lib/materials/specificMaterials/terrain/froglights.glsl"
noSmoothLighting = true; noDirectionalShading = true;
lmCoordM = vec2(1.0, 0.0);

        float blockRes = max(absMidCoordPos.x * atlasSize.x, 1.0);
        vec2 signMidCoordPosM = abs((floor((signMidCoordPos + 1.0) * blockRes) + 0.5) / blockRes - 1.0);
    float value = 1.0 - max(signMidCoordPosM.x, signMidCoordPosM.y);
emission = 0.3 + value + pow(dot(color.rgb, color.rgb) * 0.33, frogPow);
emission *= 1.7;

    DoDistantLightBokehMaterial(emission, 2.0, lViewPos);

color.rgb = pow2(color.rgb);
                                }
                            }
                        } else {
                            if (mat < 10696) {
                                if (mat < 10692) { // Pearlescent Froglight
                                    float frogPow = 24.0;
// @included "/lib/materials/specificMaterials/terrain/froglights.glsl"
noSmoothLighting = true; noDirectionalShading = true;
lmCoordM = vec2(1.0, 0.0);

        float blockRes = max(absMidCoordPos.x * atlasSize.x, 1.0);
        vec2 signMidCoordPosM = abs((floor((signMidCoordPos + 1.0) * blockRes) + 0.5) / blockRes - 1.0);
    float value = 1.0 - max(signMidCoordPosM.x, signMidCoordPosM.y);
emission = 0.3 + value + pow(dot(color.rgb, color.rgb) * 0.33, frogPow);
emission *= 1.7;

    DoDistantLightBokehMaterial(emission, 2.0, lViewPos);

color.rgb = pow2(color.rgb);
                                }
                                else /*if (mat < 10696)*/ { // Reinforced Deepslate
                                    if (abs(color.r - color.g) < 0.01) { // Reinforced Deepslate:Deepslate Part
// @included "/lib/materials/specificMaterials/terrain/deepslate.glsl"
smoothnessG = pow(color.g, 1.5) * 0.55;
smoothnessG = smoothnessG;
smoothnessD = smoothnessG;

materialMask = OSIEBCA; // Intense Fresnel
                                    } else { // Reinforced Deepslate:Sculk
                                        float boneFactor = max0(color.r * 1.25 - color.b);

                                        if (boneFactor < 0.0001) emission = 0.15;

                                        smoothnessG = min1(boneFactor * 1.7);
                                        smoothnessD = smoothnessG;
                                    }
                                }
                            } else {
                                if (mat < 10700) { // Sculk, Sculk Catalyst, Sculk Vein, Sculk Sensor:Unlit
                                    float boneFactor = max0(color.r * 1.25 - color.b);

                                    if (boneFactor < 0.0001) {
                                        emission = pow2(max0(color.g - color.r)) * 1.7;

                                            vec2 bpos = floor(playerPos.xz + cameraPosition.xz + 0.501)
                                                    + floor(playerPos.y + cameraPosition.y + 0.501);
                                            bpos = bpos * 0.01 + 0.003 * frameTimeCounter;
                                            emission *= pow2(texture2DLod(s_noise, bpos, 0.0).r * pow1_5(texture2DLod(s_noise, bpos * 0.5, 0.0).r));
                                            emission *= 6.0;
                                    }

                                    smoothnessG = min1(boneFactor * 1.7);
                                    smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10704)*/ { // Sculk Shrieker
                                    float boneFactor = max0(color.r * 1.25 - color.b);

                                    if (boneFactor < 0.0001) {
                                        emission = pow2(max0(color.g - color.r)) * 2.0;

                                            vec2 coordFactor = abs(fract(playerPos.xz + cameraPosition.xz) - 0.5);
                                            float coordFactorM = max(coordFactor.x, coordFactor.y);
                                            if (coordFactorM < 0.43) emission += color.g * 7.0;
                                    }

                                    smoothnessG = min1(boneFactor * 1.7);
                                    smoothnessD = smoothnessG;

                                }
                            }
                        }
                    }
                } else {
                    if (mat < 10736) {
                        if (mat < 10720) {
                            if (mat < 10712) {
                                if (mat < 10708) { // Sculk Sensor:Lit
                                    lmCoordM = vec2(0.0, 0.0);
                                    emission = pow2(max0(color.g - color.r)) * 7.0 + 0.7;
                                }
                                else /*if (mat < 10712)*/ { // Spawner
                                    smoothnessG = color.b + 0.2;
                                    smoothnessD = smoothnessG;

                                    emission = 7.0 * float(CheckForColor(color.rgb, vec3(110, 4, 83)));
                                }
                            } else {
                                if (mat < 10716) { // Tuff++
                                    smoothnessG = color.r * 0.4;
                                    smoothnessD = smoothnessG;
                                }
                                else /*if (mat < 10720)*/ { // Clay
                                    highlightMult = 2.0;
                                    smoothnessG = pow2(pow2(color.g)) * 0.5;
                                    smoothnessG = min1(smoothnessG);
                                    smoothnessD = smoothnessG * 0.7;

                                        DoOceanBlockTweaks(smoothnessD);

                                }
                            }
                        } else {
                            if (mat < 10728) {
                                if (mat < 10724) { // Ladder
                                    noSmoothLighting = true;
                                }
                                else /*if (mat < 10728)*/ { // Gravel, Suspicious Gravel
// @included "/lib/materials/specificMaterials/terrain/stone.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;


                                        DoOceanBlockTweaks(smoothnessD);

                                }
                            } else {
                                if (mat < 10732) { // Flower Pot, Potted Stuff:Without Subsurface
                                    noSmoothLighting = true;
                                }
                                else /*if (mat < 10736)*/ { // Potted Stuff:With Subsurface
                                    noSmoothLighting = true;

                                    float NdotE = dot(normalM, eastVec);
                                    if (abs(abs(NdotE) - 0.5) < 0.4) {
                                        subsurfaceMode = 1, noDirectionalShading = true;
                                    }
                                }
                            }
                        }
                    } else {
                        if (mat < 10752) {
                            if (mat < 10744) {
                                if (mat < 10740) { // Structure Block, Jigsaw Block, Test Block, Test Instance Block
                                    float blockRes = absMidCoordPos.x * atlasSize.x;
                                    vec2 signMidCoordPosM = (floor((signMidCoordPos + 1.0) * blockRes) + 0.5) / blockRes - 1.0;
                                    float dotsignMidCoordPos = dot(signMidCoordPosM, signMidCoordPosM);
                                    float lBlockPosM = pow2(max0(1.0 - 1.125 * pow2(dotsignMidCoordPos)));

                                    emission = 2.5 * lBlockPosM + 1.0;
                                    color.rgb = mix(color.rgb, pow2(color.rgb), 0.5);
                                }
                                else /*if (mat < 10744)*/ { // Chain
                                    noSmoothLighting = true;
                                    lmCoordM.x = min(lmCoordM.x, 0.77); // consistency748523

// @included "/lib/materials/specificMaterials/terrain/lanternMetal.glsl"
smoothnessG = color.b;
smoothnessD = color.b;

                                }
                            } else {
                                if (mat < 10748) { // Soul Sand, Soul Soil
                                    smoothnessG = color.r * 0.4;
                                    smoothnessD = color.r * 0.25;
                                }
                                else /*if (mat < 10752)*/ { // Dried Kelp Block
                                    smoothnessG = pow2(color.b) * 0.8;
                                    smoothnessD = smoothnessG;

                                }
                            }
                        } else {
                            if (mat < 10760) {
                                if (mat < 10756) { // Bamboo
                                        vec4 checkColor = color;
                                    if (
                                        CheckForColor(checkColor.rgb, vec3(55, 90, 21))  || 
                                        CheckForColor(checkColor.rgb, vec3(47, 69, 23))  || 
                                        CheckForColor(checkColor.rgb, vec3(82, 137, 25))  || 
                                        CheckForColor(checkColor.rgb, vec3(103, 161, 36))  || 
                                        CheckForColor(checkColor.rgb, vec3(69, 118, 22))  || 
                                        CheckForColor(checkColor.rgb, vec3(43, 62, 22))  || 
                                        CheckForColor(checkColor.rgb, vec3(105, 144, 34))  || 
                                        CheckForColor(checkColor.rgb, vec3(80, 117, 22))  || 
                                        CheckForColor(checkColor.rgb, vec3(148, 187, 79))  || 
                                        CheckForColor(checkColor.rgb, vec3(86, 129, 18))
                                    ) {
                                        subsurfaceMode = 1, noSmoothLighting = true, noDirectionalShading = true;
                                    }
                                    // No further material properties as bamboo jungles are already slow
                                }
                                else /*if (mat < 10760)*/ { // Block of Bamboo, Bamboo Planks++
// @included "/lib/materials/specificMaterials/planks/bambooPlanks.glsl"
smoothnessG = color.r * 0.3;
smoothnessD = color.r * 0.25;
                                }
                            } else {
                                if (mat < 10764) { // Cherry Planks++
// @included "/lib/materials/specificMaterials/planks/cherryPlanks.glsl"
smoothnessG = pow2(pow2(color.g)) * 0.5;
smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10768)*/ { // Cherry Log, Cherry Wood
                                    if (color.g > 0.33) { // Cherry Log:Clean Part
// @included "/lib/materials/specificMaterials/planks/cherryPlanks.glsl"
smoothnessG = pow2(pow2(color.g)) * 0.5;
smoothnessD = smoothnessG;

                                    } else { // Cherry Log:Wood Part, Cherry Wood
                                        smoothnessG = pow2(color.r);
                                        smoothnessD = smoothnessG;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            if (mat < 10896) {
                if (mat < 10832) {
                    if (mat < 10800) {
                        if (mat < 10784) {
                            if (mat < 10776) {
                                if (mat < 10772) { // Torchflower
// @included "/lib/materials/specificMaterials/terrain/torchflower.glsl"
subsurfaceMode = 1, noSmoothLighting = true, noDirectionalShading = true;

    DoFoliageColorTweaks(color.rgb, shadowMult, snowMinNdotU, viewPos, nViewPos, lViewPos, dither);


    emission = (1.0 - abs(signMidCoordPos.x)) * max0(0.7 - abs(signMidCoordPos.y + 0.7));
    emission = pow1_5(emission) * 2.5;
                                }
                                else /*if (mat < 10776)*/ { // Potted Torchflower
                                    noSmoothLighting = true;

                                    float NdotE = dot(normalM, eastVec);
                                    if (abs(abs(NdotE) - 0.5) < 0.4) {
// @included "/lib/materials/specificMaterials/terrain/torchflower.glsl"
subsurfaceMode = 1, noSmoothLighting = true, noDirectionalShading = true;

    DoFoliageColorTweaks(color.rgb, shadowMult, snowMinNdotU, viewPos, nViewPos, lViewPos, dither);


    emission = (1.0 - abs(signMidCoordPos.x)) * max0(0.7 - abs(signMidCoordPos.y + 0.7));
    emission = pow1_5(emission) * 2.5;
                                    }
                                }
                            } else {
                                if (mat < 10780) { // Crimson Fungus, Warped Fungus
                                    noSmoothLighting = true;

                                    if (color.r > 0.91) {
                                        emission = 3.0 * color.g;
                                        color.r *= 1.2;
                                        maRecolor = vec3(0.1);
                                    }
                                }
                                else /*if (mat < 10784)*/ { // Potted Crimson Fungus, Potted Warped Fungus
                                    noSmoothLighting = true;

                                    float NdotE = dot(normalM, eastVec);
                                    if (abs(abs(NdotE) - 0.5) < 0.4) {
                                        if (color.r > 0.91) {
                                            emission = 3.0 * color.g;
                                            color.r *= 1.2;
                                            maRecolor = vec3(0.1);
                                        }
                                    }
                                }
                            }
                        } else {
                            if (mat < 10792) {
                                if (mat < 10788) { // Calibrated Sculk Sensor:Unlit
                                        vec4 checkColor = color;
                                    if (checkColor.r + checkColor.b > checkColor.g * 2.2  ||  checkColor.r > 0.99) { // Amethyst Part
                                                vec2 absCoord = abs(signMidCoordPos);
                                                float maxBlockPos = max(absCoord.x, absCoord.y);
                                                emission = pow2(max0(1.0 - maxBlockPos) * color.g) * 5.4 + 1.2 * color.g;

                                                color.g *= 1.0 - emission * 0.07;
                                                color.rgb *= color.g;

                                    } else { // Sculk Part
                                        float boneFactor = max0(color.r * 1.25 - color.b);

                                        if (boneFactor < 0.0001) emission = pow2(max0(color.g - color.r));

                                        smoothnessG = min1(boneFactor * 1.7);
                                        smoothnessD = smoothnessG;
                                    }

                                }
                                else /*if (mat < 10792)*/ { // Calibrated Sculk Sensor:Lit
                                    lmCoordM = vec2(0.0, 0.0);

                                        vec4 checkColor = color;
                                    if (checkColor.r + checkColor.b > checkColor.g * 2.2  ||  checkColor.r > 0.99) { // Amethyst Part
                                        lmCoordM.x = 1.0;

                                                vec2 absCoord = abs(signMidCoordPos);
                                                float maxBlockPos = max(absCoord.x, absCoord.y);
                                                emission = pow2(max0(1.0 - maxBlockPos) * color.g) * 5.4 + 1.2 * color.g;

                                                color.g *= 1.0 - emission * 0.07;
                                                color.rgb *= color.g;

                                    } else { // Sculk Part
                                        emission = pow2(max0(color.g - color.r)) * 7.0 + 0.7;
                                    }
                                }
                            } else {
                                if (mat < 10796) { // Oak Door
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/planks/oakPlanks.glsl"
smoothnessG = pow2(dot(color.rgb, color.rgb)) * 0.105;
smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10800)*/ { // Spruce Door
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/planks/sprucePlanks.glsl"
smoothnessG = pow2(dot(color.rgb, vec3(0.3)));
smoothnessD = smoothnessG;

                                }
                            }
                        }
                    } else {
                        if (mat < 10816) {
                            if (mat < 10808) {
                                if (mat < 10804) { // Birch Door
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/planks/birchPlanks.glsl"
smoothnessG = pow2(pow2(color.g)) * 0.55;
smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10808)*/ { // Jungle Door
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/planks/junglePlanks.glsl"
smoothnessG = pow2(pow2(pow2(color.g))) * 12.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

                                }
                            } else {
                                if (mat < 10812) { // Acacia Door
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/planks/acaciaPlanks.glsl"
smoothnessG = pow2(pow2(color.r)) * 0.45;
smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10816)*/ { // Dark Oak Door
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/planks/darkOakPlanks.glsl"
smoothnessG = dot(color.rgb, vec3(0.2));
smoothnessD = smoothnessG;

                                }
                            }
                        } else {
                            if (mat < 10824) {
                                if (mat < 10820) { // Mangrove Door
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/planks/mangrovePlanks.glsl"
smoothnessG = pow2(color.r) * 0.5;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;
                                }
                                else /*if (mat < 10824)*/ { // Crimson Door
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/planks/crimsonPlanks.glsl"
smoothnessG = pow2(color.r) * 0.5;
smoothnessD = smoothnessG;

                                }
                            } else {
                                if (mat < 10828) { // Warped Door
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/planks/warpedPlanks.glsl"
smoothnessG = pow2(color.g) * 0.5;
smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10832)*/ { // Bamboo Door
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/planks/bambooPlanks.glsl"
smoothnessG = color.r * 0.3;
smoothnessD = color.r * 0.25;
                                }
                            }
                        }
                    }
                } else {
                    if (mat < 10864) {
                        if (mat < 10848) {
                            if (mat < 10840) {
                                if (mat < 10836) { // Cherry Door
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/planks/cherryPlanks.glsl"
smoothnessG = pow2(pow2(color.g)) * 0.5;
smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10840)*/ { // Brewing Stand
                                        vec3 worldPos = playerPos + cameraPosition;
                                        vec3 fractPos = fract(worldPos.xyz);
                                        vec3 coordM = abs(fractPos.xyz - 0.5);
                                        float cLength = dot(coordM, coordM) * 1.3333333;
                                        cLength = pow2(1.0 - cLength);

                                        if (color.r + color.g > color.b * 3.0  &&  max(coordM.x, coordM.z) < 0.07) {
                                            emission = 2.5 * pow1_5(cLength);
                                        } else {
                                            lmCoordM.x = max(lmCoordM.x * 0.9, cLength);

// @included "/lib/materials/specificMaterials/terrain/cobblestone.glsl"
smoothnessG = dot(color.rgb, color.rgb) * 0.17;
smoothnessD = smoothnessG;
smoothnessG = max(smoothnessG, 0.3 * color.g * float(color.g > color.b * 1.5));
                                        }
                                }
                            } else {
                                if (mat < 10844) { // Lime Concrete
                                    smoothnessG = 0.4;
                                    highlightMult = 1.5;
                                    smoothnessD = 0.3;


                                }
                                else /*if (mat < 10848)*/ { // Lime Carpet, Lime Wool

                                }
                            }
                        } else {
                            if (mat < 10856) {
                                if (mat < 10852) { // Crafter
                                    smoothnessG = pow2(color.b);
                                    smoothnessD = max(smoothnessG, 0.2);

                                    if (color.r > 2.5 * (color.g + color.b)) {
                                        emission = 4.0;
                                        color.rgb *= color.rgb;
                                    }
                                }
                                else /*if (mat < 10856)*/ { // Copper Bulb:BrighterOnes
// @included "/lib/materials/specificMaterials/terrain/copperBulb.glsl"
noSmoothLighting = true;

vec3 hsvColor = rgb2hsv(color.rgb);
if (abs(hsvColor.r - 0.09722) < 0.04305  &&  hsvColor.b > 0.7) { // Active Light Part
    smoothnessG = 0.75;
    smoothnessD = 0.35;

    float blockRes = absMidCoordPos.x * atlasSize.x;
    vec2 signMidCoordPosM = (floor((signMidCoordPos + 1.0) * blockRes) + 0.5) / blockRes - 1.0;
    float dotsignMidCoordPos = dot(signMidCoordPosM, signMidCoordPosM);
    float lBlockPosM = pow2(max0(1.0 - 1.7 * pow2(pow2(dotsignMidCoordPos))));

    emission = pow2(lmCoordM.x) + 0.3 * color.r;
    emission *= (0.7 + 2.0 * pow2(lBlockPosM));
} else if (color.r > 2.5 * (color.g + color.b)) { // Middle Redstone Part
    emission = 4.0;
    color.rgb *= color.rgb;
} else { // Copper Base
// @included "/lib/materials/specificMaterials/terrain/copperBlock.glsl"
materialMask = OSIEBCA * 2.0; // Copper Fresnel
smoothnessG = pow2(pow2(color.r)) + pow2(max0(color.g - color.r * 0.5)) * 0.3;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

color.rgb *= min1(0.6 + 0.7 * GetLuminance(color.rgb));

}
                                }
                            } else {
                                if (mat < 10860) { // Copper Bulb:DimmerOnes
// @included "/lib/materials/specificMaterials/terrain/copperBulb.glsl"
noSmoothLighting = true;

vec3 hsvColor = rgb2hsv(color.rgb);
if (abs(hsvColor.r - 0.09722) < 0.04305  &&  hsvColor.b > 0.7) { // Active Light Part
    smoothnessG = 0.75;
    smoothnessD = 0.35;

    float blockRes = absMidCoordPos.x * atlasSize.x;
    vec2 signMidCoordPosM = (floor((signMidCoordPos + 1.0) * blockRes) + 0.5) / blockRes - 1.0;
    float dotsignMidCoordPos = dot(signMidCoordPosM, signMidCoordPosM);
    float lBlockPosM = pow2(max0(1.0 - 1.7 * pow2(pow2(dotsignMidCoordPos))));

    emission = pow2(lmCoordM.x) + 0.3 * color.r;
    emission *= (0.7 + 2.0 * pow2(lBlockPosM));
} else if (color.r > 2.5 * (color.g + color.b)) { // Middle Redstone Part
    emission = 4.0;
    color.rgb *= color.rgb;
} else { // Copper Base
// @included "/lib/materials/specificMaterials/terrain/copperBlock.glsl"
materialMask = OSIEBCA * 2.0; // Copper Fresnel
smoothnessG = pow2(pow2(color.r)) + pow2(max0(color.g - color.r * 0.5)) * 0.3;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

color.rgb *= min1(0.6 + 0.7 * GetLuminance(color.rgb));

}
                                    emission *= 0.85;
                                }
                                else /*if (mat < 10864)*/ { // Copper Door+
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/terrain/copperBlock.glsl"
materialMask = OSIEBCA * 2.0; // Copper Fresnel
smoothnessG = pow2(pow2(color.r)) + pow2(max0(color.g - color.r * 0.5)) * 0.3;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

color.rgb *= min1(0.6 + 0.7 * GetLuminance(color.rgb));

                                }
                            }
                        }
                    } else {
                        if (mat < 10880) {
                            if (mat < 10872) {
                                if (mat < 10868) { // Copper Trapdoor+
// @included "/lib/materials/specificMaterials/terrain/copperBlock.glsl"
materialMask = OSIEBCA * 2.0; // Copper Fresnel
smoothnessG = pow2(pow2(color.r)) + pow2(max0(color.g - color.r * 0.5)) * 0.3;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

color.rgb *= min1(0.6 + 0.7 * GetLuminance(color.rgb));

                                }
                                else /*if (mat < 10872)*/ { // Trial Spawner:NotOminous:Active, Vault:NotOminous:Active
                                    smoothnessG = max0(color.b - color.r * 0.5);
                                    smoothnessD = smoothnessG;

                                    emission = max0(color.r - color.b) * 3.0;
                                    color.rgb = pow(color.rgb, vec3(1.0 + 0.5 * sqrt(emission)));
                                }
                            } else {
                                if (mat < 10876) { // Trial Spawner:Inactive, Vault:Inactive
                                    smoothnessG = max0(color.b - color.r * 0.5);
                                    smoothnessD = smoothnessG;
                                }
                                else /*if (mat < 10880)*/ { // Trial Spawner:Ominous:Active, Vault:Ominous:Active
                                    float maxComponent = max(max(color.r, color.g), color.b);
                                    float minComponent = min(min(color.r, color.g), color.b);
                                    float saturation = (maxComponent - minComponent) / (1.0 - abs(maxComponent + minComponent - 1.0));

                                    smoothnessG = max0(color.b - pow2(saturation) * 0.5) * 0.5 + 0.1;
                                    smoothnessD = smoothnessG;

                                    emission = saturation > 0.5 ? 4.0 : 0.0;
                                    color.rgb = pow(color.rgb, vec3(1.0 + (0.3 + 0.5 * color.r) * emission));
                                }
                            }
                        } else {
                            if (mat < 10888) {
                                if (mat < 10884) { // ACT - WSR non-solid blocks with no properties

                                }
                                else /*if (mat < 10888)*/ { // Weeping Vines, Twisting Vines
                                    noSmoothLighting = true;


                                    if (color.r > 0.91) {
                                        emission = 3.0 * color.g;
                                        color.r *= 1.2;
                                        maRecolor = vec3(0.1);
                                    }
                                }
                            } else {
                                if (mat < 10892) { // Hay Block
                                    smoothnessG = pow2(color.r) * 0.5;
                                    highlightMult *= 1.5;
                                    smoothnessD = float(color.r > color.g * 2.0) * 0.3;
                                }
                                else /*if (mat < 10896)*/ { // Iron Door
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/terrain/ironBlock.glsl"
    smoothnessG = pow2(pow2(color.r));
highlightMult = smoothnessG * 3.0;
smoothnessD = smoothnessG;
materialMask = OSIEBCA; // Intense Fresnel

color.rgb *= 0.6 + 0.5 * GetLuminance(color.rgb);

                                    color.rgb *= 0.9;
                                }
                            }
                        }
                    }
                }
            } else {
                if (mat < 10960) {
                    if (mat < 10928) {
                        if (mat < 10900) { // Iron Trapdoor
// @included "/lib/materials/specificMaterials/terrain/ironBlock.glsl"
    smoothnessG = pow2(pow2(color.r));
highlightMult = smoothnessG * 3.0;
smoothnessD = smoothnessG;
materialMask = OSIEBCA; // Intense Fresnel

color.rgb *= 0.6 + 0.5 * GetLuminance(color.rgb);

                            color.rgb *= 0.9;
                        }
                        else if (mat < 10924) { // Candles:Lit, Candle Cakes:Lit
// @included "/lib/materials/specificMaterials/terrain/candle.glsl"
noSmoothLighting = true;

color.rgb *= 1.0 + 0.7 * pow2(max(-signMidCoordPos.y + 0.6, float(NdotU > 0.9) * 1.6));

                        }
                        else /*if (mat < 10928)*/ { // Moss Block+, Pale Moss Block+
// @included "/lib/materials/specificMaterials/terrain/cobblestone.glsl"
smoothnessG = dot(color.rgb, color.rgb) * 0.17;
smoothnessD = smoothnessG;
smoothnessG = max(smoothnessG, 0.3 * color.g * float(color.g > color.b * 1.5));
                        }
                    } else {
                        if (mat < 10944) {
                            if (mat < 10936) {
                                if (mat < 10932) { // Pale Oak Planks++
// @included "/lib/materials/specificMaterials/planks/paleOakPlanks.glsl"
smoothnessG = pow2(color.g) * 0.22;
smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10936)*/ { // // Pale Oak Log, Pale Oak Wood
                                    if (color.g > 0.45) { // Pale Oak Log:Clean Part
// @included "/lib/materials/specificMaterials/planks/paleOakPlanks.glsl"
smoothnessG = pow2(color.g) * 0.22;
smoothnessD = smoothnessG;

                                    } else { // Pale Oak Log:Wood Part, Pale Oak Wood
// @included "/lib/materials/specificMaterials/terrain/paleOakWood.glsl"
smoothnessD = pow2(color.g) * 0.7;
smoothnessG = smoothnessD;
                                    }
                                }
                            } else {
                                if (mat < 10940) { // Pale Oak Door
                                    noSmoothLighting = true;
// @included "/lib/materials/specificMaterials/planks/paleOakPlanks.glsl"
smoothnessG = pow2(color.g) * 0.22;
smoothnessD = smoothnessG;

                                }
                                else /*if (mat < 10944)*/ { // Resin++
                                    smoothnessG = color.r * 0.25;
                                    smoothnessD = smoothnessG;
                                }
                            }
                        } else {
                            if (mat < 10952) {
                                if (mat < 10948) { // Creaking Heart: Inactive
// @included "/lib/materials/specificMaterials/terrain/paleOakWood.glsl"
smoothnessD = pow2(color.g) * 0.7;
smoothnessG = smoothnessD;
                                }
                                else /*if (mat < 10952)*/ { // Creaking Heart: Active
// @included "/lib/materials/specificMaterials/terrain/paleOakWood.glsl"
smoothnessD = pow2(color.g) * 0.7;
smoothnessG = smoothnessD;

                                    if (color.r + color.g > color.b * 4.0) {
                                        emission = pow2(color.r) * 2.5 + 0.2;
                                    }
                                }
                            } else {
                                if (mat < 10956) { // Snow: Layers 1 to 7
// @included "/lib/materials/specificMaterials/terrain/snow.glsl"
smoothnessG = (1.0 - pow(color.g, 64.0) * 0.3) * 0.4;
highlightMult = 2.0;

smoothnessD = smoothnessG;

    DoBrightBlockTweaks(color.rgb, 0.5, shadowMult, highlightMult);


                                        float snowFadeOut = 0.0;
                                        snowFadeOut = clamp01((playerPos.y) * 0.1);
                                        snowFadeOut *= clamp01((lViewPos - 64.0) * 0.01);

                                        if (dither + 0.25 < snowFadeOut) discard;
                                }
                                else /*if (mat < 10960)*/ { // Target Block: Inactive
                                    smoothnessG = pow2(color.r) * 0.5;
                                    smoothnessD = smoothnessG * 0.5;
                                }
                            }
                        }
                    }
                } else {
                    if (mat < 10992) {
                        if (mat < 10976) {
                            if (mat < 10968) {
                                if (mat < 10964) { // Target Block: Active
                                    smoothnessG = pow2(color.r) * 0.5;
                                    smoothnessD = smoothnessG * 0.5;

                                    if (color.r > color.g + color.b) {
                                        if (CheckForColor(color.rgb, vec3(220, 74, 74))) {
                                            emission = 5.0;
                                            color.rgb *= mix(vec3(1.0), pow2(color.rgb), 0.9);
                                        } else {
                                            emission = 3.0;
                                            color.rgb *= pow2(color.rgb);
                                        }
                                    }
                                }
                                else /*if (mat < 10968)*/ { // Sponge
                                    smoothnessG = pow2(color.g) * 0.2;
                                    smoothnessD = smoothnessG * 0.5;
                                }
                            } else {
                                if (mat < 10972) { // Wet Sponge
                                    smoothnessG = color.g * 0.75;
                                    highlightMult = 0.3;
                                    smoothnessD = smoothnessG * 0.25;
                                }
                                else /*if (mat < 10976)*/ { // Firefly Bush
                                    subsurfaceMode = 1, noSmoothLighting = true, noDirectionalShading = true;


                                    color.rgb *= 1.5;
                                }
                            }
                        } else {
                            if (mat < 10984) {
                                if (mat < 10980) { // Open Eyeblossom
// @included "/lib/materials/specificMaterials/terrain/openEyeblossom.glsl"
subsurfaceMode = 1, noSmoothLighting = true, noDirectionalShading = true;

    DoFoliageColorTweaks(color.rgb, shadowMult, snowMinNdotU, viewPos, nViewPos, lViewPos, dither);


if (color.r > 0.7  &&  color.r > color.g * 1.2  &&  color.g > color.b * 2.0) { // Emissive Part
    lmCoordM.x = 0.5;
    emission = 5.0 * color.g;
    color.rgb *= color.rgb;
}
                                }
                                else /*if (mat < 10984)*/ { // Potted Open Eyeblossom
                                    noSmoothLighting = true;

                                    float NdotE = dot(normalM, eastVec);
                                    if (abs(abs(NdotE) - 0.5) < 0.4) {
// @included "/lib/materials/specificMaterials/terrain/openEyeblossom.glsl"
subsurfaceMode = 1, noSmoothLighting = true, noDirectionalShading = true;

    DoFoliageColorTweaks(color.rgb, shadowMult, snowMinNdotU, viewPos, nViewPos, lViewPos, dither);


if (color.r > 0.7  &&  color.r > color.g * 1.2  &&  color.g > color.b * 2.0) { // Emissive Part
    lmCoordM.x = 0.5;
    emission = 5.0 * color.g;
    color.rgb *= color.rgb;
}
                                    }
                                }
                            } else {
                                if (mat < 10988) { // Copper Torch
                                    noDirectionalShading = true;

                                    float lum = GetLuminance(color.rgb);

                                    if (color.g - color.r > 0.22  ||  lum > 0.99) {
                                        noSmoothLighting = true;
                                        lmCoordM.x = 1.0;
                                            emission = 2.95;
                                    }

                                        else if (abs(NdotU) < 0.5) {
                                                lmCoordM.x = min1(0.7 + 0.3 * smoothstep1(max0(0.4 - signMidCoordPos.y)));
                                        }

                                        //DoDistantLightBokehMaterial(color, vec4(1.0, 0.6, 0.2, 1.0), emission, 5.0, lViewPos);

                                    emission += 0.0001; // No light reducing during noon
                                }
                                else /*if (mat < 10992)*/ { // Copper Lantern++
                                    noSmoothLighting = true;
                                    lmCoordM.x = 1.0;

                                    float lum = GetLuminance(color.rgb);

// @included "/lib/materials/specificMaterials/terrain/copperBlock.glsl"
materialMask = OSIEBCA * 2.0; // Copper Fresnel
smoothnessG = pow2(pow2(color.r)) + pow2(max0(color.g - color.r * 0.5)) * 0.3;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

color.rgb *= min1(0.6 + 0.7 * GetLuminance(color.rgb));


                                    if (color.g - color.r > 0.2  &&  abs(color.r - color.b) < 0.1  ||  lum > 0.8) {
                                        emission = 4.0 * lum;
                                    }

                                        //DoDistantLightBokehMaterial(color, vec4(1.0, 0.6, 0.2, 1.0), emission, 5.0, lViewPos);

                                }
                            }
                        }
                    } else {
                        if (mat < 11008) {
                            if (mat < 11000) {
                                if (mat < 10996) { // Sulfur+, Potent Sulfur, Sulfur Spike
                                    if (mat == 10995) { // Sulfur Spike
                                        noDirectionalShading = true;
                                    }

                                    smoothnessG = pow2(pow2(max(color.r, max(color.g, color.b)))) * 0.35;
                                    smoothnessD = smoothnessG;
                                }
                                else /*if (mat < 11000)*/ { // Chiseled Sulfur, Polished Sulfur+, Sulfur Bricks+
                                    smoothnessG = pow2(pow2(max(color.r, max(color.g, color.b)))) * 0.8;
                                    smoothnessD = smoothnessG;

                                }
                            } else {
                                if (mat < 11004) { // Cinnabar+
                                    smoothnessG = pow2(pow2(color.r)) * 0.7;
                                    smoothnessD = smoothnessG;
                                }
                                else /*if (mat < 11008)*/ { // Chiseled Cinnabar, Polished Cinnabar+, Cinnabar Bricks+
                                    smoothnessG = pow1_5(color.r) * 0.7;
                                    smoothnessD = smoothnessG;

                                }
                            }
                        } else {
                            if (mat < 11016) {
                                if (mat < 11012) { // Vine, Pale Hanging Moss
                                    subsurfaceMode = 3, centerShadowBias = true; noSmoothLighting = true;


                                    float factor = color.g;
                                    smoothnessG = factor * 0.5;
                                    highlightMult = factor * 4.0 + 2.0;

                                        float fresnel = clamp(1.0 + dot(normalM, normalize(viewPos)), 0.0, 1.0);
                                        highlightMult *= 1.0 - pow2(pow2(fresnel));
                                }
                                else /*if (mat < 11016)*/ { //

                                }
                            } else {
                                if (mat < 11020) { //

                                }
                                else /*if (mat < 11024)*/ { //

                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
}

    else { // mat < 10000
        // Support for the Enhanced Block Entities mod. The mod makes block entities render in gbuffers_terrain
        int blockEntityId = mat;
// @included "/lib/materials/materialHandling/blockEntityIPBR.glsl"
if (blockEntityId < 5028) {
    if (blockEntityId < 5012) {
        if (blockEntityId < 5004) {
            if (blockEntityId == 5000) { //

            } else { // blockEntityId < 5000

            }
        } else {
            if (blockEntityId == 5004) { // Signs
                noSmoothLighting = true;

                if (glColor.r + glColor.g + glColor.b <= 2.99  ||  lmCoord.x > 0.999) { // Sign Text
// @included "/lib/materials/specificMaterials/others/signText.glsl"
normalM = upVec;

highlightMult = 0.0;
shadowMult = vec3(0.0);

color.rgb *= 5.0;
                }

            } else /*if (blockEntityId == 5008)*/ { // Chest
                noSmoothLighting = true;

                smoothnessG = pow2(color.g);

            }
        }
    } else {
        if (blockEntityId < 5020) {
            if (blockEntityId == 5012) { // Ender Chest
                noSmoothLighting = true;

                float factor = min(pow2(color.g), 0.25);
                smoothnessG = factor * 2.0;

                if (color.g > color.r  ||  color.b > color.g)
                    emission = pow2(factor) * 20.0;
                emission += 0.35;

            } else /*if (blockEntityId == 5016)*/ { // Shulker Box+, Head+, Bed+
                noSmoothLighting = true;
            }
        } else {
            if (blockEntityId == 5020) { // Conduit
                noSmoothLighting = true;
                lmCoordM.x = 0.9;

                if (color.b > color.r) { // Conduit:Wind, Conduit:Blue Pixels of The Eye
                    emission = color.r * 16.0;
                } else if (color.r > color.b * 2.5) { // Conduit:Red Pixels of The Eye
                    emission = 20.0;
                    color.rgb *= vec3(1.0, 0.25, 0.1);
                }
            } else /*if (blockEntityId == 5024)*/ { // End Portal, End Gateway
// @included "/lib/materials/specificMaterials/others/endPortalEffect.glsl"
// End Portal fix by fayer3#2332 (Modified)
vec3[8] colors = vec3[](
    vec3(0.3472479, 0.6559956, 0.7387838) * 1.5,
    vec3(0.6010780, 0.7153565, 1.060625 ),
    vec3(0.4221090, 0.8135094, 0.9026056),
    vec3(0.3492291, 1.0241201, 1.8612821),
    vec3(0.7543085, 0.8238697, 0.6803233),
    vec3(0.4144472, 0.5648165, 0.8037   ),
    vec3(0.508905 , 0.6719649, 0.9982805),
    vec3(0.5361914, 0.4476583, 0.8008522));
color.rgb = vec3(0.421, 0.7, 1.6) * 0.14;

float dither = Bayer64(gl_FragCoord.xy);
    dither = fract(dither + goldenRatio * mod(float(frameCounter), 3600.0));
    int repeat = 4;
float dismult = 0.5;
for (int j = 0; j < repeat; j++) {
    float add = float(j + dither) * 0.0625 / float(repeat);
    for (int i = 1; i <= 8; i++) {
        float colormult = 0.9/(30.0+i);
        float rotation = (i - 0.1 * i + 0.71 * i - 11 * i + 21) * 0.01 + i * 0.01;
        float Cos = cos(radians(rotation));
        float Sin = sin(radians(rotation));
        vec2 offset = vec2(0.0, 1.0/(3600.0/24.0)) * pow(16.0 - i, 2.0) * 0.004;

        vec3 wpos = normalize((gbufferModelViewInverse * vec4(viewPos * (i * dismult + 1), 1.0)).xyz);
        if (abs(NdotU) > 0.9) {
            wpos.xz /= wpos.y;
            wpos.xz *= 0.06 * sign(- playerPos.y);
            wpos.xz *= abs(playerPos.y) + i * dismult + add;
            wpos.xz -= cameraPosition.xz * 0.05;
        } else {
            vec3 absPos = abs(playerPos);
            if (abs(dot(normal, eastVec)) > 0.9) {
                wpos.xz = wpos.yz / wpos.x;
                wpos.xz *= 0.06 * sign(- playerPos.x);
                wpos.xz *= abs(playerPos.x) + i * dismult + add;
                wpos.xz -= cameraPosition.yz * 0.05;
            } else {
                wpos.xz = wpos.yx / wpos.z;
                wpos.xz *= 0.06 * sign(- playerPos.z);
                wpos.xz *= abs(playerPos.z) + i * dismult + add;
                wpos.xz -= cameraPosition.yx * 0.05;
            }
        }
        vec2 pos = wpos.xz;

        vec2 wind = fract((frameTimeCounter + 984.0) * (i + 8) * 0.125 * offset);
        vec2 coord = float2x2(Cos, Sin, -Sin, Cos) * pos + wind;
        if (mod(float(i), 4) < 1.5) coord = coord.yx + vec2(-1.0, 1.0) * wind.y;

        vec3 psample = pow(perlinAtlas(float3(coord, jLayer)).rgb, vec3(0.85)) * colors[i-1] * colormult;
        color.rgb += psample * length(psample.rgb) * (3000.0 / repeat);
    }
}
color.rgb *= vec3(0.09, 0.086, 0.06) * 0.9;
emission = 10.0;
noDirectionalShading = true;


            }
        }
    }
} else {
    if (blockEntityId < 5044) {
        if (blockEntityId < 5036) {
            if (blockEntityId == 5028) { // Bell
                if (color.r + color.g > color.b + 0.5) { // Bell:Golden Part
// @included "/lib/materials/specificMaterials/terrain/goldBlock.glsl"
materialMask = OSIEBCA * 3.0; // Gold Fresnel

    float colorG2 = pow2(color.g);
float colorG4 = pow2(colorG2);
float factor = max(color.g, 0.8);

smoothnessG = min1(factor - colorG4 * 0.5);
highlightMult = 3.5 * max(colorG4, 0.2);

smoothnessD = colorG4;

color.rgb *= 0.5 + 0.4 * GetLuminance(color.rgb);

                } else {
// @included "/lib/materials/specificMaterials/terrain/stone.glsl"
smoothnessG = pow2(pow2(color.g)) * 2.0;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

                }
            } else /*if (blockEntityId == 5032)*/ { // Copper Chest+, Copper Golem+
// @included "/lib/materials/specificMaterials/terrain/copperBlock.glsl"
materialMask = OSIEBCA * 2.0; // Copper Fresnel
smoothnessG = pow2(pow2(color.r)) + pow2(max0(color.g - color.r * 0.5)) * 0.3;
smoothnessG = min1(smoothnessG);
smoothnessD = smoothnessG;

color.rgb *= min1(0.6 + 0.7 * GetLuminance(color.rgb));

            }
        } else {
            if (blockEntityId == 5036) { // Banner+
                noSmoothLighting = true;
                noGeneratedNormals = true;
            } else /*if (blockEntityId == 5040)*/ { //

            }
        }
    } else {
        if (blockEntityId < 5052) {
            if (blockEntityId == 5044) { //

            } else /*if (blockEntityId == 5048)*/ { //

            }
        } else {
            if (blockEntityId == 5052) { //

            } else if (blockEntityId == 10548) { // Enchanting Table:Book
                smoothnessG = pow2(color.g) * 0.35;

                if (color.b < 0.0001  &&  color.r > color.g) {
                    emission = color.g * 3.5;
                }
            }
        }
    }
}
    }








    DoLighting(color, shadowMult, playerPos, viewPos, lViewPos, geoNormal, normalM, dither,
               worldGeoNormal, lmCoordM, noSmoothLighting, noDirectionalShading, noVanillaAO,
               centerShadowBias, subsurfaceMode, smoothnessG, highlightMult, emission);

        color.rgb += maRecolor;


    float skyLightFactor = GetSkyLightFactor(lmCoordM, shadowMult);



    /* DRAWBUFFERS:06 */
    gl_FragData0 = color;
    gl_FragData1 = vec4(smoothnessD, materialMask, skyLightFactor, 1.0);

        /* DRAWBUFFERS:064 */
        gl_FragData2 = vec4(mat3(gbufferModelViewInverse) * normalM, 1.0);
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
    output.v_midCoord = vs_stage::midCoord;
    output.v_normal = vs_stage::normal;
    output.v_vertexPos = vs_stage::vertexPos;
    output.v_glColorRaw = vs_stage::glColorRaw;
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
    fs_stage::midCoord = input.v_midCoord;
    fs_stage::normal = input.v_normal;
    fs_stage::vertexPos = input.v_vertexPos;
    fs_stage::glColorRaw = input.v_glColorRaw;
    fs_stage::jLayer = input.v_jLayer;
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
    output.t2 = fs_stage::gl_FragData2;
    return output;
}
