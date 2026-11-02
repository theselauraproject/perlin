#include "client/renderer/gpu/RenderContext_rhi.hpp"

#include "client/Options.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/resources/ShaderPack.hpp"
#include "client/renderer/MaterialDb.hpp"
#include "client/renderer/Tessellator.hpp"
#include "rhi/DeviceProfile.hpp"
#include "util/Env.hpp"
#include "util/Paths.hpp"
#include "util/Resources.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include "util/Log.hpp"

namespace perlin {
    namespace {
        struct ProgramName {
            const char* builtin;
            // shader pack variant; null when the pack does not replace it
            const char* pack;
            gpu::VertexLayout layout;
            // converted java program used when a java pack is active
            const char* java = nullptr;
        };

        constexpr ProgramName kPrograms[static_cast<size_t>(Program::Count)] = {
            {"terrain_solid", "vv_terrain_solid", gpu::VertexLayout::Terrain, "java_gbuffers_terrain"},
            {"terrain_cutout", "vv_terrain_cutout", gpu::VertexLayout::Terrain, "java_gbuffers_terrain"},
            {"terrain_translucent", "vv_terrain_translucent", gpu::VertexLayout::Terrain,
             "java_gbuffers_water"},
            {"sky", "vv_sky", gpu::VertexLayout::Terrain, "java_gbuffers_skybasic"},
            {"star", "vv_star", gpu::VertexLayout::Position},
            {"sunrise", "vv_sunrise", gpu::VertexLayout::Color},
            {"celestial", "vv_celestial", gpu::VertexLayout::Texture},
            {"cloud", "vv_cloud", gpu::VertexLayout::Texture},
            {"screen", nullptr, gpu::VertexLayout::Position},
            {"ui", nullptr, gpu::VertexLayout::Ui},
            {"post", nullptr, gpu::VertexLayout::Position},
            {nullptr, "vv_shadow", gpu::VertexLayout::Terrain, "vv_shadow"},
            {nullptr, "vv_shadow_cutout", gpu::VertexLayout::Terrain, "vv_shadow_cutout"},
            {nullptr, "vv_shadow_glass", gpu::VertexLayout::Terrain, "vv_shadow_glass"},
            {"celestial", nullptr, gpu::VertexLayout::Texture},
            {"entity_shadow", nullptr, gpu::VertexLayout::Ui},
            {"bloom", nullptr, gpu::VertexLayout::Position},
            {"upscale", nullptr, gpu::VertexLayout::Position},
            {"taa", nullptr, gpu::VertexLayout::Position},
            {"framegen", nullptr, gpu::VertexLayout::Position},
            {"entity", nullptr, gpu::VertexLayout::Ui},
            {"entity_mask", nullptr, gpu::VertexLayout::Ui},
            {"entity_emissive", nullptr, gpu::VertexLayout::Ui},
            {"entity_glint", nullptr, gpu::VertexLayout::Ui},
            {"particle", nullptr, gpu::VertexLayout::Ui},
            {"particle_cutout", nullptr, gpu::VertexLayout::Ui},
            {"weather", nullptr, gpu::VertexLayout::Ui},
            {"cracks", nullptr, gpu::VertexLayout::Ui},
            {"nametag", nullptr, gpu::VertexLayout::Ui},
            {nullptr, nullptr, gpu::VertexLayout::Position, "java_deferred1"},
            {nullptr, nullptr, gpu::VertexLayout::Position, "java_composite"},
            {nullptr, nullptr, gpu::VertexLayout::Position, "java_composite1"},
            {nullptr, nullptr, gpu::VertexLayout::Position, "java_composite3"},
            {nullptr, nullptr, gpu::VertexLayout::Position, "java_composite4"},
            {nullptr, nullptr, gpu::VertexLayout::Position, "java_composite5"},
            {nullptr, nullptr, gpu::VertexLayout::Position, "java_composite6"},
            {nullptr, nullptr, gpu::VertexLayout::Position, "java_composite7"},
            {nullptr, nullptr, gpu::VertexLayout::Position, "java_final"},
        };

        bool LoadPackBytes(std::vector<uint8_t>& out) {
            std::string embedded;
            if (resources::Read("perlin_shaders.pack", embedded) && !embedded.empty()) {
                out.assign(embedded.begin(), embedded.end());
                return true;
            }
            for (const std::string& path :
                 {paths::ExeDir() + "/perlin_shaders.pack", std::string("perlin_shaders.pack")}) {
                std::ifstream in(path, std::ios::binary);
                if (!in)
                    continue;
                out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                if (!out.empty())
                    return true;
            }
            if (const char* env = PERLIN_ENV("PERLIN_SHADER_PACK")) {
                std::ifstream in(env, std::ios::binary);
                out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                return !out.empty();
            }
            return false;
        }

        std::string FailureFile() {
            return paths::DataDir() + "/render_failures.txt";
        }

        std::vector<rhi::KnownFailure> ReadFailures() {
            std::ifstream in(FailureFile());
            if (!in)
                return {};
            return rhi::ParseFailures(
                std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
        }

        void WriteFailures(const std::vector<rhi::KnownFailure>& failures) {
            std::ofstream out(FailureFile(), std::ios::trunc);
            out << rhi::SerializeFailures(failures);
        }
    }

    RenderContext_rhi::RenderContext_rhi() {
        programIds.fill(-1);
        programLayouts.fill(gpu::VertexLayout::Ui);
    }

    RenderContext_rhi::~RenderContext_rhi() {
        Shutdown();
    }

    std::string RenderContext_rhi::GpuVendor() const {
        return initialized ? rhi::VendorName(const_cast<gpu::Gpu&>(gpu).Caps().adapter.vendorId)
                           : std::string();
    }

    std::string RenderContext_rhi::GpuDescription() const {
        return initialized ? const_cast<gpu::Gpu&>(gpu).Caps().adapter.name : std::string();
    }

