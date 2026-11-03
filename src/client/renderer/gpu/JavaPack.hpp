#pragma once
#include <chrono>
#include "client/javashaders/BlockStates.hpp"
#include "client/javashaders/PipelineConfig.hpp"
#include "client/javashaders/ProgramSet.hpp"
#include "client/javashaders/RhiBackend.hpp"
#include "client/javashaders/UniformTable.hpp"
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
    // bytes per vertex of the pack stream; layout in JavaPack.cpp
    constexpr uint32_t kPackVertexBytes = 72;
    // iris allows 16 custom images; storage buffer indices go up to 12
    constexpr uint32_t kImageSlots = 16, kSsboSlots = 16;

    // runs an optifine / iris shader pack on the rhi: gbuffers into the pack's own targets, the begin / shadowcomp / prepare /
    // deferred / composite chains and final, then the pack's depth goes back to the engine for what draws after it
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
        };

        // what the engine hands over each frame
        struct Output {
            rhi::TextureHandle color;
            rhi::Format colorFormat = rhi::Format::BGRA8Unorm;
            rhi::TextureHandle depth;
            uint32_t width = 0, height = 0;
            float scale = 1.0f; // internal resolution against width / height
            rhi::TextureHandle lightmap;
            rhi::TextureHandle sun, moon, endSky;
        };

        JavaPack(Gpu& gpu, ChunkMeshStore& meshes);
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

        // starts shader compilation on worker threads; programs left to compile
        int WarmUp();
        // false when the terrain program failed for good
        bool TerrainUsable();

        // after the mesh store picked what is visible: remembers the index ranges the frame will draw
        void GatherGeometry(float camX, float camZ);

        // one frame. false when it could not be drawn (the caller then draws the normal way)
        bool Record(rhi::CommandList& list, const FrameParams& params, const Output& out);

        // the pack's vertex stream for one chunk mesh (kPackVertexBytes per vertex)
        void Convert(const Tessellator& tess, std::vector<uint8_t>& out);
        void ClearGeometry();

        static constexpr uint32_t AttribMask();

        // what iris draws through the pack besides terrain and sky: mobs, players, the hand, particles, weather,
        // block breaking. the engine hands its meshes over instead of drawing them on top of the pack's picture
        struct ExternalDraw {
            enum class Kind : uint8_t {
                Entity,              // gbuffers_entities
                EntityTranslucent,   // gbuffers_entities_translucent
                Eyes,                // gbuffers_spidereyes (emissive layers)
                Hand,                // gbuffers_hand
                HandTranslucent,     // gbuffers_hand_water
                Particle,            // gbuffers_particles
                ParticleTranslucent, // gbuffers_particles_translucent
                Weather,             // gbuffers_weather
                Cracks,              // gbuffers_damagedblock
                Block,               // gbuffers_block (block entities)
                BlockTranslucent,    // gbuffers_block_translucent (sign text, translucent block entities)
                Glint,               // gbuffers_armor_glint
                HandGlint,           // gbuffers_armor_glint, in the hand pass
                Clouds,              // gbuffers_clouds
                Lightning,           // gbuffers_lightning
                Line,                // gbuffers_line (block outline, hit boxes)
                Glowing              // gbuffers_entities_glowing
            };
            Kind kind = Kind::Entity;
            std::vector<uint8_t> vertices; // pack vertex stream, a plain triangle list
            uint32_t count = 0;
            rhi::TextureHandle texture;
            bool depthTest = true, depthWrite = true;
            bool blend = false, additive = false;
            float entityColor[4] = {0, 0, 0, 0};
            bool castsShadow = false;
            bool shadowOnly = false; // the first person body: only its shadow
            bool player = false;
            // entity.properties / block.properties / item.properties ids
            int entityId = 0, blockEntityId = 0, itemId = 0;
            // the render type's alpha test (iris AlphaTests), < 0 for none
            float alphaRef = -1.0f;
            // iris_TextureMat[0] (glint scrolling)
            float textureMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            bool glintBlend = false; // vanilla glint: src colour, one / zero, one; depth equal
            mutable RingSlice ring;
            mutable int ringStamp = -1;
        };
        // a triangle list in the pack's vertex layout from an engine mesh: positions through `model`, normals and
        // tangents from each triangle, light from the given levels (0-15) or per vertex (two levels each, -1 for
        // the given ones); normals point away from the mesh's middle, or toward `faceToward` (a world point) when
        // given
        static void BuildVertices(const UiVertex* vertices, size_t vertexCount, const uint32_t* indices,
                                  size_t indexCount, const float model[16], int blockLight, int skyLight,
                                  std::vector<uint8_t>& out, const int8_t* vertexLight = nullptr,
                                  const float* faceToward = nullptr, bool noSkyLight = false);
        void QueueDraw(ExternalDraw draw) { queued.push_back(std::move(draw)); }
        // atlas tiles of still / flowing water and lava: fluid quads (waterlogged blocks included) carry the
        // fluid's block id and mc_Entity.y = 1, as iris's fluid renderer gives them
        void SetFluidTiles(int water, int waterFlow, int lava, int lavaFlow) {
            fluidTiles[0] = water;
            fluidTiles[1] = waterFlow;
            fluidTiles[2] = lava;
            fluidTiles[3] = lavaFlow;
        }
        void DropQueued() { queued.clear(); }
        void WriteEngineDepth(rhi::CommandList& list, const Output& out) { DepthToEngine(list, out); }
        // the pack has a shadow pass (iris draws entities into it)
        struct WarmProgress {
            int done = 0, total = 0;
            std::string current; // a program still compiling
        };
        WarmProgress Progress() const;
        bool ShadowsEnabled() const { return config.shadowEnabled && shadowDepth; }
        // shaders.properties clouds: "off", "fast", "fancy" or "default"
        const std::string& CloudSetting() const { return config.clouds; }

    private:
        struct Buffer {
            rhi::TextureHandle tex[2];
            int cur = 0;
            bool used = false;
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
            Image // the sampler of a custom image (image.<name> = <sampler> ...)
        };
        struct SamplerBind {
            int slot = 0;
            Kind kind = Kind::Dummy;
            int index = 0;
            bool shadow = false;
            std::string name;
            std::string customKey; // Kind::Custom: "<stage>:<sampler>"
        };
        // an iris custom image: storage texture in set 2 at its slot, sampled through its sampler name
        struct Image {
            jshader::CustomImage info;
            rhi::TextureHandle tex;
            rhi::Format format = rhi::Format::Unknown;
            rhi::TextureDim dim = rhi::TextureDim::Tex2D;
            uint32_t w = 1, h = 1, d = 1;
            bool integer = false;
        };
        struct UniformSlot {
            jshader::UniformDecl decl;
            bool perDraw = false;
            bool known = false;
            bool resolved = false;
            const void* entry = nullptr;
        };
        struct Program {
            rhi::ShaderHandle vs, fs;
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
            // custom images the program loads / stores (slots in set 2)
            std::vector<int> imageSlots;
            uint32_t imageMask = 0;
            // compute programs (.csh)
            bool compute = false;
            rhi::ShaderHandle cs;
            rhi::PipelineHandle computePipeline;
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
            std::array<rhi::Format, 8> colors{};
            uint32_t colorCount = 0;
            rhi::Format depth = rhi::Format::Unknown;
            bool depthTest = false, depthWrite = false;
            bool cull = false; // back faces of terrain, as vanilla culls them
            rhi::CompareOp depthOp = rhi::CompareOp::LessEqual;
            bool blend = false;
            rhi::BlendFactor src = rhi::BlendFactor::One, dst = rhi::BlendFactor::Zero,
                             srcA = rhi::BlendFactor::One, dstA = rhi::BlendFactor::Zero;
            rhi::ColorMask mask = rhi::ColorMask::All;
        };
        struct PackSet {
            std::array<rhi::TextureHandle, kTextureSlots> texture{};
            std::array<uint8_t, kTextureSlots> sampler{};
            std::array<rhi::TextureDim, kTextureSlots> dim{};
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

        // iris feature flags beyond the base set this device runs (custom images, compute, ssbo)
        std::vector<std::string> ExtraFeatures() const;
        // setup
        void ScanUsage();
        void CreateStatic();
        bool CreateLayouts();
        bool CreateTargets(uint32_t w, uint32_t h);
        void DestroyTargets();
        void ReleaseAll();
        rhi::TextureHandle MakeTexture(uint32_t w, uint32_t h, rhi::Format format, rhi::TextureUsage usage,
                                       const char* name);
        rhi::SamplerHandle SamplerFor(int id);
        void BuildBlockMap();
        int PackBlockId(uint16_t palette);
        // entity.properties / item.properties / block.properties ids (0 when the pack does not list it)
        int PackEntityId(const std::string& bedrockType);
        int PackItemId(const std::string& bedrockItem);
        int PackBlockIdByName(const std::string& bedrockName);

    public:
        // ids for engine draws (0 when the pack does not list them)
        int EntityIdFor(const std::string& bedrockType) { return PackEntityId(bedrockType); }
        int ItemIdFor(const std::string& bedrockItem) { return PackItemId(bedrockItem); }
        int BlockIdFor(const std::string& bedrockName) { return PackBlockIdByName(bedrockName); }

    private:
        // programs
        std::string KeyFor(const std::string& name, bool toBackbuffer) const;
        Program* GetProgram(const std::string& name, bool toBackbuffer,
                            const jshader::ExpandedSource* vs = nullptr,
                            const jshader::ExpandedSource* fs = nullptr,
                            rhi::Format backFormat = rhi::Format::Unknown, const std::string& alpha = {});
        // the alpha test a program runs with: the pack's alphaTest.<program> or iris's for the render type
        // (defaultRef < 0: none)
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
        rhi::PipelineHandle PipelineFor(Program& p, const PipeState& state);
        void FillUniforms(Program& p, const jshader::PackDrawState& draw, bool perDrawOnly);
        bool BindUniforms(rhi::CommandList& list, Program& p, const jshader::PackDrawState& draw);
        rhi::BindGroupHandle Textures(rhi::CommandList& list, Program& p,
                                      const std::vector<rhi::TextureHandle>& attached);
        rhi::BindGroupHandle GroupFor(const PackSet& set);
        // custom images and storage buffers (sets 2 and 3)
        bool CreateStorage();
        void ClearImages(rhi::CommandList& list, bool all);
        rhi::BindGroupHandle ImageGroup(uint32_t mask);
        void BindStorage(rhi::CommandList& list, const Program& p);
        void StorageBarrier(rhi::CommandList& list, const Program& p);
        Program* GetCompute(const std::string& name, size_t index);
        Program* FinishCompute(const std::string& key, const std::string& name, const Converted& c);
        // the compute shaders of a pass, before its fragment program; false while one still compiles
        bool RunCompute(rhi::CommandList& list, const std::string& name, uint32_t renderW, uint32_t renderH);
        void FillProgramResources(Program& p, const std::string& name,
                                  const std::vector<jshader::RhiSampler>& samplerList,
                                  const std::vector<std::string>& imageNames,
                                  const std::vector<jshader::UniformDecl>& uniforms, int regs);

        // frame
        void BuildFrame(const FrameParams& params, const Output& out);
        jshader::PackDrawState TerrainDraw() const;
        void BeginPass(rhi::CommandList& list, const std::vector<rhi::TextureHandle>& colors,
                       rhi::TextureHandle depth, uint32_t w, uint32_t h, bool clearDepth,
                       const std::vector<std::array<float, 4>>* clears, const char* name);
        bool RunPass(rhi::CommandList& list, const std::string& name, bool toBackbuffer, const Output& out);
        void RunChain(rhi::CommandList& list, const char* base);
        void ClearBuffers(rhi::CommandList& list, const FrameParams& params);
        void DrawShadows(rhi::CommandList& list);
        void DrawSky(rhi::CommandList& list);
        void DrawSkyObjects(rhi::CommandList& list, const char* program, int stage, rhi::BufferHandle vb,
                            uint32_t first, uint32_t count, rhi::BufferHandle ring, uint64_t ringOffset,
                            const float* modelView, rhi::TextureHandle texture, bool additive,
                            const float* colorModulator = nullptr);
        void DrawTranslucent(rhi::CommandList& list);
        void DrawTerrain(rhi::CommandList& list);
        bool DrawLayers(rhi::CommandList& list, const char* program, const std::vector<RenderLayer>& layers,
                        bool translucent, bool shadow, int stage, float alphaRef);
        void DepthToEngine(rhi::CommandList& list, const Output& out);
        void CopyDepth(rhi::CommandList& list, rhi::TextureHandle src, rhi::TextureHandle dst);

        Gpu& gpu;
        ChunkMeshStore& meshes;
        std::shared_ptr<jshader::ShaderPackInstance> instance;
        jshader::PipelineConfig config;
        int shadowResPatch = 0;
        bool depthTexUsed[3] = {true, true, true};
        int shadowInterval = 1;
        int shadowSkipState = -1;
        bool shadowKept = false, shadowReuse = false;
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
        int fluidIds[2] = {-1, -1}; // water, lava
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
        std::unordered_map<PipeKey, rhi::PipelineHandle, PipeKeyHash> pipelines;
        std::unordered_map<PackSet, rhi::BindGroupHandle, PackSetHash> groups;

        rhi::BindLayoutHandle textureLayout, imageLayout, bufferLayout;
        std::vector<Image> images;
        std::map<std::string, int> imageSlotOf, imageSamplerOf;
        rhi::TextureHandle dummyStorage;
        std::array<rhi::BufferHandle, 16> ssbo{};
        rhi::BufferHandle dummySsbo, zeroBuffer;
        uint64_t zeroBytes = 0;
        bool storageFresh = true;
        rhi::BindGroupHandle bufferGroup;
        std::unordered_map<uint32_t, rhi::BindGroupHandle> imageGroups;
        bool setupDone = false;
        std::vector<ExternalDraw> queued;
        // read by the mesh threads that convert chunks
        std::array<std::atomic<int>, 4> fluidTiles{-1, -1, -1, -1};
        void DrawExternal(rhi::CommandList& list, std::initializer_list<ExternalDraw::Kind> kinds,
                          bool shadowPass);
        rhi::PipelineLayoutHandle pipelineLayout;
        std::array<rhi::SamplerHandle, 16> samplers{};
        rhi::BufferHandle quad;
        rhi::BufferHandle skyStatic;
        uint32_t starVertexCount = 0;
        rhi::TextureHandle textureOverride, skySunTex, skyMoonTex, skyEndTex;
        // java gives only the overworld skylight: elsewhere every lightmap coordinate has sky 0
        bool noSkyLight = false;
        bool overrideRepeat = false; // the end sky's texture repeats
        bool resolvingFullscreen = false;
        uint32_t statPasses = 0, statCopies = 0;
        static constexpr uint32_t kTimerMarks = 96;
        struct GpuTiming {
            bool on = false;
            rhi::QuerySetHandle queries;
            std::vector<rhi::BufferHandle> readback;
            std::vector<std::vector<std::string>> names;
            uint32_t slot = 0;
            std::map<std::string, double> sum;
            int frames = 0;
            double cpuMs = 0;
            std::map<std::string, double> cpuSum;
            std::chrono::steady_clock::time_point cpuLast;
        } timing;
        void GpuTimesBegin(rhi::CommandList& list);
        void GpuMark(rhi::CommandList& list, const std::string& name);
        void GpuTimesEnd(rhi::CommandList& list);
        static bool IsFullscreenPass(const std::string& progName);

        std::array<Buffer, jshader::kMaxColorTex> bufs;
        rhi::TextureHandle depth[3];
        rhi::TextureHandle shadowDepth;
        rhi::TextureHandle
            shadowDepthNoTranslucents; // shadowtex1: the shadow depth before translucent terrain
        bool shadowCleared = false;
        bool resolvingWaterShadow = false;
        float fogClock = 0.0f, waterVisionTicks = 0.0f;
        // copies of attachments a pass also samples (see FeedbackCopy)
        struct Feedback {
            rhi::TextureHandle tex;
            uint64_t serial = 0;
        };
        std::unordered_map<uint32_t, Feedback> feedback;
        uint64_t passSerial = 1;
        rhi::TextureHandle FeedbackCopy(rhi::CommandList& list, rhi::TextureHandle src);
        mutable std::mutex progressMutex;
        WarmProgress warmProgress;
        rhi::TextureHandle shadowColor[2];
        bool shadowColorUsed[2] = {false, false};
        bool shadowUsed = false;
        rhi::TextureHandle white, flatNormal, blackTex, noise;
        std::map<std::string, rhi::TextureHandle> customTextures; // "<stage>:<sampler>"
        std::map<std::string, int> customSamplers;
        int noiseSampler = 2; // linear repeat
        std::string resolvingStage;
        std::vector<rhi::TextureHandle> owned;
        uint32_t width = 0, height = 0;
        int frameStamp = 0;

        rhi::TextureHandle atlas;
        int atlasTilePx = 16, atlasTiles = 0, gridCols = 1, gridRows = 1, atlasWidth = 0, atlasLevels = 1;

        // camera history for the previous-frame uniforms
        float prevModelView[16] = {}, prevProjection[16] = {};
        double prevCamera[3] = {0, 0, 0};
        bool havePrev = false;
        float lastTime = 0.0f;
        // smoothed uniforms (eyeBrightnessSmooth, wetness)
        float smoothAt = 0.0f, wetnessSmooth = 0.0f, eyeSmooth[2] = {240.0f, 240.0f};
        bool smoothStarted = false;

        // shadow camera of the current frame
        float shadowView[16] = {}, shadowProjection[16] = {}, shadowDrawView[16] = {};
        float handProjection[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        FrameParams frame{};
        rhi::TextureHandle lightmapTex;
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
