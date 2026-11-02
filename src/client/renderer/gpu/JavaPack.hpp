#pragma once
#include "client/javashaders/PipelineConfig.hpp"
#include "client/javashaders/ProgramSet.hpp"
#include "client/javashaders/RhiBackend.hpp"
#include "client/javashaders/UniformTable.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/gpu/ChunkMeshStore.hpp"
#include "client/renderer/gpu/GpuCore.hpp"

#include <array>
#include <condition_variable>
#include <deque>
#include <functional>
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
        };

        // what the engine hands over each frame
        struct Output {
            rhi::TextureHandle color;
            rhi::Format colorFormat = rhi::Format::BGRA8Unorm;
            rhi::TextureHandle depth;
            uint32_t width = 0, height = 0;
            float scale = 1.0f; // internal resolution against width / height
            rhi::TextureHandle lightmap;
            rhi::TextureHandle sun, moon;
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
        // programs
        std::string KeyFor(const std::string& name, bool toBackbuffer) const;
        Program* GetProgram(const std::string& name, bool toBackbuffer,
                            const jshader::ExpandedSource* vs = nullptr,
                            const jshader::ExpandedSource* fs = nullptr,
                            rhi::Format backFormat = rhi::Format::Unknown);
        Program* Finish(const std::string& key, const std::string& name, const Converted& c,
                        std::shared_ptr<jshader::ProgramSource> source);
        SamplerBind ResolveSampler(const jshader::RhiSampler& s);
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
                            const float* modelView, rhi::TextureHandle texture, bool additive);
        void DrawTranslucent(rhi::CommandList& list);
        void DrawTerrain(rhi::CommandList& list);
        bool DrawLayers(rhi::CommandList& list, const char* program, const std::vector<RenderLayer>& layers,
                        bool translucent, bool shadow);
        void DepthToEngine(rhi::CommandList& list, const Output& out);
        void CopyDepth(rhi::CommandList& list, rhi::TextureHandle src, rhi::TextureHandle dst);

        Gpu& gpu;
        ChunkMeshStore& meshes;
        std::shared_ptr<jshader::ShaderPackInstance> instance;
        jshader::PipelineConfig config;
        jshader::UniformTable table;
        jshader::CustomUniforms custom;
        jshader::FrameState frameState;
        std::string name;
        std::string lastError;
        std::string path;
        std::function<std::string(uint16_t)> blockName;
        std::function<int(uint16_t)> blockEmission;
        std::unordered_map<std::string, int> nameToPackId;
        std::vector<int16_t> paletteMap;

        Pool pool;
        std::unordered_map<std::string, std::shared_future<std::shared_ptr<Converted>>> pending;
        std::unordered_map<std::string, std::unique_ptr<Program>> programs;
        std::set<std::string> loggedMissing;
        std::unordered_map<std::string, rhi::PipelineHandle> pipelines;
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
        rhi::PipelineLayoutHandle pipelineLayout;
        std::array<rhi::SamplerHandle, 16> samplers{};
        rhi::BufferHandle quad;
        rhi::BufferHandle skyStatic;
        uint32_t starVertexCount = 0;
        rhi::TextureHandle textureOverride, skySunTex, skyMoonTex;

        std::array<Buffer, jshader::kMaxColorTex> bufs;
        rhi::TextureHandle depth[3];
        rhi::TextureHandle shadowDepth;
        rhi::TextureHandle shadowColor[2];
        bool shadowColorUsed[2] = {false, false};
        bool shadowUsed = false;
        rhi::TextureHandle white, flatNormal, blackTex, noise;
        std::map<std::string, rhi::TextureHandle> customTextures;
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

        // shadow camera of the current frame
        float shadowView[16] = {}, shadowProjection[16] = {}, shadowDrawView[16] = {};
        FrameParams frame{};
        rhi::TextureHandle lightmapTex;
        std::array<std::vector<ChunkMeshStore::Run>, kRenderLayerCount> layerRuns;
        std::array<std::vector<ChunkMeshStore::Run>, 3> shadowRuns;
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
