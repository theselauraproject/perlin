#pragma once
#include <chrono>
#include "client/javashaders/BlockStates.hpp"
#include "client/javashaders/PipelineConfig.hpp"
#include "client/javashaders/ProgramSet.hpp"
#include "client/javashaders/RhiBackend.hpp"
#include "client/javashaders/UniformTable.hpp"
#include "client/renderer/Frustum.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/gpu/ChunkMeshStore.hpp"
#include "client/renderer/gpu/GpuCore.hpp"

#include <glm/glm.hpp>

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <initializer_list>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace perlin::gpu {

    constexpr uint32_t kPackVertexBytes = 72;

    constexpr uint32_t kImageSlots = 16, kSsboSlots = 16;

    class JavaPack {
    public:
        const std::shared_ptr<jshader::ShaderPackInstance>& Instance() const { return instance; }
        bool VanillaClouds() const { return config.clouds != "off"; }
        struct Config {
            std::string path;
            jshader::OptionValues options;
            std::string dimension = "world0";
            std::function<std::string(uint16_t)> blockName;
            std::function<int(uint16_t)> blockEmission;
            int shadowResolutionLimit = 0;
            int shadowInterval = 1;
            bool shadowCache = true;
        };

        struct Output {
            conduit::texture_handle color;
            conduit::format colorFormat = conduit::format::bgra8_unorm;
            conduit::texture_handle depth;
            uint32_t width = 0, height = 0;
            float scale = 1.0f;
            conduit::texture_handle lightmap;
            conduit::texture_handle sun, moon, endSky;
        };

        JavaPack(Gpu& gpu, ChunkMeshStore& meshes);
        void DrawRunList(conduit::command_list& list, const std::vector<ChunkMeshStore::Run>& runList, RenderLayer layer);
        ~JavaPack();
        JavaPack(const JavaPack&) = delete;
        JavaPack& operator=(const JavaPack&) = delete;

        bool Load(const Config& config, std::string& error);
        const std::string& Name() const { return name; }
        const std::string& Error() const { return lastError; }

        void SetDimension(const std::string& folder);
        void SetAtlas(const uint8_t* rgba, int tilePx, int tiles);
        void UpdateTile(int tile, const uint8_t* rgba);
        bool HasAtlas() const { return static_cast<bool>(atlas); }

        int WarmUp();

        bool TerrainUsable();

        void GatherGeometry(float camX, float camZ);
        void SetViewFrustum(const Frustum& f) {
            viewFrustum = f;
            haveViewFrustum = true;
        }

        bool Record(conduit::command_list& list, const FrameParams& params, const Output& out);

        void Convert(const Tessellator& tess, std::vector<uint8_t>& out);
        void ClearGeometry();

        static constexpr uint32_t AttribMask();

        struct ExternalDraw {
            enum class Kind : uint8_t {
                Entity,
                EntityTranslucent,
                Eyes,
                Hand,
                HandTranslucent,
                Particle,
                ParticleTranslucent,
                Weather,
                Cracks,
                Block,
                BlockTranslucent,
                Glint,
                HandGlint,
                Clouds,
                Lightning,
                Line,
                Glowing
            };
            Kind kind = Kind::Entity;
            std::vector<uint8_t> vertices;
            uint32_t count = 0;
            conduit::texture_handle texture;
            bool depthTest = true, depthWrite = true;
            bool blend = false, additive = false;
            float entityColor[4] = {0, 0, 0, 0};
            bool castsShadow = false;
            bool shadowOnly = false;
            bool player = false;

            int entityId = 0, blockEntityId = 0, itemId = 0;

            float alphaRef = -1.0f;

            float textureMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            bool glintBlend = false;
            mutable RingSlice ring;
            mutable int ringStamp = -1;
        };

        static void BuildVertices(const UiVertex* vertices, size_t vertexCount, const uint32_t* indices,
                                  size_t indexCount, const float model[16], int blockLight, int skyLight,
                                  std::vector<uint8_t>& out, const int8_t* vertexLight = nullptr,
                                  const float* faceToward = nullptr, bool noSkyLight = false);
        void QueueDraw(ExternalDraw draw) { queued.push_back(std::move(draw)); }

        void SetFluidTiles(int water, int waterFlow, int lava, int lavaFlow) {
            fluidTiles[0] = water;
            fluidTiles[1] = waterFlow;
            fluidTiles[2] = lava;
            fluidTiles[3] = lavaFlow;
        }
        void DropQueued() { queued.clear(); }
        void WriteEngineDepth(conduit::command_list& list, const Output& out) { DepthToEngine(list, out); }

        struct WarmProgress {
            int done = 0, total = 0;
            std::string current;
        };
        WarmProgress Progress() const;
        bool ShadowsEnabled() const { return config.shadowEnabled && shadowDepth; }

        const std::string& CloudSetting() const { return config.clouds; }

    private:
        struct Buffer {
            conduit::texture_handle tex[2];
            int cur = 0;
            bool used = false;
            // Programs write this target through colorimgN; both halves are one storage-capable texture.
            bool asImage = false;
            uint32_t w = 0, h = 0;
        };
        enum class Kind {
            Dummy,
            Color,
            Depth,
            ShadowDepth,
            ShadowColor,
            Noise,
            Atlas,
            Lightmap,
            Normals,
            Specular,
            Overlay,
            Custom,
            Image
        };
        struct SamplerBind {
            int slot = 0;
            Kind kind = Kind::Dummy;
            int index = 0;
            bool shadow = false;
            std::string name;
            std::string customKey;
        };

        struct Image {
            jshader::CustomImage info;
            conduit::texture_handle tex;
            conduit::format format = conduit::format::unknown;
            conduit::texture_dim dim = conduit::texture_dim::tex2d;
            uint32_t w = 1, h = 1, d = 1;
            bool integer = false;
            int colorBuffer = -1;
        };
        struct UniformSlot {
            jshader::UniformDecl decl;
            bool perDraw = false;
            bool known = false;
            bool resolved = false;
            const void* entry = nullptr;
        };
        struct Program {
            conduit::shader_handle vs, fs;
            int regs = 0;
            std::vector<SamplerBind> samplers;
            std::vector<UniformSlot> slots;
            std::vector<int> attachments;
            std::shared_ptr<jshader::ProgramSource> source;
            std::vector<float> frameRegs, drawRegs;
            int frameStamp = -1;
            jshader::PackDrawState lastDraw;
            bool haveDraw = false;
            bool anyPerDraw = false;
            bool valid = false;
            float alphaRef = 0.0f;
            bool debugCopy = false;

            std::vector<int> imageSlots;
            uint32_t imageMask = 0;

            bool compute = false;
            conduit::shader_handle cs;
            conduit::pipeline_handle computePipeline;
            jshader::ComputeResult dispatch;
        };
        struct Converted {
            bool ok = false;
            std::string error;
            jshader::ProgramResult transform;
            jshader::RhiProgram program;
            bool isCompute = false;
            jshader::ComputeResult computeTransform;
            jshader::RhiCompute computeProgram;
        };
        struct PipeState {
            bool scene = true;
            std::array<conduit::format, 8> colors{};
            uint32_t colorCount = 0;
            conduit::format depth = conduit::format::unknown;
            bool depthTest = false, depthWrite = false;
            bool cull = false;
            conduit::compare_op depthOp = conduit::compare_op::less_equal;
            bool blend = false;
            conduit::blend_factor src = conduit::blend_factor::one, dst = conduit::blend_factor::zero,
                             srcA = conduit::blend_factor::one, dstA = conduit::blend_factor::zero;
            conduit::color_mask mask = conduit::color_mask::all;
        };
        struct PackSet {
            std::array<conduit::texture_handle, kTextureSlots> texture{};
            std::array<uint8_t, kTextureSlots> sampler{};
            std::array<conduit::texture_dim, kTextureSlots> dim{};
            bool operator==(const PackSet& o) const {
                return texture == o.texture && sampler == o.sampler && dim == o.dim;
            }
        };
        struct PackSetHash {
            size_t operator()(const PackSet& s) const;
        };

        class Pool {
        public:
            void Start(int n);
            ~Pool();
            std::shared_future<std::shared_ptr<Converted>>
            Submit(std::function<std::shared_ptr<Converted>()> fn);

        private:
            void Loop();
            std::vector<std::thread> threads;
            std::deque<std::function<void()>> jobs;
            std::mutex m;
            std::condition_variable cv;
            bool quit = false;
        };

        std::vector<std::string> ExtraFeatures() const;

        void ScanUsage();
        void CreateStatic();
        bool CreateLayouts();
        bool CreateTargets(uint32_t w, uint32_t h);
        void DestroyTargets();
        void ReleaseAll();
        conduit::texture_handle MakeTexture(uint32_t w, uint32_t h, conduit::format format, conduit::texture_usage usage,
                                       const char* name);
        conduit::sampler_handle SamplerFor(int id);
        void BuildBlockMap();
        int PackBlockId(uint16_t palette);

        int PackEntityId(const std::string& bedrockType);
        int PackItemId(const std::string& bedrockItem);
        int PackBlockIdByName(const std::string& bedrockName);

    public:

        int EntityIdFor(const std::string& bedrockType) { return PackEntityId(bedrockType); }
        int ItemIdFor(const std::string& bedrockItem) { return PackItemId(bedrockItem); }
        int BlockIdFor(const std::string& bedrockName) { return PackBlockIdByName(bedrockName); }

    private:

        std::string KeyFor(const std::string& name, bool toBackbuffer) const;
        Program* GetProgram(const std::string& name, bool toBackbuffer,
                            const jshader::ExpandedSource* vs = nullptr,
                            const jshader::ExpandedSource* fs = nullptr,
                            conduit::format backFormat = conduit::format::unknown, const std::string& alpha = {});

        std::string AlphaFor(const std::string& progName, float defaultRef) const;
        std::string ComputeAlpha(const std::string& progName, float defaultRef) const;
        mutable std::unordered_map<std::string, std::string> alphaCache;
        bool IsFluidBlock(uint16_t palette);
        std::vector<uint8_t> fluidMap;
        const jshader::ProgramConfig* BlendConfigFor(const std::string& progName) const;
        Program* Finish(const std::string& key, const std::string& name, const Converted& c,
                        std::shared_ptr<jshader::ProgramSource> source);
        SamplerBind ResolveSampler(const jshader::RhiSampler& s);
        std::vector<SamplerBind> ResolveAll(const std::string& progName,
                                            const std::vector<jshader::RhiSampler>& samplerList);
        int ChooseSampler(const SamplerBind& s) const;
        std::string MergeHlslSamplers(const std::string& src, const std::vector<SamplerBind>& binds) const;
        conduit::pipeline_handle PipelineFor(Program& p, const PipeState& state);
        void FillUniforms(Program& p, const jshader::PackDrawState& draw, bool perDrawOnly);
        bool BindUniforms(conduit::command_list& list, Program& p, const jshader::PackDrawState& draw);
        conduit::bind_group_handle Textures(conduit::command_list& list, Program& p,
                                      const std::vector<conduit::texture_handle>& attached);
        conduit::bind_group_handle GroupFor(const PackSet& set);

        bool CreateStorage();
        void ClearImages(conduit::command_list& list, bool all);
        conduit::bind_group_handle ImageGroup(uint32_t mask);
        void BindStorage(conduit::command_list& list, const Program& p);
        void StorageBarrier(conduit::command_list& list, const Program& p);
        Program* GetCompute(const std::string& name, size_t index);
        Program* FinishCompute(const std::string& key, const std::string& name, const Converted& c);

        bool RunCompute(conduit::command_list& list, const std::string& name, uint32_t renderW, uint32_t renderH);
        void FillProgramResources(Program& p, const std::string& name,
                                  const std::vector<jshader::RhiSampler>& samplerList,
                                  const std::vector<std::string>& imageNames,
                                  const std::vector<jshader::UniformDecl>& uniforms, int regs);

        void BuildFrame(const FrameParams& params, const Output& out);
        jshader::PackDrawState TerrainDraw() const;
        void BeginPass(conduit::command_list& list, const std::vector<conduit::texture_handle>& colors,
                       conduit::texture_handle depth, uint32_t w, uint32_t h, bool clearDepth,
                       const std::vector<std::array<float, 4>>* clears, const char* name, bool overwrite = false);
        bool RunPass(conduit::command_list& list, const std::string& name, bool toBackbuffer, const Output& out);
        void RunChain(conduit::command_list& list, const char* base);
        void ClearBuffers(conduit::command_list& list, const FrameParams& params);
        void DrawShadows(conduit::command_list& list);
        void DrawSky(conduit::command_list& list);
        void DrawSkyObjects(conduit::command_list& list, const char* program, int stage, conduit::buffer_handle vb,
                            uint32_t first, uint32_t count, conduit::buffer_handle ring, uint64_t ringOffset,
                            const float* modelView, conduit::texture_handle texture, bool additive,
                            const float* colorModulator = nullptr);
        void DrawTranslucent(conduit::command_list& list);
        void DrawTerrain(conduit::command_list& list);
        bool DrawLayers(conduit::command_list& list, const char* program, const std::vector<RenderLayer>& layers,
                        bool translucent, bool shadow, int stage, float alphaRef);
        void DepthToEngine(conduit::command_list& list, const Output& out);
        void CopyDepth(conduit::command_list& list, conduit::texture_handle src, conduit::texture_handle dst);

        Gpu& gpu;
        ChunkMeshStore& meshes;
        std::shared_ptr<jshader::ShaderPackInstance> instance;
        jshader::PipelineConfig config;
        int shadowResPatch = 0;
        int prepassState = -1;
        conduit::shader_handle fragmentOverride;
        bool DepthPrepass(conduit::command_list& list, Program& p, const jshader::PackDrawState& draw,
                          conduit::bind_group_handle textures, conduit::texture_handle depthTarget, uint32_t w, uint32_t h);
        Frustum viewFrustum{};
        bool haveViewFrustum = false;
        glm::vec3 shadowLightWorld{0.0f};
        size_t shadowCulled = 0, shadowKeptChunks = 0;
        std::vector<conduit::texture_handle> staticTextures;
        bool depthTexUsed[3] = {true, true, true};
        int shadowInterval = 1;
        int shadowSkipState = -1;
        bool shadowKept = false, shadowReuse = false;
        bool shadowCacheOn = true, shadowPartial = false, shadowCacheValid = false;
        uint64_t shadowCacheGeneration = 0;
        int shadowCacheAge = 0;
        conduit::texture_handle shadowCacheDepth, shadowCacheColor[2];
        float keptShadowDrawView[16] = {};
        bool EnsureShadowCache();
        glm::mat4 keptShadowRot{1.0f};
        glm::vec3 keptShadowOffset{0.0f};
        glm::dvec3 keptShadowCamera{0.0};
        float keptShadowAngle = 0.0f;
        jshader::UniformTable table;
        jshader::CustomUniforms custom;
        jshader::FrameState frameState;
        std::string name;
        std::string lastError;
        std::string path;
        std::function<std::string(uint16_t)> blockName;
        std::function<int(uint16_t)> blockEmission;
        jshader::BlockIdMatcher blockMatcher;
        std::unordered_map<std::string, int> entityIdMap, itemIdMap, entityIdCache, itemIdCache;
        int fluidIds[2] = {-1, -1};
        std::vector<int16_t> paletteMap;

        Pool pool;
        std::unordered_map<std::string, std::shared_future<std::shared_ptr<Converted>>> pending;
        std::unordered_map<std::string, std::unique_ptr<Program>> programs;
        std::set<std::string> loggedMissing;
        struct PipeKey {
            const void* program = nullptr;
            std::array<uint8_t, 24> bytes{};
            bool operator==(const PipeKey&) const = default;
        };
        struct PipeKeyHash {
            size_t operator()(const PipeKey& k) const {
                uint64_t h = 1469598103934665603ull ^ reinterpret_cast<uintptr_t>(k.program);
                for (uint8_t c : k.bytes)
                    h = (h ^ c) * 1099511628211ull;
                return static_cast<size_t>(h);
            }
        };
        std::unordered_map<PipeKey, conduit::pipeline_handle, PipeKeyHash> pipelines;
        std::unordered_map<PackSet, conduit::bind_group_handle, PackSetHash> groups;

        conduit::bind_layout_handle textureLayout, imageLayout, bufferLayout;
        std::vector<Image> images;
        std::map<std::string, int> imageSlotOf, imageSamplerOf;
        conduit::texture_handle dummyStorage;
        std::array<conduit::buffer_handle, 16> ssbo{};
        conduit::buffer_handle dummySsbo, zeroBuffer;
        uint64_t zeroBytes = 0;
        bool storageFresh = true;
        conduit::bind_group_handle bufferGroup;
        std::unordered_map<uint32_t, conduit::bind_group_handle> imageGroups;
        bool setupDone = false;
        std::vector<ExternalDraw> queued;

        std::array<std::atomic<int>, 4> fluidTiles{-1, -1, -1, -1};
        void DrawExternal(conduit::command_list& list, std::initializer_list<ExternalDraw::Kind> kinds,
                          bool shadowPass);
        conduit::pipeline_layout_handle pipelineLayout;
        std::array<conduit::sampler_handle, 16> samplers{};
        conduit::buffer_handle quad;
        conduit::buffer_handle skyStatic;
        uint32_t starVertexCount = 0;
        conduit::texture_handle textureOverride, skySunTex, skyMoonTex, skyEndTex;

        bool noSkyLight = false;
        bool overrideRepeat = false;
        bool resolvingFullscreen = false;
        uint32_t statPasses = 0, statCopies = 0;
        static constexpr uint32_t kTimerMarks = 96;
        struct GpuTiming {
            bool on = false;
            conduit::query_set_handle queries;
            std::vector<conduit::buffer_handle> readback;
            std::vector<std::vector<std::string>> names;
            uint32_t slot = 0;
            std::map<std::string, double> sum;
            int frames = 0;
            double cpuMs = 0;
            std::map<std::string, double> cpuSum;
            std::chrono::steady_clock::time_point cpuLast;
        } timing;
        void GpuTimesBegin(conduit::command_list& list);
        void GpuMark(conduit::command_list& list, const std::string& name);
        void GpuTimesEnd(conduit::command_list& list);
        static bool IsFullscreenPass(const std::string& progName);

        std::array<Buffer, jshader::kMaxColorTex> bufs;
        conduit::texture_handle depth[3];
        conduit::texture_handle shadowDepth;
        conduit::texture_handle
            shadowDepthNoTranslucents;
        bool shadowCleared = false;
        bool resolvingWaterShadow = false;
        float fogClock = 0.0f, waterVisionTicks = 0.0f;

        struct Feedback {
            conduit::texture_handle tex;
            uint64_t serial = 0;
        };
        std::unordered_map<uint32_t, Feedback> feedback;
        uint64_t passSerial = 1;
        conduit::texture_handle FeedbackCopy(conduit::command_list& list, conduit::texture_handle src);
        mutable std::mutex progressMutex;
        WarmProgress warmProgress;
        conduit::texture_handle shadowColor[2];
        bool shadowColorUsed[2] = {false, false};
        bool shadowUsed = false;
        conduit::texture_handle white, flatNormal, blackTex, noise;
        std::map<std::string, conduit::texture_handle> customTextures;
        std::map<std::string, int> customSamplers;
        int noiseSampler = 2;
        std::string resolvingStage;
        std::vector<conduit::texture_handle> owned;
        uint32_t width = 0, height = 0;
        int frameStamp = 0;

        conduit::texture_handle atlas;
        int atlasTilePx = 16, atlasTiles = 0, gridCols = 1, gridRows = 1, atlasWidth = 0, atlasLevels = 1;

        float prevModelView[16] = {}, prevProjection[16] = {};
        double prevCamera[3] = {0, 0, 0};
        bool havePrev = false;
        float lastTime = 0.0f;

        float smoothAt = 0.0f, wetnessSmooth = 0.0f, eyeSmooth[2] = {240.0f, 240.0f};
        bool smoothStarted = false;

        float shadowView[16] = {}, shadowProjection[16] = {}, shadowDrawView[16] = {};
        float handProjection[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        FrameParams frame{};
        conduit::texture_handle lightmapTex;
        std::array<std::vector<ChunkMeshStore::Run>, kRenderLayerCount> layerRuns;
        std::array<std::vector<ChunkMeshStore::Run>, kRenderLayerCount> shadowRuns;
        bool trace = false;
    };

    constexpr uint32_t JavaPack::AttribMask() {
        return jshader::AttribBit(jshader::kAttribPosition) | jshader::AttribBit(jshader::kAttribColor0) |
               jshader::AttribBit(jshader::kAttribTexCoord0) | jshader::AttribBit(jshader::kAttribTexCoord5) |
               jshader::AttribBit(jshader::kAttribNormal) | jshader::AttribBit(jshader::kAttribTexCoord2) |
               jshader::AttribBit(jshader::kAttribTexCoord3) | jshader::AttribBit(jshader::kAttribTexCoord4) |
               jshader::AttribBit(jshader::kAttribTangent);
    }
}
