#pragma once
#include <functional>
#include "client/renderer/RenderStates.hpp"
#include "client/renderer/Frustum.hpp"
#include "client/renderer/Lightmap.hpp"
#include "client/renderer/SkyGeometry.hpp"
#include "client/resources/TextureLoader.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class AppPlatform;

namespace perlin {
    class Tessellator;
    struct Options;

    struct SkyFrame {
        bool enabled = false;
        float skyColor[3] = {};
        float sunAngle = 0.0f;
        float moonAngle = 0.0f;
        float starAngle = 0.0f;
        float rainBrightness = 1.0f;
        float starBrightness = 0.0f;
        float sunrise[4] = {};
        int moonPhase = 0;
        bool darkDisc = false;
    };

    struct CloudDraw {
        bool enabled = false;
        bool fancy = true;
        int rangeChunks = 128;
        float color[4] = {};
        float height = 192.33f;
        double gameTicks = 0.0;
    };

    struct NativeHandleInfo {
        std::string api, adapter;
        uint64_t window = 0, display = 0, device = 0;
    };

    struct UiVertex {
        float x, y, z;
        uint32_t abgr;
        float u, v;
    };
    static_assert(sizeof(UiVertex) == 24);

    enum class UiLayer : uint8_t { World, WorldLate, Hand, Gui, Doll, Top, Export };

    enum class DrawMaterial : uint8_t { Auto, Entity, Particle, Weather, Cracks, Nametag };

    struct UiDraw {
        DrawMaterial material = DrawMaterial::Auto;
        bool colorMask = false;
        uint32_t mesh = 0;
        uint32_t texture = 0;
        float model[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        bool scissor = false;
        int scissorRect[4] = {0, 0, 0, 0};
        bool premultiplied = true;
        float alphaCutoff = 0.0f;
        bool depth = false;
        bool depthWrite = true;
        uint8_t blend = 0;
        bool customBlend = false;
        BlendFactor blendSrc = BlendFactor::SrcAlpha;
        BlendFactor blendDst = BlendFactor::InvSrcAlpha;
        DepthFunc depthFunc = DepthFunc::LessEqual;
        CullMode cull = CullMode::None;
        bool colorWrite = true;
        bool alphaWrite = true;
        bool alphaToCoverage = false;
        float overlay[4] = {0, 0, 0, 0};
        uint8_t emissive = 0;
        uint32_t glintTexture = 0;
        float glintMatrix[4] = {1, 0, 0, 1};
        float glintOffset[2] = {0, 0};
        float glintStrength = 1.0f;
        bool shadowOnly = false;
    };

    struct FrameParams {
        float viewProjection[16];
        float skyViewProjection[16];
        float cameraPosition[3];
        float fogColor[4];
        float environmentalFogStart;
        float environmentalFogEnd;
        float renderDistanceFogStart;
        float renderDistanceFogEnd;
        float skyFogEnd;
        float cloudFogEnd;
        bool rgss;
        SkyFrame sky;
        CloudDraw cloud;
        float timeSeconds;
        float motionBlur = 0.0f;
        float bloom = 0.0f;
        int shadowQuality = 2;
        int shadowDistance = 0;
        bool shadowEntities = true;
        int cameraMedium = 0;
        float waterSurfaceAbove = 0.0f;
        float menuBlur = 0.0f;
        float renderScale = 1.0f;
        int upscaler = 0;
        float view[16];
        float projection[16];
        int frameGen = 1;
        bool teleported = false;
        float upscaleSharpness = 0.0f;
        bool crosshair = false;
        int guiScale = 0;
        float screenTint[4];
        float portalOverlay = 0.0f;
        uint32_t frameIndex;
        // level clock for java pack uniforms; -1 when the session has none yet
        int worldDay = -1, worldTick = -1;
        float cameraNear = 0.05f, cameraFar = 1024.0f;
        float handProjection[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        int dynMode = 0;
        int dynCount = 0;
        float dynLights[8][4] = {};
        float packSettings[32] = {};
        float heldLight = 0.0f;
        float waterTile = -1.0f;
        float lavaTile = -1.0f;
        float waterFlowTile = -1.0f;
        uint8_t lightmap[kLightmapBytes];
    };

    namespace jshader {
        class ShaderPackInstance;
    }

    struct JavaPackOption {
        std::string name, label, comment, group;
        bool toggle = false;
        std::string current;
        std::vector<std::string> values;
    };

    class RenderContext {
    public:
        virtual ~RenderContext() = default;