    bool RenderContext_rhi::Initialize(AppPlatform& p, const Options& options) {
        platform = &p;
        vsync = options.vsync;
        NativeWindowInfo native = platform->GetNativeWindowInfo();
        platform->GetFramebufferSize(width, height);

        std::vector<uint8_t> pack;
        if (!LoadPackBytes(pack)) {
            Log("render", "the shader pack is missing");
            return false;
        }

        std::string forced = PERLIN_ENV("PERLIN_RENDERER") ? PERLIN_ENV("PERLIN_RENDERER") : options.renderer;
        if (!StartDevice(forced, pack, false))
            return false;
        packData = std::move(pack);
        offscreen = gpu.OffscreenTexture().value != 0 || settings.offscreen;

        initialized = true;
        nativeWindow = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(native.window));
        nativeDisplay = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(native.display));
        adapterName = gpu.Caps().adapter.name;
        rendererName = rhi::ApiName(gpu.Api());
        caveCulling = !PERLIN_ENV("PERLIN_NO_CAVECULL");
        Log("render", "{} on {}", rendererName, adapterName);
        meshes.Init(gpu);
        if (!CreateStaticResources())
            return false;
        return true;
    }

    bool RenderContext_rhi::StartDevice(const std::string& forced, std::span<const uint8_t> pack,
                                        bool strict) {
        NativeWindowInfo native = platform->GetNativeWindowInfo();
        rhi::UserChoice user;
        user.api = forced == "auto" || forced.empty() ? rhi::Api::None : rhi::ApiFromId(forced);
        if (forced == "opengl" || forced == "gl")
            user.api = rhi::Api::None;
#if defined(_WIN32)
        constexpr bool kWindows = true, kApple = false;
#elif defined(__APPLE__)
        constexpr bool kWindows = false, kApple = true;
#else
        constexpr bool kWindows = false, kApple = false;
#endif
        rhi::ProbeOptions probeOptions;
        probeOptions.readCaps = true;
        const rhi::Probe probe = rhi::ProbeSystem(probeOptions);
        std::vector<rhi::KnownFailure> failures = ReadFailures();
        const rhi::RenderProfile profile = rhi::ChooseProfile(probe, user, failures, kWindows, kApple);
        for (const std::string& reason : profile.reasons)
            Log("gpu", "{}", reason);

        std::vector<std::pair<rhi::Api, uint32_t>> order;
        if (profile.api != rhi::Api::None)
            order.emplace_back(profile.api, profile.adapterIndex);
        for (const rhi::Candidate& c : profile.fallbacks)
            order.emplace_back(c.adapter.api, c.adapter.index);
        if (strict)
            order.erase(std::remove_if(order.begin(), order.end(),
                                       [&](const auto& o) { return o.first != user.api; }),
                        order.end());
        else if (order.empty())
            order.emplace_back(rhi::Api::Vulkan, 0);

        for (const auto& [api, adapter] : order) {
            gpu::GpuSettings s;
            s.api = api;
            s.adapterIndex = adapter;
            s.validation = PERLIN_ENV("PERLIN_GPU_VALIDATION") != nullptr;
            // renders into an owned image instead of a window (software rasteriser captures, offline previews)
            s.offscreen = PERLIN_ENV("PERLIN_OFFSCREEN") != nullptr;
            s.window = {native.window, native.display, native.wayland};
            s.width = static_cast<uint32_t>(std::max(1, width));
            s.height = static_cast<uint32_t>(std::max(1, height));
            s.present = vsync ? rhi::PresentMode::Fifo
                              : (profile.tearing ? rhi::PresentMode::Immediate : rhi::PresentMode::Mailbox);
            s.framesInFlight = std::max(1u, profile.framesInFlight);
            s.hdr = profile.hdr;
            std::string error;
            settings = s;
            if (gpu.Init(s, pack, &error)) {
                settings = gpu.Settings();
                break;
            }
            Log("render", "{} could not start: {}", rhi::ApiName(api), error);
            gpu.Shutdown();
            for (const rhi::Candidate& c : probe.candidates)
                if (c.adapter.api == api && c.adapter.index == adapter)
                    rhi::RecordFailure(failures, c.adapter);
            WriteFailures(failures);
        }
        return gpu.Chain() || gpu.OffscreenTexture();
    }

    bool RenderContext_rhi::InitializeOffscreen(const gpu::GpuSettings& s, std::span<const uint8_t> pack,
                                                std::string* error) {
        settings = s;
        settings.offscreen = true;
        packData.assign(pack.begin(), pack.end());
        if (!gpu.Init(settings, packData, error))
            return false;
        settings = gpu.Settings();
        offscreen = true;
        initialized = true;
        width = static_cast<int>(settings.width);
        height = static_cast<int>(settings.height);
        rendererName = rhi::ApiName(gpu.Api());
        adapterName = gpu.Caps().adapter.name;
        meshes.Init(gpu);
        return CreateStaticResources();
    }

    void RenderContext_rhi::Shutdown() {
        if (!initialized && !gpu.Chain() && !gpu.OffscreenTexture())
            return;
        if (cloudJobRunning) {
            cloudJob.wait();
            cloudJobRunning = false;
        }
        gpu.Dev().WaitIdle();
        FinishReadbacks();
        jpack.reset();
        DestroyStaticResources();
        meshes.Shutdown();
        DestroyUi();
        gpu.Shutdown();
        initialized = false;
    }

    int RenderContext_rhi::ProgramIndex(Program program) const {
        return programIds[static_cast<size_t>(program)];
    }

    bool RenderContext_rhi::MakeTexture(TexRec& rec, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips,
                                        rhi::Format format, gpu::SamplerKind sampler, const char* name) {
        FreeTexture(rec);
        rhi::TextureDesc td;
        td.dim = layers > 1 ? rhi::TextureDim::Tex2DArray : rhi::TextureDim::Tex2D;
        td.format = format;
        td.width = w;
        td.height = h;
        td.depthOrLayers = layers;
        td.mips = mips;
        td.name = name;
        rec.handle = gpu.CreateSampled(td);
        if (!rec.handle)
            return false;
        rec.w = w;
        rec.h = h;
        rec.layers = layers;
        rec.mips = mips;
        rec.format = format;
        rec.sampler = sampler;
        rec.used = true;
        return true;
    }

    void RenderContext_rhi::FreeTexture(TexRec& rec) {
        if (rec.handle)
            gpu.DestroyTexture(rec.handle);
        rec = {};
    }

    // programs for the active shader variant
    bool RenderContext_rhi::ResolvePrograms() {
        for (size_t i = 0; i < programIds.size(); i++) {
            const ProgramName& n = kPrograms[i];
            // java pack replaces terrain with the converted program
            const char* name = javaActive && n.java ? n.java : (packActive && n.pack ? n.pack : n.builtin);
            programIds[i] = name ? gpu.Program(name) : -1;
            programLayouts[i] = n.layout;
        }
        for (Program required : {Program::TerrainSolid, Program::Ui, Program::Sky}) {
            if (programIds[static_cast<size_t>(required)] < 0) {
                Log("render", "the shader pack lacks a required program");
                return false;
            }
        }
        return true;
    }

    bool RenderContext_rhi::ReloadShaders() {
        const bool want = ShaderPack::Instance().Active() && ShaderPack::Instance().Bundled();
        if (!initialized)
            return false;
        if (want != packActive) {
            packActive = want;
            gpu.ClearPipelines();
            if (!ResolvePrograms()) {
                packActive = false;
                ResolvePrograms();
                return false;
            }
        }
        return true;
    }

    bool RenderContext_rhi::CreateStaticResources() {
        if (!ResolvePrograms())
            return false;

        const auto upload = [&](const auto& vertices, const char* name) {
            return gpu.CreateStatic(vertices.data(), vertices.size() * sizeof(vertices[0]),
                                    rhi::BufferUsage::Vertex, name);
        };
        const auto uploadIndices = [&](const std::vector<uint16_t>& indices, const char* name) {
            return gpu.CreateStatic(indices.data(), indices.size() * sizeof(uint16_t),
                                    rhi::BufferUsage::Index, name);
        };
        const std::vector<PositionVertex> topSkyV = BuildSkyDisc(16.0f);
        // sky discs ride the terrain layout so converted java sky programs
        // see valid inputs; vv sky only reads position
        const auto toTerrain = [](const std::vector<PositionVertex>& in) {
            std::vector<Vertex> out;
            out.reserve(in.size());
            const uint32_t bright = PackLight(15.0f, 15.0f);
            for (const PositionVertex& p : in) {
                const float l = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
                const float inv = l > 1e-6f ? 1.0f / l : 0.0f;
                Vertex v{};
                v.x = p.x;
                v.y = p.y;
                v.z = p.z;
                v.rgba = 0xFFFFFFFFu;
                v.light = bright;
                v.normal =
                    PackColor(p.x * inv * 0.5f + 0.5f, p.y * inv * 0.5f + 0.5f, p.z * inv * 0.5f + 0.5f);
                out.push_back(v);
            }
            return out;
        };
        topSky = upload(toTerrain(topSkyV), "sky top");
        bottomSky = upload(toTerrain(BuildSkyDisc(-16.0f)), "sky bottom");
        {
            std::vector<PositionVertex> box;
            box.reserve(8);
            for (int i = 0; i < 8; i++)
                box.push_back({(i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f});
            skyBox = upload(box, "sky box");
            skyBoxIdx = uploadIndices({0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3},
                                      "sky box indices");
        }
        {
            const std::vector<PositionVertex> screen = {
                {-1.0f, -1.0f, 0.0f}, {3.0f, -1.0f, 0.0f}, {-1.0f, 3.0f, 0.0f}};
            screenTri = upload(screen, "screen triangle");
        }
        const std::vector<uint16_t> skyFan = FanIndices(static_cast<int>(topSkyV.size()));
        skyFanIndexCount = static_cast<uint32_t>(skyFan.size());
        fanIdx = uploadIndices(skyFan, "sky fan");
        const std::vector<ColorVertex> sunrise = BuildSunriseFan();
        sunriseVerts = upload(sunrise, "sunrise");
        const std::vector<uint16_t> sunriseFan = FanIndices(static_cast<int>(sunrise.size()));
        sunriseIndexCount = static_cast<uint32_t>(sunriseFan.size());
        sunriseIdx = uploadIndices(sunriseFan, "sunrise indices");
        const std::vector<PositionVertex> stars = BuildStars();
        starVerts = upload(stars, "stars");
        const std::vector<uint16_t> starQuads = QuadIndices(static_cast<int>(stars.size() / 4));
        starIndexCount = static_cast<uint32_t>(starQuads.size());
        starIdx = uploadIndices(starQuads, "star indices");
        sunVerts = upload(BuildSunQuad(), "sun");
        moonVerts = upload(BuildMoonQuads(), "moon");
        quadIdx = uploadIndices(QuadIndices(1), "quad");

        if (!MakeTexture(lightmap, kLightmapSize, kLightmapSize, 1, 1, rhi::Format::RGBA8Unorm,
                         gpu::SamplerKind::LinearClamp, "lightmap"))
            return false;
        const std::vector<uint8_t> blank(kLightmapBytes, 0);
        gpu.WriteTexture(lightmap.handle, 0, 0, 0, kLightmapSize, kLightmapSize, blank.data());
        return CreateScreenTargets();
    }

    void RenderContext_rhi::DestroyStaticResources() {
        auto& dev = gpu.Dev();
        DestroyPostTargets();
        post.failed = false;
        DestroyShadows();
        for (rhi::BufferHandle* b :
             {&topSky, &bottomSky, &skyBox, &skyBoxIdx, &screenTri, &starVerts, &starIdx, &sunriseVerts,
              &sunriseIdx, &sunVerts, &moonVerts, &quadIdx, &fanIdx}) {
            if (*b) {
                gpu.ForgetBuffer(*b);
                dev.Destroy(*b);
            }
            *b = {};
        }
        cloudVb.Destroy(dev);
        cloudIb.Destroy(dev);
        cloudVertexCapacity = cloudIndexCapacity = 0;
        cloudBuilt = false;
        for (TexRec* t :
             {&atlas, &lightmap, &sunTex, &moonTex, &cloudMapTex, &tileFlagsTex, &crosshair, &javaNoise,
              &javaNormalDef, &javaSpecDef, &javaBlack, &javaCloudWater, &javaMaterials})
            FreeTexture(*t);
        javaTexReady = false;
        javaMaterialCount = 0;
        DestroyScreenTargets();
    }

    bool RenderContext_rhi::CreateScreenTargets() {
        const uint32_t w = std::max(1u, gpu.TargetWidth()), h = std::max(1u, gpu.TargetHeight());
        if (screenDepth && screenDepthW == w && screenDepthH == h)
            return true;
        DestroyScreenTargets();
        rhi::TextureDesc td;
        td.format = rhi::Format::D32Float;
        td.width = w;
        td.height = h;
        td.usage = rhi::TextureUsage::DepthTarget | rhi::TextureUsage::Sampled;
        td.name = "screen depth";
        screenDepth = gpu.Dev().CreateTexture(td);
        screenDepthW = w;
        screenDepthH = h;
        return static_cast<bool>(screenDepth);
    }

    void RenderContext_rhi::DestroyScreenTargets() {
        if (screenDepth)
            gpu.DestroyTexture(screenDepth);
        screenDepth = {};
        screenDepthW = screenDepthH = 0;
    }

    void RenderContext_rhi::SetVsync(bool enabled) {
        vsync = enabled;
        if (initialized && !offscreen)
            gpu.SetPresentMode(enabled ? rhi::PresentMode::Fifo
                                       : (gpu.Caps().features.tearing ? rhi::PresentMode::Immediate
                                                                      : rhi::PresentMode::Mailbox));
    }

    bool RenderContext_rhi::QueryNativeHandles(NativeHandleInfo& out) const {
        if (!initialized)
            return false;
        out.api = rendererName;
        out.adapter = adapterName;
        out.window = nativeWindow;
        out.display = nativeDisplay;
        const rhi::NativeHandles h = const_cast<gpu::Gpu&>(gpu).Dev().Native();
        out.device = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h.device));
        return true;
    }

    // -- recording --------------------------------------------------------------------------------------------------

    gpu::PipelineKey RenderContext_rhi::KeyFor(const ViewData& view, const DrawState& s) const {
        gpu::PipelineKey k;
        k.program = static_cast<uint16_t>(std::max(0, ProgramIndex(s.program)));
        k.layout = s.layout;
        k.cull = s.cull;
        k.front = s.front;
        k.depthTest = s.depthTest;
        k.depthWrite = s.depthWrite;
        k.depthOp = view.reversedZ ? ReverseOp(s.depthOp) : s.depthOp;
        k.blend = s.blend;
        k.srcColor = s.srcColor;
        k.dstColor = s.dstColor;
        k.srcAlpha = s.srcAlpha;
        k.dstAlpha = s.dstAlpha;
        k.mask = s.mask;
        k.alphaOp = s.alphaOp;
        k.alphaToCoverage = s.alphaToCoverage;
        k.color = view.colorFormat;
        k.depth = view.depthFormat;
        k.colorCount = static_cast<uint8_t>(view.colorCount);
        k.color1 = view.colorFormat1;
        k.color2 = view.colorFormat2;
        return k;
    }

    void RenderContext_rhi::ResetBindings() {
        bound = {};
        model = glm::mat4(1.0f);
        modelIdentity = true;
    }

    void RenderContext_rhi::SetTransform(const glm::mat4& m) {
        model = m;
        modelIdentity = false;
    }

    void RenderContext_rhi::SetMatrix(const float columnMajor[16]) {
        uni.Mat4("u_mvp", columnMajor);
    }

    void RenderContext_rhi::BindTexture(const char* name, rhi::TextureHandle texture,
                                        gpu::SamplerKind sampler) {
        const rhi::TextureSlot* slot = gpu.Library().FindTexture(name);
        if (!slot || !texture)
            return;
        bound.texture[slot->slot] = texture;
        bound.sampler[slot->slot] = sampler;
    }

    bool RenderContext_rhi::Prepare(View view, const DrawState& state, DrawCall& dc) {
        const int program = ProgramIndex(state.program);
        if (!frameOpen || program < 0)
            return false;
        ViewData& vd = views[static_cast<size_t>(view)];
        const gpu::PipelineKey key = KeyFor(vd, state);
        const rhi::PipelineHandle pipeline = gpu.Pipeline(key);
        if (!pipeline)
            return false;

        // builtins that depend on the view and the model transform
        const glm::mat4 viewProj = vd.proj * vd.view;
        const glm::mat4 modelViewProj = modelIdentity ? viewProj : viewProj * model;
        if (builtinIds[0] < 0) {
            static constexpr const char* names[kBuiltinCount] = {
                "u_modelViewProj", "u_viewProj", "u_view", "u_proj", "u_model", "u_viewRect", "u_viewTexel"};
            for (int i = 0; i < kBuiltinCount; i++)
                builtinIds[i] = uni.Id(names[i]);
        }
        uni.Mat4(builtinIds[0], glm::value_ptr(modelViewProj));
        uni.Mat4(builtinIds[1], glm::value_ptr(viewProj));
        uni.Mat4(builtinIds[2], glm::value_ptr(vd.view));
        uni.Mat4(builtinIds[3], glm::value_ptr(vd.proj));
        uni.Mat4(builtinIds[4], glm::value_ptr(model));
        const float rect[4] = {static_cast<float>(vd.x), static_cast<float>(vd.y), static_cast<float>(vd.w),
                               static_cast<float>(vd.h)};
        uni.Set(builtinIds[5], rect, 4);
        const float texel[4] = {vd.w ? 1.0f / static_cast<float>(vd.w) : 0.0f,
                                vd.h ? 1.0f / static_cast<float>(vd.h) : 0.0f, 0.0f, 0.0f};
        uni.Set(builtinIds[6], texel, 4);

        dc = {};
        dc.pipeline = pipeline;
        const rhi::ProgramInfo& info = gpu.ProgramInfo(program);
        if (info.uniformBytes) {
            if (uniformCache.size() <= static_cast<size_t>(program))
                uniformCache.resize(static_cast<size_t>(program) + 1);
            UniformCache& cache = uniformCache[static_cast<size_t>(program)];
            const uint64_t frameId = gpu.FrameNumber();
            if (cache.valid && cache.frame == frameId && cache.version == uni.Version()) {
                dc.uniformChunk = cache.chunk;
                dc.uniformOffset = cache.offset;
            } else {
                const gpu::UniformStore::Binder& binder = uni.For(info, program);
                const gpu::RingSlice slice = gpu.Ring(info.uniformBytes, 256);
                if (!slice.data)
                    return false;
                uni.Write(binder, slice.data);
                dc.uniformChunk = slice.chunk;
                dc.uniformOffset = static_cast<uint32_t>(slice.offset);
                cache = {uni.Version(), slice.chunk, dc.uniformOffset, frameId, true};
            }
        } else {
            const gpu::RingSlice slice = gpu.Ring(256, 256);
            if (!slice.data)
                return false;
            dc.uniformChunk = slice.chunk;
            dc.uniformOffset = static_cast<uint32_t>(slice.offset);
        }
        dc.textures = gpu.TextureGroup(bound);
        return true;
    }

    void RenderContext_rhi::Push(View view, const DrawCall& templ, const Source& vertices,
                                 const Source* indices, rhi::IndexType indexType, uint32_t first,
                                 uint32_t count, const rhi::Rect* scissor) {
        DrawCall dc = templ;
        dc.vertices = vertices.buffer;
        dc.vertexOffset = vertices.offset;
        if (indices) {
            dc.indices = indices->buffer;
            dc.indexOffset = indices->offset;
            dc.indexType = indexType;
        }
        dc.first = first;
        dc.count = count;
        if (scissor) {
            dc.scissor = true;
            dc.rect = *scissor;
        }
        views[static_cast<size_t>(view)].draws.push_back(dc);
    }

    void RenderContext_rhi::Submit(View view, const DrawState& state, const Source& vertices,
                                   const Source* indices, rhi::IndexType indexType, uint32_t first,
                                   uint32_t count, const rhi::Rect* scissor) {
        DrawCall dc;
        if (count && vertices.buffer && (!indices || indices->buffer) && Prepare(view, state, dc))
            Push(view, dc, vertices, indices, indexType, first, count, scissor);
        ResetBindings();
    }

    void RenderContext_rhi::Replay(rhi::CommandList& list, const std::vector<DrawCall>& draws) {
        rhi::PipelineHandle pipeline;
        rhi::BindGroupHandle textures;
        rhi::BufferHandle vb, ib;
        uint64_t vbo = 0, ibo = 0;
        rhi::IndexType it = rhi::IndexType::U32;
        uint32_t chunk = ~0u;
        bool scissored = false;
        for (const DrawCall& dc : draws) {
            if (dc.pipeline != pipeline) {
                list.SetPipeline(dc.pipeline);
                pipeline = dc.pipeline;
            }
            const uint32_t offset = dc.uniformOffset;
            list.SetBindGroup(0, gpu.UniformGroup(dc.uniformChunk), std::span<const uint32_t>(&offset, 1));
            chunk = dc.uniformChunk;
            if (dc.textures != textures) {
                list.SetBindGroup(1, dc.textures);
                textures = dc.textures;
            }
            if (dc.vertices != vb || dc.vertexOffset != vbo) {
                list.SetVertexBuffer(0, dc.vertices, dc.vertexOffset);
                vb = dc.vertices;
                vbo = dc.vertexOffset;
            }
            if (dc.indices) {
                if (dc.indices != ib || dc.indexOffset != ibo || dc.indexType != it) {
                    list.SetIndexBuffer(dc.indices, dc.indexType, dc.indexOffset);
                    ib = dc.indices;
                    ibo = dc.indexOffset;
                    it = dc.indexType;
                }
            }
            if (dc.scissor) {
                list.SetScissor(dc.rect);
                scissored = true;
            } else if (scissored) {
                list.SetScissor({0, 0, gpu.TargetWidth(), gpu.TargetHeight()});
                scissored = false;
            }
            if (dc.indices)
                list.DrawIndexed(dc.count, 1, dc.first, 0, 0);
            else
                list.Draw(dc.count, 1, dc.first, 0);
        }
        (void)chunk;
    }

    // -- frame ------------------------------------------------------------------------------------------------------

    // z' = w - z in clip space: a zero-to-one depth range becomes one-to-zero, so far geometry gets the dense float precision near 0
    glm::mat4 RenderContext_rhi::ReverseDepth(const glm::mat4& m) {
        glm::mat4 r = m;
        for (int col = 0; col < 4; col++)
            r[col][2] = m[col][3] - m[col][2];
        return r;
    }

    rhi::CompareOp RenderContext_rhi::ReverseOp(rhi::CompareOp op) {
        switch (op) {
        case rhi::CompareOp::Less:
            return rhi::CompareOp::Greater;
        case rhi::CompareOp::LessEqual:
            return rhi::CompareOp::GreaterEqual;
        case rhi::CompareOp::Greater:
            return rhi::CompareOp::Less;
        case rhi::CompareOp::GreaterEqual:
            return rhi::CompareOp::LessEqual;
        default:
            return op;
        }
    }

    bool RenderContext_rhi::StageReversed(const Stage& stage) {
        for (View v : stage.views)
            if (v == View::ShadowBuild || v == View::ShadowGlass || v == View::ShadowEntity)
                return false;
        return true;
    }

    void RenderContext_rhi::ConvertDepth(const float in[16], float out[16]) const {
        const glm::mat4 r = ReverseDepth(glm::make_mat4(in));
        std::memcpy(out, glm::value_ptr(r), sizeof(float) * 16);
    }

    void RenderContext_rhi::PlanViews(const FrameParams& params) {
        for (ViewData& v : views) {
            v.draws.clear();
            v.x = v.y = 0;
            v.w = static_cast<uint32_t>(width);
            v.h = static_cast<uint32_t>(height);
            v.view = glm::mat4(1.0f);
            v.proj = glm::mat4(1.0f);
            v.reversedZ = true;
            v.colorFormat = gpu.TargetFormat();
            v.depthFormat = rhi::Format::D32Float;
        }
        for (View v : {View::ShadowBuild, View::ShadowGlass, View::ShadowEntity})
            views[static_cast<size_t>(v)].reversedZ = false;
        const glm::mat4 worldProj = ReverseDepth(glm::make_mat4(params.viewProjection));
        for (View v : {View::World, View::Entity, View::Late}) {
            views[static_cast<size_t>(v)].proj = worldProj;
        }
        views[static_cast<size_t>(View::Hand)].proj = ReverseDepth(glm::make_mat4(params.handProjection));
        const glm::mat4 ortho = ReverseDepth(glm::orthoRH_ZO(
            0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, -1000.0f, 1000.0f));
        for (View v : {View::Gui, View::Doll, View::Top, View::ExportUi})
            views[static_cast<size_t>(v)].proj = ortho;
    }

    void RenderContext_rhi::SetFrameUniforms(const FrameParams& params) {
        uni.Set("u_camera", params.cameraPosition, 3);
        uni.Set("u_fogColor", params.fogColor, 4);
        const float env[4] = {params.environmentalFogStart, params.environmentalFogEnd,
                              params.renderDistanceFogStart, params.renderDistanceFogEnd};
        uni.Set("u_fogEnv", env, 4);
        const float fogSky[4] = {params.skyFogEnd, params.cloudFogEnd, 0.0f, 0.0f};
        uni.Set("u_fogSky", fogSky, 4);
        const float texInfo[4] = {1.0f / static_cast<float>(std::max(1, tileSize)), params.rgss ? 1.0f : 0.0f,
                                  0.0f, 0.0f};
        uni.Set("u_textureInfo", texInfo, 4);
        const float sunY = std::cos(params.sky.sunAngle);
        const float sun[4] = {-std::sin(params.sky.sunAngle), sunY, 0.0f,
                              std::clamp((sunY + 0.1f) / 0.35f, 0.0f, 1.0f)};
        uni.Set("u_sunDir", sun, 4);
        const float time[4] = {params.timeSeconds, params.sky.rainBrightness, params.sky.starBrightness,
                               0.0f};
        uni.Set("u_time", time, 4);
        const float skyParams[4] = {params.cloud.height, std::floor(static_cast<float>(params.sky.moonPhase)),
                                    params.sky.enabled ? 1.0f : 0.0f,
                                    static_cast<float>(params.cameraMedium)};
        uni.Set("u_skyParams", skyParams, 4);
        const float held[4] = {params.heldLight, 0.0f, 0.0f, 0.0f};
        uni.Set("u_heldLight", held, 4);
        const float dyn[4] = {static_cast<float>(params.dynMode),
                              static_cast<float>(params.dynMode ? std::clamp(params.dynCount, 0, 8) : 0),
                              params.waterSurfaceAbove, 0.0f};
        uni.Set("u_dynParams", dyn, 4);
        uni.Set("u_dynLights", &params.dynLights[0][0], 32);
        uni.Set("u_packSettings", &params.packSettings[0], 32);
        const float tiles[4] = {params.waterTile, params.lavaTile, params.waterFlowTile, 0.0f};
        uni.Set("u_tiles", tiles, 4);
        uni.Vec4("u_chunk", 1.0f, 0.0f, 0.0f, 0.0f);
        if (javaActive)
            FeedJavaUniforms(params);
    }

    void RenderContext_rhi::BeginFrame(const FrameParams& paramsIn) {
        if (!initialized)
            return;
        if (jpack && !jpackReady && jpackError.empty()) {
            static uint32_t waitFrames = 0;
            const int left = jpack->WarmUp();
            if (left == 0 && !jpack->TerrainUsable()) {
                jpackError =
                    "the shader pack's terrain program failed to compile (see [jshader] lines above)";
                Log("javapack", "{}", jpackError);
            } else if (left == 0) {
                jpackReady = true;
                Log("javapack", "shaders ready");
            } else if (++waitFrames % 120 == 1) {
                Log("javapack", "compiling shaders, {} left (using the normal renderer meanwhile)", left);
            }
        }
        // the pack does its own post processing, scaling and shadows
        FrameParams packParams;
        const FrameParams* chosen = &paramsIn;
        if (PackRunning()) {
            packParams = paramsIn;
            jpackScale = paramsIn.renderScale;
            packParams.renderScale = 1.0f;
            packParams.upscaler = 0;
            packParams.frameGen = 1;
            packParams.motionBlur = 0.0f;
            packParams.bloom = 0.0f;
            packParams.menuBlur = 0.0f;
            packParams.shadowQuality = 0;
            chosen = &packParams;
        }
        const FrameParams& params = *chosen;
        jpackGathered = false;
        {
            std::string request;
            {
                std::lock_guard lock(switchMutex);
                request.swap(requestedSwitch);
            }
            if (!request.empty())
                ApplySwitchTo(request);
        }
        int w = 0, h = 0;
        if (!offscreen)
            platform->GetFramebufferSize(w, h);
        else {
            w = width;
            h = height;
        }
        if (forcedW > 0 && forcedH > 0) {
            w = forcedW;
            h = forcedH;
        }
        if (!offscreen && platform->TakeSurfaceChanged() && w > 0 && h > 0) {
            const NativeWindowInfo native = platform->GetNativeWindowInfo();
            gpu.SetWindow({native.window, native.display, native.wayland});
            gpu.RecreateSurface(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        }
        if (w > 0 && h > 0 && (w != width || h != height)) {
            width = w;
            height = h;
            gpu.Resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        }
        CreateScreenTargets();

        gpu.BeginFrame();
        FinishReadbacks();
        ReleaseFrameMeshes();
        uniformCache.assign(uniformCache.size(), {});
        frameOpen = true;
        frame = params;
        if (jpack)
            jpack->SetFluidTiles(static_cast<int>(params.waterTile), static_cast<int>(params.waterFlowTile),
                                 static_cast<int>(params.lavaTile), -1);
        PlanViews(params);
        {
            const bool usable = std::isfinite(params.projection[0]) && params.projection[0] != 0.0f &&
                                std::isfinite(params.view[12]);
            cam.havePrev =
                cam.haveCur && usable && !params.teleported && cam.sizeW == width && cam.sizeH == height;
            if (usable) {
                cam.prev2View = cam.prevView;
                cam.havePrev2 = cam.havePrev;
                cam.prevView = cam.view;
                cam.prevProj = cam.proj;
                cam.prevEye = cam.eye;
                const auto widen = [](const float* m) {
                    glm::dmat4 r(1.0);
                    for (int col = 0; col < 4; col++)
                        for (int row = 0; row < 4; row++)
                            r[col][row] = static_cast<double>(m[col * 4 + row]);
                    return r;
                };
                cam.view = widen(params.view);
                cam.proj = widen(params.projection);
                {
                    glm::dmat4 rz = cam.proj;
                    for (int c = 0; c < 4; c++)
                        rz[c][2] = cam.proj[c][3] - cam.proj[c][2];
                    cam.proj = rz;
                }
                cam.eye =
                    glm::dvec3(params.cameraPosition[0], params.cameraPosition[1], params.cameraPosition[2]);
                cam.prevTime = cam.time;
                cam.time = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
                               .count();
            }
            cam.haveCur = usable;
            cam.teleported = params.teleported;
            cam.sizeW = width;
            cam.sizeH = height;
        }
        PlanPost(params);
        if (shadowConfigDirty || params.shadowQuality != shadowQualityBuilt ||
            params.shadowDistance != shadowDistanceBuilt) {
            shadowConfigDirty = false;
            shadowQualityBuilt = params.shadowQuality;
            shadowDistanceBuilt = params.shadowDistance;
            InitShadows(params.shadowQuality, params.shadowDistance);
        }

        ConvertDepth(params.viewProjection, mvp);
        ConvertDepth(params.skyViewProjection, skyMvp);
        // sky at infinity: jitter shimmers clouds against unjittered sky
        if (plan.temporal) {
            float* m = mvp;
            for (int col = 0; col < 4; col++) {
                m[col * 4 + 0] += plan.jitter[0] * m[col * 4 + 3];
                m[col * 4 + 1] += plan.jitter[1] * m[col * 4 + 3];
            }
        }
        SetFrameUniforms(params);
        uni.Vec4("u_cloudMap", 0.0f, 3.96f, 1.0f / 3072.0f, 1.0f / 3072.0f);
        if (cloudTexture.width > 0 && cloudTexture.height > 0) {
            const double widthBlocks = cloudTexture.width * 12.0;
            uni.Vec4("u_cloudMap",
                     static_cast<float>(std::fmod(params.cloud.gameTicks * 0.030000001, widthBlocks)), 3.96f,
                     static_cast<float>(1.0 / widthBlocks),
                     static_cast<float>(1.0 / (cloudTexture.height * 12.0)));
        }
        {
            const glm::mat4 inverse = glm::inverse(glm::make_mat4(mvp));
            uni.Vec4("u_sceneInfo", 0.0f, 0.0f, 0.0f, 0.0f);
            uni.Mat4("u_invMvp", glm::value_ptr(inverse));
        }
        UploadLightmap(params.lightmap);
        ResetBindings();
    }

    void RenderContext_rhi::EndFrame() {
        if (!initialized || !frameOpen)
            return;
        RecordPost();
        frameOpen = false;
        ExecuteFrame();
        gpu.Dev().EndFrame();
        for (ViewData& v : views)
            v.draws.clear();
    }

    void RenderContext_rhi::ExecuteFrame() {
        rhi::Swapchain::Image image;
        if (!gpu.AcquireImage(image)) {
            if (!offscreen && platform) {
                const uint32_t failures = gpu.AcquireFailures();
                int fw = 0, fh = 0;
                platform->GetFramebufferSize(fw, fh);
                if (failures == 20 || failures == 600 || failures % 6000 == 0)
                    Log("render", "{} has had no image to draw to for {} frames (window {}x{})", rendererName,
                        failures, fw, fh);
                // a surface that stays dead is replaced: new window surface, new swapchain
                if (fw > 0 && fh > 0 && (failures == 45 || failures == 360 || failures == 3000)) {
                    platform->RenewRenderSurface();
                    const NativeWindowInfo native = platform->GetNativeWindowInfo();
                    gpu.SetWindow({native.window, native.display, native.wayland});
                    nativeWindow = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(native.window));
                    nativeDisplay = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(native.display));
                    const bool healed =
                        gpu.RecreateSurface(static_cast<uint32_t>(fw), static_cast<uint32_t>(fh));
                    Log("render", "rebuilding the window surface: {}", (healed ? "ok" : "failed"));
                }
            }
            return;
        }
        rhi::Device& dev = gpu.Dev();
        rhi::CommandList* list = dev.AcquireCommandList(rhi::QueueType::Graphics);
        if (!list)
            return;
        gpu.SetState(image.texture, rhi::State::Undefined);
        gpu.Flush(*list);

        Target screen;
        screen.color = image.texture;
        screen.depth = screenDepth;
        screen.w = gpu.TargetWidth();
        screen.h = gpu.TargetHeight();
        screen.colorFormat = gpu.TargetFormat();

        for (Stage& stage : stages)
            if (stage.toScreen) {
                stage.target = screen;
                if (!stage.useScreenDepth)
                    stage.target.depth = {};
            }
        if (PackRunning() && jpackGathered) {
            gpu::JavaPack::Output po;
            po.color = image.texture;
            po.colorFormat = gpu.TargetFormat();
            po.depth = screenDepth;
            po.width = gpu.TargetWidth();
            po.height = gpu.TargetHeight();
            po.scale = jpackScale;
            po.lightmap = lightmap.handle;
            po.sun = sunTex.handle;
            po.moon = moonTex.handle;
            // the pack wrote the whole picture and the depth: the world stage draws on top instead of clearing
            const bool recorded = jpack->Record(*list, frame, po);
            jpack->DropQueued();
            if (std::getenv("JSHADER_TRACE"))
                std::fprintf(stderr, "[jshader] pack frame recorded: %d (stages %zu, first views %zu)\n",
                             recorded ? 1 : 0, stages.size(),
                             stages.empty() ? size_t{0} : stages[0].views.size());
            if (recorded)
                for (Stage& stage : stages)
                    if (stage.toScreen && !stage.views.empty() && stage.views[0] == View::World) {
                        stage.clearColor = stage.clearDepth = false;
                        break;
                    }
        }
        for (const Stage& stage : shadowStages)
            RunStage(*list, stage);
        for (const Stage& stage : stages)
            RunStage(*list, stage);

        if (!pendingSinks.empty() || !pendingShots.empty())
            ResolveCapture(*list, screen);
        gpu.Transition(*list, image.texture, offscreen ? rhi::State::CopySrc : rhi::State::Present);
        gpu.FlushRings();
        dev.Submit(rhi::QueueType::Graphics, list);
        gpu.PresentImage();
    }

    void RenderContext_rhi::UploadLightmap(const uint8_t* pixels) {
        if (lightmapValid && std::memcmp(lastLightmap, pixels, kLightmapBytes) == 0)
            return;
        std::memcpy(lastLightmap, pixels, kLightmapBytes);
        lightmapValid = true;
        gpu.WriteTexture(lightmap.handle, 0, 0, 0, kLightmapSize, kLightmapSize, pixels);
    }

    // -- capture ----------------------------------------------------------------------------------------------------

    void RenderContext_rhi::RequestScreenshot(const std::string& path) {
        if (!initialized)
            return;
        std::lock_guard lock(captureMutex);
        pendingShots.push_back(path);
    }

    void RenderContext_rhi::RequestFrameCapture(CaptureSink sink) {
        if (!initialized || !sink)
            return;
        std::lock_guard lock(captureMutex);
        pendingSinks.push_back(std::move(sink));
    }

    void RenderContext_rhi::ResolveCapture(rhi::CommandList& list, const Target& target) {
        std::vector<CaptureSink> sinks;
        std::vector<std::string> shots;
        {
            std::lock_guard lock(captureMutex);
            sinks.assign(std::make_move_iterator(pendingSinks.begin()),
                         std::make_move_iterator(pendingSinks.end()));
            pendingSinks.clear();
            shots.swap(pendingShots);
        }
        if (sinks.empty() && shots.empty())
            return;
        const uint32_t w = target.w, h = target.h;
        const uint32_t pitch = static_cast<uint32_t>(
            (static_cast<uint64_t>(w) * 4 + gpu.Caps().limits.copyRowPitchAlignment - 1) /
            gpu.Caps().limits.copyRowPitchAlignment * gpu.Caps().limits.copyRowPitchAlignment);
        rhi::BufferDesc bd;
        bd.size = static_cast<uint64_t>(pitch) * h;
        bd.usage = rhi::BufferUsage::CopyDst;
        bd.memory = rhi::MemoryClass::Readback;
        bd.name = "capture";
        const rhi::BufferHandle buffer = gpu.Dev().CreateBuffer(bd);
        if (!buffer)
            return;
        gpu.Transition(list, target.color, rhi::State::CopySrc);
        rhi::BufferTextureCopy c;
        c.bufferRowPitch = pitch;
        c.width = w;
        c.height = h;
        list.CopyTextureToBuffer(buffer, target.color, std::span<const rhi::BufferTextureCopy>(&c, 1));
        Readback r;
        r.buffer = buffer;
        r.w = w;
        r.h = h;
        r.pitch = pitch;
        r.bgra =
            target.colorFormat == rhi::Format::BGRA8Unorm || target.colorFormat == rhi::Format::BGRA8Srgb;
        r.frame = gpu.FrameNumber();
        r.sinks = std::move(sinks);
        r.paths = std::move(shots);
        readbacks.push_back(std::move(r));
    }

    void RenderContext_rhi::FinishReadbacks() {
        for (size_t i = 0; i < readbacks.size();) {
            Readback& r = readbacks[i];
            if (r.frame + gpu.Frames() > gpu.FrameNumber()) {
                i++;
                continue;
            }
            gpu.Dev().Invalidate(r.buffer, 0, static_cast<uint64_t>(r.pitch) * r.h);
            const uint8_t* data = gpu.Dev().Map(r.buffer);
            if (data) {
                std::vector<uint8_t> swapped;
                const uint8_t* bgra = data;
                if (!r.bgra) {
                    swapped.assign(data, data + static_cast<size_t>(r.pitch) * r.h);
                    for (uint32_t y = 0; y < r.h; y++)
                        for (uint32_t x = 0; x < r.w; x++)
                            std::swap(swapped[static_cast<size_t>(y) * r.pitch + x * 4],
                                      swapped[static_cast<size_t>(y) * r.pitch + x * 4 + 2]);
                    bgra = swapped.data();
                }
                for (CaptureSink& sink : r.sinks)
                    sink(r.w, r.h, r.pitch, bgra, false);
                for (const std::string& path : r.paths)
                    SaveBmp(path, r.w, r.h, r.pitch, bgra);
            }
            gpu.ForgetBuffer(r.buffer);
            gpu.Dev().Destroy(r.buffer);
            readbacks.erase(readbacks.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    void RenderContext_rhi::SaveBmp(const std::string& path, uint32_t w, uint32_t h, uint32_t pitch,
                                    const uint8_t* bgra) {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f)
            return;
        const uint32_t rowBytes = w * 4;
        const uint32_t fileSize = 14 + 40 + rowBytes * h;
        uint8_t fh[14] = {'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0};
        std::memcpy(fh + 2, &fileSize, 4);
        uint8_t ih[40] = {};
        const uint32_t biSize = 40;
        std::memcpy(ih + 0, &biSize, 4);
        std::memcpy(ih + 4, &w, 4);
        const int32_t negH = -static_cast<int32_t>(h);
        std::memcpy(ih + 8, &negH, 4);
        ih[12] = 1;
        ih[14] = 32;
        const uint32_t imgSize = rowBytes * h;
        std::memcpy(ih + 20, &imgSize, 4);
        fwrite(fh, 1, 14, f);
        fwrite(ih, 1, 40, f);
        std::vector<uint8_t> row(rowBytes);
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t* src = bgra + static_cast<size_t>(y) * pitch;
            for (uint32_t x = 0; x < w; x++) {
                row[x * 4 + 0] = src[x * 4 + 0];
                row[x * 4 + 1] = src[x * 4 + 1];
                row[x * 4 + 2] = src[x * 4 + 2];
                row[x * 4 + 3] = 255;
            }
            fwrite(row.data(), 1, rowBytes, f);
        }
        fclose(f);
    }

    bool RenderContext_rhi::ReadBackFrame(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) {
        const rhi::TextureHandle image = gpu.OffscreenTexture();
        if (!image)
            return false;
        rhi::Device& dev = gpu.Dev();
        dev.WaitIdle();
        w = gpu.TargetWidth();
        h = gpu.TargetHeight();
        const uint32_t align = gpu.Caps().limits.copyRowPitchAlignment;
        const uint32_t pitch = (w * 4 + align - 1) / align * align;
        rhi::BufferDesc bd;
        bd.size = static_cast<uint64_t>(pitch) * h;
        bd.usage = rhi::BufferUsage::CopyDst;
        bd.memory = rhi::MemoryClass::Readback;
        bd.name = "frame readback";
        const rhi::BufferHandle buffer = dev.CreateBuffer(bd);
        if (!buffer)
            return false;
        rhi::CommandList* list = dev.AcquireCommandList(rhi::QueueType::Graphics);
        gpu.Transition(*list, image, rhi::State::CopySrc);
        rhi::BufferTextureCopy c;
        c.bufferRowPitch = pitch;
        c.width = w;
        c.height = h;
        list->CopyTextureToBuffer(buffer, image, std::span<const rhi::BufferTextureCopy>(&c, 1));
        dev.Submit(rhi::QueueType::Graphics, list);
        dev.WaitIdle();
        dev.Invalidate(buffer, 0, bd.size);
        const uint8_t* data = dev.Map(buffer);
        rgba.resize(static_cast<size_t>(w) * h * 4);
        for (uint32_t y = 0; y < h; y++)
            std::memcpy(rgba.data() + static_cast<size_t>(y) * w * 4, data + static_cast<size_t>(y) * pitch,
                        static_cast<size_t>(w) * 4);
        gpu.ForgetBuffer(buffer);
        dev.Destroy(buffer);
        return true;
    }

    // swaps the whole graphics api under a running game: everything that lives on the gpu is saved first, rebuilt on the new
    // device, and the world, ui handles and textures the game holds keep working
    bool RenderContext_rhi::RequestRendererSwitch(const std::string& id) {
        if (!initialized || offscreen)
            return false;
        std::lock_guard lock(switchMutex);
        requestedSwitch = id;
        return true;
    }

    bool RenderContext_rhi::RendererSwitchPending() const {
        std::lock_guard lock(switchMutex);
        return !requestedSwitch.empty();
    }

    // what the device policy would pick on this machine right now
    rhi::Api RenderContext_rhi::AutoApi() {
#if defined(_WIN32)
        constexpr bool kWindows = true, kApple = false;
#elif defined(__APPLE__)
        constexpr bool kWindows = false, kApple = true;
#else
        constexpr bool kWindows = false, kApple = false;
#endif
        rhi::ProbeOptions probeOptions;
        probeOptions.readCaps = true;
        const rhi::Probe probe = rhi::ProbeSystem(probeOptions);
        return rhi::ChooseProfile(probe, rhi::UserChoice{}, ReadFailures(), kWindows, kApple).api;
    }

    void RenderContext_rhi::ApplySwitchTo(const std::string& id) {
        const rhi::Api target = id == "auto" ? AutoApi() : rhi::ApiFromId(id);
        if (target == rhi::Api::None || target == gpu.Api()) {
            Log("render", "staying on {}", rendererName);
            return;
        }
        RebuildDevice(rhi::ApiId(target));
    }

    bool RenderContext_rhi::RebuildDevice(const std::string& id) {
        if (!initialized || frameOpen)
            return false;
        const rhi::Api target = rhi::ApiFromId(id);
        const rhi::Api previous = gpu.Api();
        if (target == rhi::Api::None)
            return false;
        const std::vector<uint8_t>& pack = packData;
        if (pack.empty()) {
            Log("render", "cannot switch: the shader pack is missing");
            return false;
        }
        Log("render", "switching {} to {}", rendererName, rhi::ApiName(target));

        if (cloudJobRunning) {
            cloudJob.wait();
            cloudJobRunning = false;
        }
        gpu.Dev().WaitIdle();
        FinishReadbacks();
        ReleaseFrameMeshes();
        // the pack holds device objects; it is built again on the new device (the meshes keep their extra stream)
        const bool hadPack = jpack != nullptr;
        jpack.reset();
        jpackReady = false;

        const std::vector<gpu::PipelineKey> warmKeys = gpu.PipelineKeys();
        gpu::ChunkMeshStore::Backup chunkBackup = meshes.Save();
        std::vector<gpu::Gpu::BufferSource> uiSources;
        for (UiPage& p : uiPages) {
            if (!p.live)
                continue;
            uiSources.push_back({p.vb.Buffer(), p.vb.BarrierState(), p.vb.Bytes()});
            uiSources.push_back({p.ib.Buffer(), p.ib.BarrierState(), p.ib.Bytes()});
        }
        std::vector<std::vector<uint8_t>> uiBlobs = gpu.ReadBuffers(uiSources);

        std::vector<TexRec> uiSaved = uiTextures;
        for (TexRec& t : uiSaved)
            t.handle = {};
        std::vector<uint8_t> flagsSaved = tileFlagData;
        std::vector<uint8_t> atlasSaved = std::move(atlasSource);
        const int atlasTilesSaved = atlasTiles, tilePxSaved = tileSize;
        const Texture sunSaved = sunSource, moonSaved = moonSource, cloudSaved = cloudSource,
                      iconSaved = iconSource;
        uint8_t lightmapSaved[kLightmapBytes];
        std::memcpy(lightmapSaved, lastLightmap, kLightmapBytes);
        const bool lightmapWas = lightmapValid;

        for (TexRec& t : uiTextures)
            if (t.used)
                FreeTexture(t);
        DestroyStaticResources();
        meshes.ReleaseGpu();
        for (UiPage& p : uiPages)
            for (gpu::Arena* a : {&p.vb, &p.ib}) {
                if (a->Buffer()) {
                    gpu.ForgetBuffer(a->Buffer());
                    gpu.Dev().Destroy(a->Buffer());
                }
                a->Detach();
            }
        gpu.Shutdown();
        initialized = false;
        uniformCache.clear();
        bound = {};
        lightmapValid = false;
        atlasTiles = 0;

        const auto bringUp = [&](const std::string& api) {
            // the old swapchain is gone; a window that presented through one api can keep showing its last frame under the next
            if (!offscreen && platform) {
                platform->RenewRenderSurface();
                const NativeWindowInfo native = platform->GetNativeWindowInfo();
                settings.window = {native.window, native.display, native.wayland};
                nativeWindow = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(native.window));
                nativeDisplay = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(native.display));
                int fw = 0, fh = 0;
                platform->GetFramebufferSize(fw, fh);
                if (fw > 0 && fh > 0) {
                    width = fw;
                    height = fh;
                    settings.width = static_cast<uint32_t>(fw);
                    settings.height = static_cast<uint32_t>(fh);
                }
            }
            settings.api = rhi::ApiFromId(api);
            const bool started = offscreen ? gpu.Init(settings, pack, nullptr) : StartDevice(api, pack, true);
            if (!started) {
                gpu.Shutdown();
                return false;
            }
            initialized = true;
            meshes.Init(gpu);
            if (!CreateStaticResources()) {
                gpu.Shutdown();
                initialized = false;
                return false;
            }
            return true;
        };
        bool ok = bringUp(id);
        if (!ok) {
            Log("render", "{} failed; returning to {}", rhi::ApiName(target), rhi::ApiName(previous));
            ok = bringUp(rhi::ApiId(previous));
        }
        if (!ok) {
            Log("render", "no renderer could be restored");
            return false;
        }

        adapterName = gpu.Caps().adapter.name;
        rendererName = rhi::ApiName(gpu.Api());
        // temporal filters must not reproject from the other device's frames, and the first frame must not stall on shader compiles
        cam = CameraHistory{};
        frameOpen = false;
        for (const gpu::PipelineKey& key : warmKeys)
            gpu.Pipeline(key);
        meshes.Load(std::move(chunkBackup));
        size_t blob = 0;
        for (UiPage& p : uiPages) {
            if (!p.live)
                continue;
            for (gpu::Arena* a : {&p.vb, &p.ib}) {
                if (a->Rebuild(gpu.Dev()) && blob < uiBlobs.size() && !uiBlobs[blob].empty())
                    gpu.WriteBuffer(*a, 0, uiBlobs[blob].data(), uiBlobs[blob].size());
                blob++;
            }
        }
        for (size_t i = 0; i < uiSaved.size() && i < uiTextures.size(); i++) {
            const TexRec& s = uiSaved[i];
            if (!s.used || s.backup.empty())
                continue;
            if (MakeTexture(uiTextures[i], s.w, s.h, 1, 1, s.format, s.sampler, "ui texture")) {
                gpu.WriteTexture(uiTextures[i].handle, 0, 0, 0, s.w, s.h, s.backup.data());
                uiTextures[i].backup = s.backup;
            }
        }
        if (!atlasSaved.empty())
            UploadAtlas(atlasSaved.data(), tilePxSaved, atlasTilesSaved);
        if (sunSaved.width > 0 || cloudSaved.width > 0)
            UploadSkyTextures(sunSaved, moonSaved, cloudSaved);
        if (!flagsSaved.empty())
            UploadTileFlags(flagsSaved);
        if (iconSaved.width > 0)
            UploadCrosshair(iconSaved);
        if (lightmapWas)
            UploadLightmap(lightmapSaved);
        if (hadPack && !jpackPath.empty()) {
            auto pack = std::make_unique<gpu::JavaPack>(gpu, meshes);
            gpu::JavaPack::Config config;
            config.path = jpackPath;
            for (const auto& kv : jpackOptions)
                config.options[kv.first] = kv.second;
            config.dimension = jpackDimension;
            config.blockName = blockNames;
            std::string why;
            if (pack->Load(config, why)) {
                pack->SetAtlas(atlasSource.data(), tileSize, atlasTiles);
                jpack = std::move(pack);
                AttachJavaPack();
            } else {
                jpackError = why;
                Log("javapack", "not available on {}: {}", rhi::ApiName(gpu.Api()), why);
            }
        }
        Log("render", "now on {} ({})", rendererName, adapterName);
        return true;
    }
}
