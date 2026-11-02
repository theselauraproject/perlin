#pragma once
#include "client/renderer/RenderContext.hpp"
#include "client/renderer/SkyGeometry.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/gpu/ChunkMeshStore.hpp"
#include "client/renderer/gpu/JavaPack.hpp"
#include "client/renderer/gpu/GpuCore.hpp"
#include "client/renderer/gpu/UniformStore.hpp"

#include <array>
#include <glm/glm.hpp>

#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin {
    // programs the renderer asks for; the pack maps each to a slang program (built-in or shader pack variant)
    enum class Program : uint8_t {
        TerrainSolid,
        TerrainCutout,
        TerrainTranslucent,
        Sky,
        Star,
        Sunrise,
        Celestial,
        Cloud,
        Screen,
        Ui,
        Post,
        Shadow,
        ShadowCutout,
        ShadowGlass,
        SpriteUi,
        EntityShadow,
        Bloom,
        Upscale,
        Temporal,
        FrameGen,
        Entity,
        EntityMask,
        EntityEmissive,
        EntityGlint,
        Particle,
        ParticleCutout,
        Weather,
        Cracks,
        Nametag,
        JavaDeferred,
        JavaComposite,
        JavaComposite1,
        JavaComposite3,
        JavaComposite4,
        JavaComposite5,
        JavaComposite6,
        JavaComposite7,
        JavaFinal,
        Count
    };

    // draw lists, executed in this order
    enum class View : uint8_t {
        ShadowBuild,
        ShadowGlass,
        ShadowEntity,
        World,
        Entity,
        Late,
        Hand,
        Gui,
        Doll,
        Top,
        ExportUi,
        Blur,
        BlurOut,
        Bloom0,
        Bloom1,
        Bloom2,
        Bloom3,
        Bloom4,
        Bloom5,
        Bloom6,
        Up0,
        Up1,
        JavaDeferred,
        JavaComposite,
        JavaComposite1,
        JavaComposite3,
        JavaComposite4,
        JavaComposite5,
        JavaComposite6,
        JavaComposite7,
        JavaFinal,
        Count
    };

    struct RendererChoice {
        rhi::Api api = rhi::Api::Vulkan;
        uint32_t adapter = 0;
    };

    class RenderContext_rhi : public RenderContext {
    public:
        RenderContext_rhi();
        ~RenderContext_rhi() override;

        bool Initialize(AppPlatform& platform, const Options& options) override;
        void Shutdown() override;
        std::string RendererName() const override { return rendererName; }
        std::string GpuVendor() const override;
        std::string GpuDescription() const override;
        void SetVsync(bool enabled) override;
        bool ReloadShaders() override;
        bool PackShadowsActive() const override { return packActive || javaActive; }
        bool SetJavaShaderPack(const std::string& path, const std::map<std::string, std::string>& options,
                               std::string& error) override;
        void ClearJavaShaderPack() override;
        bool JavaShaderPackActive() const override { return javaActive || jpack != nullptr; }
        std::vector<JavaPackOption> JavaShaderPackOptions() const override;
        std::shared_ptr<jshader::ShaderPackInstance> JavaShaderPackInstance() const override {
            return jpack ? jpack->Instance() : nullptr;
        }
        bool JavaPackReady() const { return PackRunning(); }
        std::string JavaShaderPackError() const override { return jpackError; }
        void SetJavaShaderPackDimension(const std::string& folder) override;
        void SetBlockEmissionLookup(std::function<int(uint16_t)> lookup) override {
            blockEmission = std::move(lookup);
        }
        void SetBlockNameLookup(std::function<std::string(uint16_t)> lookup) override {
            blockNames = std::move(lookup);
        }

        void BeginFrame(const FrameParams& params) override;
        void EndFrame() override;
        void RequestScreenshot(const std::string& path) override;
        void SetForcedSize(int w, int h) override {
            forcedW = w;
            forcedH = h;
        }
        void RequestFrameCapture(CaptureSink sink) override;
        bool QueryNativeHandles(NativeHandleInfo& out) const override;

        void UploadCrosshair(const Texture& icons) override;
        void DrawHudCursor(float x, float y, float w, float h) override;
        void UploadSkyTextures(const Texture& sun, const Texture& moon, const Texture& clouds) override;
        void UploadAtlas(const uint8_t* rgba, int tilePx, int tiles) override;
        void UploadTileFlags(const std::vector<uint8_t>& flags) override;
        int MaxAtlasLayers(int tilePx) const override;
        void UpdateAtlasRegion(int tile, const uint8_t* rgba) override;

        uint32_t UiCreateTexture(int w, int h, const uint8_t* rgba, bool linear,
                                 bool repeat = false) override;
        void UiDestroyTexture(uint32_t texture) override;
        uint32_t UiCreateMesh(const UiVertex* vertices, uint32_t vertexCount, const uint32_t* indices,
                              uint32_t indexCount) override;
        uint32_t UiCreateFrameMesh(const UiVertex* vertices, uint32_t vertexCount, const uint32_t* indices,
                                   uint32_t indexCount) override;
        void UiDestroyMesh(uint32_t mesh) override;
        uint32_t UiLiveMeshes() const override { return staticUiMeshes; }
        void UiSubmit(UiLayer layer, const UiDraw& draw) override;
        void FramebufferSize(int& w, int& h) const override {
            w = width;
            h = height;
        }

        void UpdateChunkMesh(int cx, int cz, const Tessellator& tess, uint64_t version) override;
        void RemoveChunkMesh(int cx, int cz) override;
        void UpdateLodTile(int tx, int tz, int size, const Tessellator& tess, uint64_t version) override;
        void RemoveLodTile(int tx, int tz) override;
        void ClearChunkMeshes() override;
        void UnloadLevelResources() override;
        void DrawChunkMeshes(const Frustum& frustum, float camX, float camZ) override;
        void SetChunkCullRadius(int chunks) override { chunkCullRadius = chunks; }
        bool SectionReached(int sx, int sy, int sz) const override {
            return meshes.SectionReached(sx, sy, sz, caveCulling);
        }

        // live renderer switch: applied on the render thread at the start of the next frame, keeping every resource
        bool RequestRendererSwitch(const std::string& id) override;
        bool RendererSwitchPending() const override;
        // tears the device down and rebuilds every resource on it (api switch, or recovery on the same api); between frames only
        bool RebuildDevice(const std::string& id);
        std::string ActiveRendererId() const override {
            return initialized ? rhi::ApiId(gpu.Api()) : std::string();
        }
        // headless init for tests: renders into an owned image instead of a window
        bool InitializeOffscreen(const gpu::GpuSettings& settings, std::span<const uint8_t> pack,
                                 std::string* error = nullptr);
        bool ReadBackFrame(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h);

        gpu::Gpu& Gpu() { return gpu; }

    private:
        struct DrawCall {
            rhi::PipelineHandle pipeline;
            uint32_t uniformChunk = 0;
            uint32_t uniformOffset = 0;
            rhi::BindGroupHandle textures;
            rhi::BufferHandle vertices;
            uint64_t vertexOffset = 0;
            rhi::BufferHandle indices;
            uint64_t indexOffset = 0;
            rhi::IndexType indexType = rhi::IndexType::U32;
            uint32_t first = 0;
            uint32_t count = 0;
            bool scissor = false;
            rhi::Rect rect;
        };

        // everything a draw needs besides its uniforms and textures
        struct DrawState {
            Program program = Program::Ui;
            gpu::VertexLayout layout = gpu::VertexLayout::Ui;
            rhi::CullMode cull = rhi::CullMode::None;
            rhi::FrontFace front = rhi::FrontFace::Clockwise;
            bool depthTest = false;
            bool depthWrite = false;
            rhi::CompareOp depthOp = rhi::CompareOp::LessEqual;
            bool blend = false;
            rhi::BlendFactor srcColor = rhi::BlendFactor::One, dstColor = rhi::BlendFactor::Zero;
            rhi::BlendFactor srcAlpha = rhi::BlendFactor::One, dstAlpha = rhi::BlendFactor::Zero;
            rhi::ColorMask mask = rhi::ColorMask::All;
            rhi::BlendOp alphaOp = rhi::BlendOp::Add;
            bool alphaToCoverage = false;
        };

        struct Source {
            rhi::BufferHandle buffer;
            uint64_t offset = 0;
        };

        struct ViewData {
            glm::mat4 view{1.0f};
            glm::mat4 proj{1.0f};
            // scene passes use reversed depth (1 at the near plane, 0 at the far plane, cleared to 0); shadow maps stay conventional
            bool reversedZ = true;
            uint32_t x = 0, y = 0, w = 0, h = 0;
            // attachments the draws of this view render into; the pipelines are built for them
            rhi::Format colorFormat = rhi::Format::BGRA8Unorm;
            rhi::Format depthFormat = rhi::Format::D32Float;
            // second MRT attachment for java gbuffer passes; 1 keeps single-target
            uint32_t colorCount = 1;
            rhi::Format colorFormat1 = rhi::Format::RGBA8Unorm;
            rhi::Format colorFormat2 = rhi::Format::RGBA8Unorm;
            std::vector<DrawCall> draws;
        };

        struct Target {
            rhi::TextureHandle color;
            rhi::TextureHandle depth;
            // optional second MRT color attachment (java colortex1)
            rhi::TextureHandle color1;
            // optional third MRT color attachment (java deferred colortex4)
            rhi::TextureHandle color2;
            uint32_t w = 0, h = 0;
            rhi::Format colorFormat = rhi::Format::RGBA8Unorm;
        };

        struct TexRec {
            rhi::TextureHandle handle;
            uint32_t w = 0, h = 0, layers = 1, mips = 1;
            rhi::Format format = rhi::Format::RGBA8Unorm;
            gpu::SamplerKind sampler = gpu::SamplerKind::LinearClamp;
            bool used = false;
            // pixels kept only for textures nothing else can regenerate (ui sheets), so a renderer switch can re-upload them
            std::vector<uint8_t> backup;
        };

        // ui meshes live in shared pages; ids carry a generation so a stale id never draws something else
        struct UiPage {
            gpu::Arena vb, ib;
            bool live = false;
        };
        struct UiMeshGpu {
            int page = -1;
            uint32_t vFirst = 0, vCount = 0, iFirst = 0, iCount = 0;
            uint32_t generation = 0;
            bool used = false;
            bool transient = false;
            gpu::RingSlice tv, ti;
        };

        // one step of a frame: a render pass over some views, or a texture copy between passes
        struct Stage {
            enum class Kind : uint8_t { Pass, Copy } kind = Kind::Pass;
            Target target;
            // the swapchain image (with or without the screen depth buffer), known only when the frame is executed
            bool toScreen = false, useScreenDepth = false;
            std::vector<View> views;
            bool clearColor = false, clearDepth = false;
            float clear[4] = {0, 0, 0, 1};
            rhi::TextureHandle copySrc, copyDst;
            bool copyDepth = false;
            uint32_t copyW = 0, copyH = 0;
        };
        std::vector<Stage> stages;

        // frame-to-frame camera state for temporal filters
        struct CameraHistory {
            glm::dmat4 view{1.0}, proj{1.0}, prevView{1.0}, prevProj{1.0}, prev2View{1.0};
            glm::dvec3 eye{0.0}, prevEye{0.0};
            bool haveCur = false, havePrev = false, havePrev2 = false;
            bool teleported = false;
            int sizeW = 0, sizeH = 0;
            double time = 0.0, prevTime = 0.0;
        } cam;

        // what this frame asks of the post chain
        struct PostPlan {
            bool active = false;
            int w = 0, h = 0;
            bool scaled = false;
            bool temporal = false;
            bool bloom = false;
            bool blur = false;
            bool sceneCopy = false;
            float jitter[2] = {0, 0};
            float jitterUv[2] = {0, 0};
        } plan;
        // offscreen targets of the post chain; recreated when the output size or the feature set changes
        struct PostTargets {
            rhi::TextureHandle worldColor, worldDepth;
            uint32_t worldW = 0, worldH = 0;
            bool worldSampledDepth = false;
            rhi::TextureHandle hist[2];
            int read = 0;
            bool histValid = false;
            float lastTime = 0.0f;
            rhi::TextureHandle bloom[4];
            uint32_t bloomW[4] = {}, bloomH[4] = {};
            rhi::TextureHandle up[3];
            uint32_t upW = 0, upH = 0;
            int upHistory = 0;
            bool upHistoryValid = false;
            uint32_t frameIndex = 0;
            rhi::TextureHandle sceneColor, sceneDepth;
            bool sceneValid = false;
            bool failed = false;
            // second MRT attachment for java gbuffer passes (colortex1 content)
            rhi::TextureHandle javaAux;
            // java composite chain ping-pong buffers (colortex1-5,7) and
            // temps; colortex0 is worldColor, colortex6 is javaAux
            rhi::TextureHandle javaC1, javaC2, javaC3, javaC4, javaC5, javaC7;
            rhi::TextureHandle javaTempA, javaTempB, javaTempC;
            bool javaChainClear = false;
            uint32_t javaChainW = 0, javaChainH = 0;
        } post;
        void PlanPost(const FrameParams& params);
        bool EnsureWorldTargets(uint32_t w, uint32_t h, bool sampledDepth, bool history);
        bool EnsureBloomTargets();
        bool EnsureUpscaleTargets();
        bool EnsureSceneTargets();
        void DestroyPostTargets();
        void RecordPost();
        void FullscreenPass(View view, Program program, rhi::Format colorFormat, uint32_t w, uint32_t h,
                            bool additive);
        rhi::TextureHandle RecordUpscale(rhi::TextureHandle source, uint32_t sourceW, uint32_t sourceH);
        void RecordBloom(float strength);
        void RunStage(rhi::CommandList& list, const Stage& stage);
        void ReadyForSampling(rhi::CommandList& list, rhi::TextureHandle except0, rhi::TextureHandle except1);
        std::vector<rhi::TextureHandle> postTextures;

        // sun shadow maps: two are kept so a finished rebuild fades in over the old one
        struct ShadowMap {
            rhi::TextureHandle color, tint;
            float sample[16] = {}, raster[16] = {}, info[4] = {};
        };
        struct ShadowState {
            bool enabled = false, tintEnabled = false, active = false, entities = false, entityFailed = false;
            bool building = false, fading = false, haveKey = false;
            int size = 0;
            float distance = 80.0f;
            rhi::TextureHandle buildDepth, entityColor, entityDepth;
            ShadowMap maps[2];
            int front = -1;
            int64_t key[6] = {};
            int framesSince = 0;
            double lastTime = 0.0, lastBuildAt = -100.0;
            float fadeT = 0.0f, fadeWeight = 0.0f;
            glm::vec3 buildRight{1, 0, 0}, buildUp{0, 1, 0}, buildFwd{0, 0, 1};
            float buildCx = 0, buildCy = 0, buildCz = 0;
            std::vector<std::pair<int64_t, uint8_t>> buildList;
            size_t buildCursor = 0;
            int buildSlice = 0, buildSlices = 1;
            float sampleE[16] = {}, rasterE[16] = {}, infoE[4] = {};
        } shadow;
        std::vector<Stage> shadowStages;
        int shadowQualityBuilt = -1, shadowDistanceBuilt = -1;
        bool shadowConfigDirty = true;
        void InitShadows(int quality, int distance);
        void DestroyShadows();
        bool EnsureEntityShadowMap();
        void RenderShadows(float camX, float camY, float camZ);
        void BindShadowSampling();

        // setup
        bool CreateStaticResources();
        void DestroyStaticResources();
        bool CreateScreenTargets();
        void DestroyScreenTargets();
        void ApplySwitchTo(const std::string& id);
        // picks an adapter and starts the device; strict keeps to the forced api only
        bool StartDevice(const std::string& forced, std::span<const uint8_t> pack, bool strict);

        // frame
        void PlanViews(const FrameParams& params);
        // snapshots uniforms and textures for a draw; Push then adds as many ranges as needed against that snapshot
        bool Prepare(View view, const DrawState& state, DrawCall& out);
        void Push(View view, const DrawCall& templ, const Source& vertices, const Source* indices,
                  rhi::IndexType indexType, uint32_t first, uint32_t count,
                  const rhi::Rect* scissor = nullptr);
        void Submit(View view, const DrawState& state, const Source& vertices, const Source* indices,
                    rhi::IndexType indexType, uint32_t first, uint32_t count,
                    const rhi::Rect* scissor = nullptr);
        void ApplyMaterial(DrawState& state, const char* material) const;
        DrawState Base(Program program, bool depth, bool depthWrite, rhi::CompareOp op) const;
        void SetTransform(const glm::mat4& model);
        void SetMatrix(const float columnMajor[16]);
        void BindTexture(const char* name, rhi::TextureHandle texture, gpu::SamplerKind sampler);
        void ResetBindings();
        int ProgramIndex(Program program) const;
        void SetFrameUniforms(const FrameParams& params);
        void ExecuteFrame();
        void Replay(rhi::CommandList& list, const std::vector<DrawCall>& draws);
        void FinishReadbacks();
        void ResolveCapture(rhi::CommandList& list, const Target& target);
        static void SaveBmp(const std::string& path, uint32_t w, uint32_t h, uint32_t pitch,
                            const uint8_t* bgra);

        // ui
        UiMeshGpu* LookupUiMesh(uint32_t id);
        uint32_t StoreUiMesh(const UiMeshGpu& mesh);
        void ReleaseUiMeshSlot(uint32_t slot);
        void ReleaseFrameMeshes();
        bool AllocUiMesh(uint32_t vertices, uint32_t indices, UiMeshGpu& out);
        void DestroyUi();

        // world
        void DrawSky();
        void DrawClouds();
        void DrawScreenTint();
        void DrawLayer(RenderLayer layer, Program program, View view);
        DrawState PassState(RenderLayer layer) const;
        rhi::CullMode cullMode = rhi::CullMode::Back;
        rhi::FrontFace frontFace = rhi::FrontFace::Clockwise;
        std::vector<gpu::ChunkMeshStore::Run> runs;
        gpu::PipelineKey KeyFor(const ViewData& view, const DrawState& state) const;

        void ConvertDepth(const float in[16], float out[16]) const;
        static glm::mat4 ReverseDepth(const glm::mat4& clipFromWorld);
        static rhi::CompareOp ReverseOp(rhi::CompareOp op);
        static bool StageReversed(const Stage& stage);
        void UploadLightmap(const uint8_t* pixels);
        bool MakeTexture(TexRec& rec, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips,
                         rhi::Format format, gpu::SamplerKind sampler, const char* name);
        void FreeTexture(TexRec& rec);

        // state
        gpu::Gpu gpu;
        gpu::GpuSettings settings;
        gpu::UniformStore uni;
        static constexpr int kBuiltinCount = 7;
        std::array<int, kBuiltinCount> builtinIds = {-1, -1, -1, -1, -1, -1, -1};
        gpu::ChunkMeshStore meshes;
        AppPlatform* platform = nullptr;
        bool initialized = false;
        bool offscreen = false;
        bool vsync = true;
        std::string rendererName, adapterName;
        uint64_t nativeWindow = 0, nativeDisplay = 0;
        std::string requestedSwitch;
        mutable std::mutex switchMutex;
        rhi::Api AutoApi();
        std::vector<uint8_t> packData;

        int width = 0, height = 0;
        int forcedW = 0, forcedH = 0;
        FrameParams frame{};
        float mvp[16] = {};
        float skyMvp[16] = {};
        bool caveCulling = true;
        int chunkCullRadius = 0;
        bool frameOpen = false;
        bool packActive = false;
        // converted java pack drives terrain through the baked java programs
        bool javaActive = false;
        std::string javaPackPath;
        // generated/default java samplers (noise, pbr fallbacks, cleared buffers)
        TexRec javaNoise, javaNormalDef, javaSpecDef, javaBlack, javaCloudWater, javaMaterials;
        struct JavaMaterialRule {
            int id = 0;
            std::vector<std::pair<std::string, std::string>> states;
        };
        std::unordered_map<std::string, std::vector<JavaMaterialRule>> javaMaterialRules;
        size_t javaMaterialCount = 0;
        bool SetBakedJavaShaderPack(const std::string& path,
                                    const std::map<std::string, std::string>& options, std::string& error);
        void ClearBakedJavaShaderPack();
        bool EnsureJavaMaterials();
        bool javaTexReady = false;
        bool EnsureJavaSamplers();
        void FeedJavaUniforms(const FrameParams& params);
        void BindJavaExtras();
        // full java chain (deferred + composite + final); false when any
        // converted program is missing, and only terrain+sky run instead
        bool JavaChainReady() const;
        bool EnsureJavaChainTargets(uint32_t w, uint32_t h);
        void BuildJavaChainStages();
        void RecordJavaChain();
        void JavaChainBinds();

        // java shader pack: loaded as soon as it is set, drawn once its programs have compiled
        std::unique_ptr<gpu::JavaPack> jpack;
        std::string jpackPath, jpackError, jpackDimension = "world0";
        std::map<std::string, std::string> jpackOptions;
        bool jpackReady = false;
        bool jpackGathered = false;
        float jpackScale = 1.0f;
        std::function<std::string(uint16_t)> blockNames;
        std::function<int(uint16_t)> blockEmission;
        bool PackRunning() const { return jpack && jpackReady; }
        bool AttachJavaPack();
        bool ResolvePrograms();
        std::array<int, static_cast<size_t>(Program::Count)> programIds{};
        std::array<gpu::VertexLayout, static_cast<size_t>(Program::Count)> programLayouts{};

        std::array<ViewData, static_cast<size_t>(View::Count)> views;
        glm::mat4 model{1.0f};
        bool modelIdentity = true;
        gpu::TextureSet bound;
        struct UniformCache {
            uint64_t version = 0;
            uint32_t chunk = 0, offset = 0;
            uint64_t frame = 0;
            bool valid = false;
        };
        std::vector<UniformCache> uniformCache;
        uint64_t uniformCacheFrame = 0;

        // targets
        rhi::TextureHandle screenDepth;
        uint32_t screenDepthW = 0, screenDepthH = 0;

        // textures
        TexRec atlas, lightmap, sunTex, moonTex, cloudMapTex, tileFlagsTex, crosshair;
        int atlasLayers = 0;
        int tileSize = 16;
        std::unordered_map<const uint8_t*, std::vector<std::vector<uint8_t>>> animatedTiles;
        uint8_t lastLightmap[kLightmapBytes] = {};
        bool lightmapValid = false;
        int crosshairPx = 0;
        std::vector<TexRec> uiTextures;
        std::vector<uint8_t> tileFlagData;
        // cpu copies that let a renderer switch rebuild the textures on the new device
        std::vector<uint8_t> atlasSource;
        int atlasTiles = 0;
        Texture sunSource, moonSource, cloudSource, iconSource;

        // static geometry
        rhi::BufferHandle topSky, bottomSky, skyBox, skyBoxIdx, screenTri, starVerts, starIdx, sunriseVerts,
            sunriseIdx, sunVerts, moonVerts, quadIdx, fanIdx;
        uint32_t starIndexCount = 0, skyFanIndexCount = 0, sunriseIndexCount = 0;

        // clouds
        CloudTexture cloudTexture;
        gpu::Arena cloudVb, cloudIb;
        uint32_t cloudVertexCapacity = 0, cloudIndexCapacity = 0, cloudIndexCount = 0;
        CloudFrame cloudBuiltFrame;
        bool cloudBuiltFancy = false;
        int cloudBuiltRange = 0;
        bool cloudBuilt = false;
        std::future<CloudMesh> cloudJob;
        bool cloudJobRunning = false;
        CloudFrame cloudJobFrame;

        // ui
        static constexpr uint32_t kUiMeshSlotBits = 20;
        static constexpr uint32_t kUiMeshSlotMask = (1u << kUiMeshSlotBits) - 1u;
        std::vector<UiPage> uiPages;
        std::vector<UiMeshGpu> uiMeshes;
        std::vector<uint32_t> freeUiMeshes, frameUiMeshes;
        uint32_t staticUiMeshes = 0;
        uint32_t staticUiMeshWarnAt = 2048;

        // capture: one readback buffer per frame serves every request made for it
        struct Readback {
            rhi::BufferHandle buffer;
            uint32_t w = 0, h = 0, pitch = 0;
            bool bgra = true;
            uint64_t frame = 0;
            std::vector<CaptureSink> sinks;
            std::vector<std::string> paths;
        };
        std::mutex captureMutex;
        std::deque<CaptureSink> pendingSinks;
        std::vector<std::string> pendingShots;
        std::vector<Readback> readbacks;
    };
}