        virtual bool Initialize(AppPlatform& platform, const Options& options) = 0;
        virtual void Shutdown() = 0;
        virtual bool ReloadShaders() { return false; }
        virtual bool SetJavaShaderPack(const std::string&, const std::map<std::string, std::string>&,
                                       std::string& error) {
            error = "Java shader packs are not supported by this renderer";
            return false;
        }
        virtual void ClearJavaShaderPack() {}
        virtual bool JavaShaderPackActive() const { return false; }
        virtual std::vector<JavaPackOption> JavaShaderPackOptions() const { return {}; }
        // the running java pack (screens, profiles, lang), null when none is loaded
        virtual std::shared_ptr<jshader::ShaderPackInstance> JavaShaderPackInstance() const { return {}; }
        virtual std::string JavaShaderPackError() const { return {}; }
        virtual void SetJavaShaderPackDimension(const std::string&) {}
        // block name of a palette id, which packs map to their own block ids
        virtual void SetBlockNameLookup(std::function<std::string(uint16_t)>) {}
        // block light a palette id gives off (0-15), for packs that read it from at_midBlock.w
        virtual void SetBlockEmissionLookup(std::function<int(uint16_t)>) {}
        virtual bool PackShadowsActive() const { return false; }
        virtual void SetVsync(bool) {}
        // switches the graphics api while the game keeps running; applied before the next frame, false when unsupported
        virtual bool RequestRendererSwitch(const std::string&) { return false; }
        virtual bool RendererSwitchPending() const { return false; }
        // api id of the running renderer ("vulkan", "d3d12", ...), empty when it has none
        virtual std::string ActiveRendererId() const { return {}; }
        virtual std::string RendererName() const { return {}; }
        virtual std::string GpuVendor() const { return {}; }
        virtual std::string GpuDescription() const { return {}; }

        virtual void BeginFrame(const FrameParams& params) = 0;
        virtual void EndFrame() = 0;
        virtual bool PresentGenerated(int, int) { return false; }
        virtual void RequestScreenshot(const std::string& path) = 0;
        using CaptureSink = std::function<void(uint32_t width, uint32_t height, uint32_t pitch,
                                               const uint8_t* bgra, bool flipY)>;
        virtual void SetForcedSize(int, int) {}
        virtual bool SetExportTarget(bool) { return false; }
        virtual bool QueryNativeHandles(NativeHandleInfo&) const { return false; }
        virtual void RequestFrameCapture(CaptureSink) {}

        virtual void UploadCrosshair(const Texture& icons) = 0;
        virtual void DrawHudCursor(float x, float y, float w, float h) = 0;
        virtual void UploadSkyTextures(const Texture& sun, const Texture& moon, const Texture& clouds) = 0;
        virtual void UploadAtlas(const uint8_t* rgba, int tilePx, int tiles) = 0;
        virtual void UploadTileFlags(const std::vector<uint8_t>&) {}
        virtual int MaxAtlasLayers(int) const { return 2048; }
        virtual void UpdateAtlasRegion(int tile, const uint8_t* rgba) = 0;

        virtual uint32_t UiCreateTexture(int width, int height, const uint8_t* rgba, bool linear,
                                         bool repeat = false) = 0;
        virtual void UiDestroyTexture(uint32_t texture) = 0;
        virtual uint32_t UiCreateMesh(const UiVertex* vertices, uint32_t vertexCount, const uint32_t* indices,
                                      uint32_t indexCount) = 0;
        virtual uint32_t UiCreateFrameMesh(const UiVertex* vertices, uint32_t vertexCount,
                                           const uint32_t* indices, uint32_t indexCount) {
            return UiCreateMesh(vertices, vertexCount, indices, indexCount);
        }
        virtual void UiDestroyMesh(uint32_t mesh) = 0;
        virtual uint32_t UiLiveMeshes() const { return 0; }
        virtual void UiSubmit(UiLayer layer, const UiDraw& draw) = 0;
        virtual void FramebufferSize(int& width, int& height) const = 0;

        virtual void UpdateChunkMesh(int cx, int cz, const Tessellator& tess, uint64_t version) = 0;
        virtual void RemoveChunkMesh(int cx, int cz) = 0;
        virtual void UpdateLodTile(int tx, int tz, int size, const Tessellator& tess, uint64_t version) = 0;
        virtual void RemoveLodTile(int tx, int tz) = 0;
        virtual void ClearChunkMeshes() = 0;
        virtual void UnloadLevelResources() { ClearChunkMeshes(); }
        virtual void DrawChunkMeshes(const Frustum& frustum, float camX, float camZ) = 0;
        virtual void SetChunkCullRadius(int) {}
        // section (16-block cube) reachable from the camera through open space, last frame
        virtual bool SectionReached(int sx, int sy, int sz) const { return true; }
    };
}
