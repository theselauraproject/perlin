#pragma once
#include "client/renderer/RenderContext.hpp"
#include "client/renderer/SkyGeometry.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/gpu/ChunkMeshStore.hpp"
#include "client/renderer/gpu/GpuTerrain.hpp"
#include "client/renderer/gpu/RenderTuner.hpp"
#include "client/renderer/gpu/JavaPack.hpp"
#include "client/renderer/gpu/GpuCore.hpp"
#include "client/renderer/gpu/ModGpu.hpp"
#include "client/renderer/gpu/UniformStore.hpp"
#include <conduit/render_profile.hpp>

#include <shared_mutex>
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

    enum class View : uint8_t {
        ShadowBuild,
        ShadowGlass,
        ShadowEntity,
        World,
        // opaque terrain the occlusion pass found newly visible: same camera as World, drawn after the depth pyramid
        WorldLate,
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
        conduit::api api = conduit::api::vulkan;
        uint32_t adapter = 0;
    };

    class RenderContext_rhi : public RenderContext {
    public:
        RenderContext_rhi();
        ~RenderContext_rhi() override;

        bool Initialize(AppPlatform& platform, const Options& options) override;
        void Shutdown() override;
        bool GpuMemoryUsage(uint64_t& dedicated, uint64_t& shared) override;
        std::string RendererName() const override { return rendererName; }
        std::string GpuVendor() const override;
        std::string GpuDescription() const override;
        void SetVsync(bool enabled) override;
        bool ReloadShaders() override;
        bool PackShadowsActive() const override {
            return packActive || javaActive || (PackRunning() && jpack->ShadowsEnabled());
        }
        bool SetJavaShaderPack(const std::string& path, const std::map<std::string, std::string>& options,
                               std::string& error) override;
        void ClearJavaShaderPack() override;
        bool JavaShaderPackActive() const override { return javaActive || jpack != nullptr; }
        bool JavaPackDrawing() const override { return PackRunning(); }
        void PrepareChunkMesh(Tessellator& tess) override {
            std::shared_lock lock(jpackMutex);
            if (!jpack || meshes.ExtensionBytes() == 0)
                return;
            tess.packVertices.clear();
            jpack->Convert(tess, tess.packVertices);
            tess.packStamp = jpackStamp.load();
        }
        JavaPackLoad JavaShaderPackLoad() const override {
            JavaPackLoad l;
            if (!jpack || jpackReady || !jpackError.empty())
                return l;
            const gpu::JavaPack::WarmProgress p = jpack->Progress();
            l.active = true;
            l.done = p.done;
            l.total = p.total;
            l.current = p.current;
            return l;
        }
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
        bool PresentGenerated(int index, int total) override;
        bool CanGenerateFrames() const override {
            return initialized && !offscreen && !fgenBlocked && ProgramIndex(Program::FrameGen) >= 0;
        }
        void RequestScreenshot(const std::string& path) override;
        void SetForcedSize(int w, int h) override {
            forcedW = w;
            forcedH = h;
        }
        void RequestFrameCapture(CaptureSink sink) override;
        bool QueryNativeHandles(NativeHandleInfo& out) const override;

        void UploadCrosshair(const Texture& image) override;
        void DrawHudCursor(float x, float y, float w, float h) override;
        void UploadSkyTextures(const Texture& sun, const Texture& moon, const Texture& clouds) override;
        void UploadEndSkyTexture(const Texture& endSky) override;
        void UploadCustomSkyTextures(const std::vector<Texture>& textures) override;
        void DrawCustomSky(const glm::mat4& view, const glm::mat4& celestial);
        void DrawEndSky(const glm::mat4& view);
        void UploadAtlas(const uint8_t* rgba, int tilePx, int tiles) override;
        bool AppendAtlasTiles(const uint8_t* rgba, int tilePx, int first, int tiles) override;
        void UploadTileFlags(const std::vector<uint8_t>& flags) override;
        int MaxAtlasLayers(int tilePx) const override;
        void UpdateAtlasRegion(int tile, const uint8_t* rgba) override;

        uint32_t UiCreateTextureMipped(int width, int height, const std::vector<const uint8_t*>& levels) override;
        uint32_t UiCreateTexture(int w, int h, const uint8_t* rgba, bool linear,
                                 bool repeat = false) override;
        void UiDestroyTexture(uint32_t texture) override;
        bool UiUpdateTexture(uint32_t texture, int x, int y, int w, int h, const uint8_t* rgba, int rowBytes) override;
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

        bool RequestRendererSwitch(const std::string& id) override;
        bool RendererSwitchPending() const override;
        bool RebuildDevice(const std::string& id);
        std::string ActiveRendererId() const override {
            return initialized ? conduit::api_id(gpu.Api()) : std::string();
        }
        bool InitializeOffscreen(const gpu::GpuSettings& settings, std::span<const uint8_t> pack,
                                 std::string* error = nullptr);
        bool ReadBackFrame(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h);

        gpu::Gpu& Gpu() { return gpu; }
        const gpu::GpuTerrain& Terrain() const { return terrain; }
        // what the cpu path would draw for the opaque layers this frame (benchmarks and tests)
        uint64_t CpuOpaqueIndices();

    private:
        struct DrawCall {
            conduit::pipeline_handle pipeline;
            uint32_t uniformChunk = 0;
            uint32_t uniformOffset = 0;
            conduit::bind_group_handle textures;
            conduit::buffer_handle vertices;
            uint64_t vertexOffset = 0;
            conduit::buffer_handle indices;
            uint64_t indexOffset = 0;
            conduit::index_type indexType = conduit::index_type::u32;
            uint32_t first = 0;
            uint32_t count = 0;
            bool scissor = false;
            conduit::rect rect;
            conduit::buffer_handle indirect;
            uint64_t indirectOffset = 0;
            uint32_t indirectDraws = 0;
            // set: indirectDraws is the most that can run, the gpu-written count here decides
            conduit::buffer_handle countBuffer;
            uint64_t countOffset = 0;
            // ui / entity / particle draws: per-draw values on vertex stream 1 (UiInstance)
            conduit::buffer_handle instances;
            uint64_t instanceOffset = 0;
            bool cullable = false;
            float bounds[4] = {};
        };
        // consecutive ui draws that differ only in mesh and instance become one multi draw; the world ones get their
        // draws culled against the frustum by a compute pass first
        void BatchUiDraws(View view, bool cull);
        void RecordUiCull(conduit::command_list& list);
        bool EnsureUiCull();
        struct UiCullEntry {
            uint32_t command[5];
            uint32_t pad[3];
            float bounds[4];
        };
        struct UiCull {
            conduit::shader_handle shader;
            conduit::bind_layout_handle bindLayout;
            conduit::pipeline_layout_handle layout;
            conduit::pipeline_handle pipeline;
            conduit::buffer_handle output;
            uint32_t capacity = 0;
            std::vector<UiCullEntry> entries;
            bool failed = false;
        } uiCull;
        bool PushRuns(View view, const DrawCall& templ, conduit::buffer_handle vertexBuffer, conduit::buffer_handle indexBuffer,
                      const std::vector<gpu::ChunkMeshStore::Run>& list, size_t begin, size_t end);
        bool UseMultiDraw() const;
        void SavePipelineCache();
        std::vector<uint8_t> pipelineBlob;
        std::string pipelineBlobPath;
        size_t savedPipelineBytes = 0;
        uint64_t pipelineSaveCounter = 0;

        struct DrawState {
            Program program = Program::Ui;
            gpu::VertexLayout layout = gpu::VertexLayout::Ui;
            conduit::cull_mode cull = conduit::cull_mode::none;
            conduit::front_face front = conduit::front_face::clockwise;
            bool depthTest = false;
            bool depthWrite = false;
            conduit::compare_op depthOp = conduit::compare_op::less_equal;
            bool blend = false;
            conduit::blend_factor srcColor = conduit::blend_factor::one, dstColor = conduit::blend_factor::zero;
            conduit::blend_factor srcAlpha = conduit::blend_factor::one, dstAlpha = conduit::blend_factor::zero;
            conduit::color_mask mask = conduit::color_mask::all;
            conduit::blend_op alphaOp = conduit::blend_op::add;
            bool alphaToCoverage = false;
        };

        struct Source {
            conduit::buffer_handle buffer;
            uint64_t offset = 0;
        };

        struct ViewData {
            glm::mat4 view{1.0f};
            glm::mat4 proj{1.0f};
            bool reversedZ = true;
            uint32_t x = 0, y = 0, w = 0, h = 0;
            conduit::format colorFormat = conduit::format::bgra8_unorm;
            conduit::format depthFormat = conduit::format::d32_float;
            uint32_t colorCount = 1;
            conduit::format colorFormat1 = conduit::format::rgba8_unorm;
            conduit::format colorFormat2 = conduit::format::rgba8_unorm;
            std::vector<DrawCall> draws;
        };

        struct Target {
            conduit::texture_handle color;
            conduit::texture_handle depth;
            conduit::texture_handle color1;
            conduit::texture_handle color2;
            uint32_t w = 0, h = 0;
            conduit::format colorFormat = conduit::format::rgba8_unorm;
        };

        struct TexRec {
            conduit::texture_handle handle;
            uint32_t w = 0, h = 0, layers = 1, mips = 1;
            conduit::format format = conduit::format::rgba8_unorm;
            gpu::SamplerKind sampler = gpu::SamplerKind::LinearClamp;
            bool used = false;
            std::vector<uint8_t> backup;
        };

        struct UiPage {
            gpu::Arena vb, ib;
            bool live = false;
        };
        struct UiMeshGpu {
            std::shared_ptr<const std::vector<UiVertex>> cpuVertices;
            std::shared_ptr<const std::vector<uint32_t>> cpuIndices;
            int page = -1;
            uint32_t vFirst = 0, vCount = 0, iFirst = 0, iCount = 0;
            uint32_t generation = 0;
            bool used = false;
            bool transient = false;
            bool lazyRing = false;
            gpu::RingSlice tv, ti;
            // local bounding sphere: centre xyz, radius w
            float bounds[4] = {0, 0, 0, -1};
        };
        bool RouteToJavaPack(UiLayer layer, const UiDraw& draw, const UiMeshGpu& mesh,
                             conduit::texture_handle texture, bool glint);

        struct Stage {
            // Occlusion: depth pyramid from the depth of the pass before it, then the late terrain cull
            enum class Kind : uint8_t { Pass, Copy, Occlusion } kind = Kind::Pass;
            Target target;
            bool toScreen = false, useScreenDepth = false;
            std::vector<View> views;
            bool clearColor = false, clearDepth = false;
            float clear[4] = {0, 0, 0, 1};
            conduit::texture_handle copySrc, copyDst;
            bool copyDepth = false;
            bool discardDepth = false;
            bool overwrite = false;
            uint32_t copyW = 0, copyH = 0;
        };
        std::vector<Stage> stages;

        struct CameraHistory {
            glm::dmat4 view{1.0}, proj{1.0}, prevView{1.0}, prevProj{1.0}, prev2View{1.0};
            glm::dvec3 eye{0.0}, prevEye{0.0};
            bool haveCur = false, havePrev = false, havePrev2 = false;
            bool teleported = false;
            int sizeW = 0, sizeH = 0;
            double time = 0.0, prevTime = 0.0;
        } cam;

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
            bool packUpscale = false;
            bool packSharpen = false;
        } plan;
        struct PostTargets {
            conduit::texture_handle worldColor, worldDepth;
            uint32_t worldW = 0, worldH = 0;
            bool worldSampledDepth = false;
            conduit::texture_handle hist[2];
            int read = 0;
            bool histValid = false;
            float lastTime = 0.0f;
            conduit::texture_handle bloom[4];
            uint32_t bloomW[4] = {}, bloomH[4] = {};
            conduit::texture_handle up[3];
            uint32_t upW = 0, upH = 0;
            int upHistory = 0;
            bool upHistoryValid = false;
            uint32_t frameIndex = 0;
            conduit::texture_handle sceneColor, sceneDepth;
            bool sceneValid = false;
            bool failed = false;
            conduit::texture_handle javaAux;
            conduit::texture_handle javaC1, javaC2, javaC3, javaC4, javaC5, javaC7;
            conduit::texture_handle javaTempA, javaTempB, javaTempC;
            bool javaChainClear = false;
            uint32_t javaChainW = 0, javaChainH = 0;
            conduit::texture_handle packLow;
            uint32_t packLowW = 0, packLowH = 0;
        } post;
        bool PlanPackUpscale(const FrameParams& params);
        void RecordPackUpscale();
        void PlanPost(const FrameParams& params);
        bool EnsureWorldTargets(uint32_t w, uint32_t h, bool sampledDepth, bool history);
        bool EnsureBloomTargets();
        bool EnsureUpscaleTargets();
        bool EnsureSceneTargets();
        void DestroyPostTargets();
        void RecordPost();
        void FullscreenPass(View view, Program program, conduit::format colorFormat, uint32_t w, uint32_t h,
                            bool additive);
        conduit::texture_handle RecordUpscale(conduit::texture_handle source, uint32_t sourceW, uint32_t sourceH);
        void RecordBloom(float strength);
        void RunStage(conduit::command_list& list, const Stage& stage);
        struct FrameGenState {
            conduit::texture_handle final, hudless, depth;
            conduit::texture_handle depthSource;
            uint32_t w = 0, h = 0;
            conduit::format format = conduit::format::unknown;
            bool valid = false;
            bool havePrev = false;
            glm::dmat4 view{1.0}, prevView{1.0}, proj{1.0};
            glm::dvec3 eye{0.0};
        } fgen;
        mutable bool fgenBlocked = false;
        int presentApplied = -1;
        void ApplyPresentMode(bool generating);
        bool EnsureFrameGenTargets(uint32_t w, uint32_t h, conduit::format format);
        void DestroyFrameGenTargets();
        bool FrameGenWanted(conduit::texture_handle image) const;
        void CaptureHudless(conduit::command_list& list, const Target& screen);
        void CaptureFinal(conduit::command_list& list, const Target& screen);
        void ReadyForSampling(conduit::command_list& list, conduit::texture_handle except0, conduit::texture_handle except1);
        std::vector<conduit::texture_handle> postTextures;

        struct ShadowMap {
            conduit::texture_handle color, tint;
            float sample[16] = {}, raster[16] = {}, info[4] = {};
        };
        struct ShadowState {
            bool enabled = false, tintEnabled = false, active = false, entities = false, entityFailed = false;
            bool building = false, fading = false, haveKey = false;
            int size = 0;
            float distance = 80.0f;
            conduit::texture_handle buildDepth, entityColor, entityDepth;
            ShadowMap maps[2];
            int front = -1;
            int64_t key[6] = {};
            int framesSince = 0;
            double lastTime = 0.0, lastBuildAt = -100.0;
            float fadeT = 0.0f, fadeWeight = 0.0f;
            glm::vec3 buildRight{1, 0, 0}, buildUp{0, 1, 0}, buildFwd{0, 0, 1};
            float buildCx = 0, buildCy = 0, buildCz = 0;
            std::vector<std::pair<int64_t, uint8_t>> buildList;
            // opaque casters culled and drawn by the gpu terrain pass; only glass stays in buildList
            bool gpuCasters = false;
            float casterPlanes[6][4] = {};
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

        bool CreateStaticResources();
        void DestroyStaticResources();
        bool CreateScreenTargets();
        void DestroyScreenTargets();
        void ApplySwitchTo(const std::string& id);
        bool StartDevice(const std::string& forced, std::span<const uint8_t> pack, bool strict);

        void PlanViews(const FrameParams& params);
        bool Prepare(View view, const DrawState& state, DrawCall& out);
        void Push(View view, const DrawCall& templ, const Source& vertices, const Source* indices,
                  conduit::index_type indexType, uint32_t first, uint32_t count,
                  const conduit::rect* scissor = nullptr);
        void Submit(View view, const DrawState& state, const Source& vertices, const Source* indices,
                    conduit::index_type indexType, uint32_t first, uint32_t count,
                    const conduit::rect* scissor = nullptr);
        void ApplyMaterial(DrawState& state, const char* material) const;
        DrawState Base(Program program, bool depth, bool depthWrite, conduit::compare_op op) const;
        static void ApplySkyBlend(DrawState& s, uint8_t blend, float brightness, float modulator[4]);
        void SetTransform(const glm::mat4& model);
        void SetMatrix(const float columnMajor[16]);
        void BindTexture(const char* name, conduit::texture_handle texture, gpu::SamplerKind sampler);
        void ResetBindings();
        int ProgramIndex(Program program) const;
        void SetFrameUniforms(const FrameParams& params);
        void ExecuteFrame();
        void Replay(conduit::command_list& list, const std::vector<DrawCall>& draws);
        void FinishReadbacks();
        void ResolveCapture(conduit::command_list& list, const Target& target);
        static void SaveBmp(const std::string& path, uint32_t w, uint32_t h, uint32_t pitch,
                            const uint8_t* bgra);

        UiMeshGpu* LookupUiMesh(uint32_t id);
        uint32_t StoreUiMesh(const UiMeshGpu& mesh);
        void ReleaseUiMeshSlot(uint32_t slot);
        void ReleaseFrameMeshes();
        bool AllocUiMesh(uint32_t vertices, uint32_t indices, UiMeshGpu& out);
        void DestroyUi();

        void DrawSky();
        void DrawClouds();
        void DrawScreenTint();
        void DrawLayer(RenderLayer layer, Program program, View view, bool late = false);
        bool SplitForOcclusion();
        bool OcclusionTarget(uint32_t& w, uint32_t& h) const;
        DrawState PassState(RenderLayer layer) const;
        conduit::cull_mode cullMode = conduit::cull_mode::back;
        conduit::front_face frontFace = conduit::front_face::clockwise;
        std::vector<gpu::ChunkMeshStore::Run> runs;
        gpu::GpuTerrain terrain;
        bool gpuCullFrame = false;
        // measures which gpu-driven features are fastest here and switches them
        gpu::RenderTuner tuner;
        bool tuneWorldFrame = false;
        std::vector<gpu::GpuTerrain::Draw> terrainDraws;
        gpu::PipelineKey KeyFor(const ViewData& view, const DrawState& state) const;

        void ConvertDepth(const float in[16], float out[16]) const;
        static glm::mat4 ReverseDepth(const glm::mat4& clipFromWorld);
        static conduit::compare_op ReverseOp(conduit::compare_op op);
        static bool StageReversed(const Stage& stage);
        void UploadLightmap(const uint8_t* pixels);
        bool MakeTexture(TexRec& rec, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips,
                         conduit::format format, gpu::SamplerKind sampler, const char* name);
        void FreeTexture(TexRec& rec);
        TexRec& AtlasPage(int page) { return page == 0 ? atlas : atlasMore[static_cast<size_t>(page - 1)]; }
        int AtlasLayersFor(int tiles, int tilePx) const;
        bool MakeAtlasPage(int page, int tiles, int tilePx);
        bool GrowAtlasPage(int page, int tiles);
        void FreeAtlas();
        void BindAtlas();

        gpu::Gpu gpu;
        std::unique_ptr<gpu::ModGpu> modGpu;
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
        conduit::api AutoApi();
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
        bool shaderReloadPending = false;
        bool javaActive = false;
        std::string javaPackPath;
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
        bool JavaChainReady() const;
        bool EnsureJavaChainTargets(uint32_t w, uint32_t h);
        void BuildJavaChainStages();
        void RecordJavaChain();
        void JavaChainBinds();

        std::unique_ptr<gpu::JavaPack> jpack;
        mutable std::shared_mutex jpackMutex;
        std::atomic<uint64_t> jpackStamp{1};
        std::string jpackPath, jpackError, jpackDimension = "world0";
        std::map<std::string, std::string> jpackOptions;
        bool jpackReady = false;
        bool jpackGathered = false;
        float jpackScale = 1.0f;
        int jpackUpscaler = 0;
        int jpackShadowQuality = 2;
        conduit::tier deviceTier = conduit::tier::medium;
        int PackShadowLimit() const;
        int PackShadowInterval() const;
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

        conduit::texture_handle screenDepth;
        uint32_t screenDepthW = 0, screenDepthH = 0;

        TexRec atlas, lightmap, sunTex, moonTex, endSkyTex, cloudMapTex, tileFlagsTex, crosshair;
        std::array<TexRec, kAtlasMaxPages - 1> atlasMore;
        int atlasPageCap = kAtlasPageLayers;
        int tileSize = 16;
        std::unordered_map<const uint8_t*, std::vector<std::vector<uint8_t>>> animatedTiles;
        uint8_t lastLightmap[kLightmapBytes] = {};
        bool lightmapValid = false;
        int crosshairPx = 0;
        std::vector<TexRec> uiTextures;
        std::vector<uint8_t> tileFlagData;
        std::vector<uint8_t> atlasSource;
        int atlasTiles = 0;
        Texture sunSource, moonSource, endSkySource, cloudSource, iconSource;

        conduit::buffer_handle topSky, bottomSky, skyBox, skyBoxIdx, screenTri, starVerts, starIdx, sunriseVerts,
            sunriseIdx, sunVerts, moonVerts, quadIdx, fanIdx, customSkyBox, customSkyCube, endSkyBox;
        std::vector<TexRec> customSkyTex;
        // per texture: 24 vertices of the OptiFine box (3x2 cells), then 24 of the Bedrock cube faces, each
        // with uvs pulled inside the texture so filtering never reaches a neighbour cell or the border
        std::vector<conduit::buffer_handle> customSkyVb;
        void FreeCustomSkyVb();
        uint32_t starIndexCount = 0, skyFanIndexCount = 0, sunriseIndexCount = 0;

        CloudTexture cloudTexture;
        gpu::Arena cloudVb, cloudIb;
        uint32_t cloudVertexCapacity = 0, cloudIndexCapacity = 0, cloudIndexCount = 0;
        CloudMesh cloudCpu;
        CloudFrame cloudBuiltFrame;
        bool cloudBuiltFancy = false;
        int cloudBuiltRange = 0;
        bool cloudBuilt = false;
        std::future<CloudMesh> cloudJob;
        bool cloudJobRunning = false;
        CloudFrame cloudJobFrame;

        static constexpr uint32_t kUiMeshSlotBits = 20;
        static constexpr uint32_t kUiMeshSlotMask = (1u << kUiMeshSlotBits) - 1u;
        std::vector<UiPage> uiPages;
        std::vector<UiMeshGpu> uiMeshes;
        std::vector<uint32_t> freeUiMeshes, frameUiMeshes;
        uint32_t staticUiMeshes = 0;
        uint32_t staticUiMeshWarnAt = 2048;

        struct Readback {
            conduit::buffer_handle buffer;
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
