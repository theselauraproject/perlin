#include <thread>
#include <chrono>
#include "client/renderer/gpu/RenderContext_rhi.hpp"
#include "util/GraphicsDebugger.hpp"

#include "client/Options.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/resources/ShaderPack.hpp"
#include "client/renderer/MaterialDb.hpp"
#include "client/renderer/Tessellator.hpp"
#include <conduit/render_profile.hpp>
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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include "util/Log.hpp"

namespace perlin {
    namespace {
        struct ProgramName {
            const char* builtin;

            const char* pack;
            gpu::VertexLayout layout;

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
            {"ui", nullptr, gpu::VertexLayout::UiInstanced},
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
            {"entity", nullptr, gpu::VertexLayout::UiInstanced},
            {"entity_mask", nullptr, gpu::VertexLayout::UiInstanced},
            {"entity_emissive", nullptr, gpu::VertexLayout::UiInstanced},
            {"entity_glint", nullptr, gpu::VertexLayout::UiInstanced},
            {"particle", nullptr, gpu::VertexLayout::UiInstanced},
            {"particle_cutout", nullptr, gpu::VertexLayout::UiInstanced},
            {"weather", nullptr, gpu::VertexLayout::UiInstanced},
            {"cracks", nullptr, gpu::VertexLayout::UiInstanced},
            {"nametag", nullptr, gpu::VertexLayout::UiInstanced},
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

        std::vector<conduit::known_failure> ReadFailures() {
            std::ifstream in(FailureFile());
            if (!in)
                return {};
            return conduit::parse_failures(
                std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
        }

        void WriteFailures(const std::vector<conduit::known_failure>& failures) {
            std::ofstream out(FailureFile(), std::ios::trunc);
            out << conduit::serialize_failures(failures);
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
        return initialized ? conduit::vendor_name(const_cast<gpu::Gpu&>(gpu).Caps().adapter.vendor_id)
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
        rendererName = conduit::api_name(gpu.Api());
        caveCulling = !PERLIN_ENV("PERLIN_NO_CAVECULL");
        Log("render", "{} on {}", rendererName, adapterName);
        tuner.Init(gpu.Caps().adapter, paths::DataDir() + "/render-tune.txt");
        tuner.TimeEveryFrame(PERLIN_ENV("PERLIN_PROFILE") != nullptr);
        meshes.Init(gpu);
        if (!CreateStaticResources())
            return false;
        return true;
    }

    bool RenderContext_rhi::StartDevice(const std::string& forced, std::span<const uint8_t> pack,
                                        bool strict) {
        NativeWindowInfo native = platform->GetNativeWindowInfo();
        conduit::user_choice user;
        user.api = forced == "auto" || forced.empty() ? conduit::api::none : conduit::api_from_id(forced);
        if (forced == "opengl" || forced == "gl")
            user.api = conduit::api::none;
#if defined(_WIN32)
        constexpr bool kWindows = true, kApple = false;
#elif defined(__APPLE__)
        constexpr bool kWindows = false, kApple = true;
#else
        constexpr bool kWindows = false, kApple = false;
#endif
        const char* debugger = gfxdebug::Attached();
        const bool chosenByUser = user.api != conduit::api::none;
        if (debugger && kWindows && !chosenByUser && !strict) {
            user.api = conduit::api::d3d12;
            Log("gpu", "{} attached: starting on Direct3D 12, which it captures without a Vulkan layer", debugger);
        }
        conduit::probe_options probeOptions;
        probeOptions.read_caps = !debugger;
        if (debugger && chosenByUser)
            probeOptions.apis = {user.api};
        else if (debugger && kWindows)
            probeOptions.apis = {conduit::api::d3d12, conduit::api::d3d11};
        const conduit::probe probe = conduit::probe_system(probeOptions);
        std::vector<conduit::known_failure> failures = debugger ? std::vector<conduit::known_failure>{} : ReadFailures();
        if (debugger)
            Log("gpu", "{} attached: probing without throwaway devices", debugger);
        const conduit::render_profile profile = conduit::choose_profile(probe, user, failures, kWindows, kApple);
        for (const std::string& reason : profile.reasons)
            Log("gpu", "{}", reason);

        std::vector<std::pair<conduit::api, uint32_t>> order;
        if (profile.api != conduit::api::none)
            order.emplace_back(profile.api, profile.adapter_index);
        for (const conduit::candidate& c : profile.fallbacks)
            order.emplace_back(c.adapter.api, c.adapter.index);
        if (strict)
            order.erase(std::remove_if(order.begin(), order.end(),
                                       [&](const auto& o) { return o.first != user.api; }),
                        order.end());
        else if (order.empty())
            order.emplace_back(conduit::api::vulkan, 0);

        for (const auto& [api, adapter] : order) {
            gpu::GpuSettings s;
            s.api = api;
            s.adapterIndex = adapter;
            s.validation = PERLIN_ENV("PERLIN_GPU_VALIDATION") != nullptr;

            s.offscreen = PERLIN_ENV("PERLIN_OFFSCREEN") != nullptr;
            s.window = {native.window, native.display, native.wayland};
            s.width = static_cast<uint32_t>(std::max(1, width));
            s.height = static_cast<uint32_t>(std::max(1, height));
            s.present = vsync ? conduit::present_mode::fifo
                              : (profile.tearing ? conduit::present_mode::immediate : conduit::present_mode::mailbox);
            s.framesInFlight = std::max(1u, profile.frames_in_flight);
            s.hdr = profile.hdr;
            std::string error;
            pipelineBlobPath = paths::DataDir() + "/cache/pipelines_" + std::string(conduit::api_name(api)) + "_" +
                               std::to_string(adapter) + ".bin";
            pipelineBlob.clear();
            if (debugger)
                pipelineBlobPath.clear();
            else if (!PERLIN_ENV("PERLIN_NO_PIPELINE_CACHE")) {
                std::ifstream in(pipelineBlobPath, std::ios::binary);
                if (in)
                    pipelineBlob.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            }
            s.pipelineCache = pipelineBlob;
            settings = s;
            if (gpu.Init(s, pack, &error)) {
                settings = gpu.Settings();
                deviceTier = profile.tier;
                if (api != profile.api || adapter != profile.adapter_index)
                    for (const conduit::candidate& c : probe.candidates)
                        if (c.adapter.api == api && c.adapter.index == adapter)
                            deviceTier = std::min(profile.tier, conduit::tier_for(c));
                break;
            }
            Log("render", "{} could not start: {}", conduit::api_name(api), error);
            gpu.Shutdown();
            for (const conduit::candidate& c : probe.candidates)
                if (c.adapter.api == api && c.adapter.index == adapter)
                    conduit::record_failure(failures, c.adapter);
            if (!debugger)
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
        rendererName = conduit::api_name(gpu.Api());
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
        gpu.Dev().wait_idle();
        SavePipelineCache();
        FinishReadbacks();
        {
            std::unique_lock jpackLock(jpackMutex);
            jpack.reset();
        }
        DestroyStaticResources();
        meshes.Shutdown();
        DestroyUi();
        if (modGpu)
            modGpu->Shutdown();
        modGpu.reset();
        gpu.Shutdown();
        initialized = false;
    }

    int RenderContext_rhi::ProgramIndex(Program program) const {
        return programIds[static_cast<size_t>(program)];
    }

    bool RenderContext_rhi::GpuMemoryUsage(uint64_t& dedicated, uint64_t& shared) {
        if (!initialized)
            return false;
        const conduit::memory_budget b = gpu.Dev().query_memory_budget();
        if (!b.valid)
            return false;
        dedicated = b.device_usage;
        shared = b.host_usage;
        return true;
    }

    bool RenderContext_rhi::MakeTexture(TexRec& rec, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips,
                                        conduit::format format, gpu::SamplerKind sampler, const char* name) {
        FreeTexture(rec);
        conduit::texture_desc td;
        td.dim = layers > 1 ? conduit::texture_dim::tex2d_array : conduit::texture_dim::tex2d;
        td.format = format;
        td.width = w;
        td.height = h;
        td.depth_or_layers = layers;
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

    bool RenderContext_rhi::ResolvePrograms() {
        for (size_t i = 0; i < programIds.size(); i++) {
            const ProgramName& n = kPrograms[i];

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
        if (frameOpen) {
            shaderReloadPending = true;
            return true;
        }
        shaderReloadPending = false;
        if (want != packActive) {
            packActive = want;
            gpu.Dev().wait_idle();
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
        if (!PERLIN_ENV("PERLIN_NO_GPU_CULL") && !terrain.Ready()) {
            if (terrain.Init(gpu, meshes)) {
                Log("render", "terrain is gpu-driven (compute culling, indirect count draws)");
            } else {
                terrain.Shutdown();
                Log("render", "terrain culls on the cpu: the device lacks multi draw indirect with a gpu count");
            }
        }
        if (!ResolvePrograms())
            return false;

        const auto upload = [&](const auto& vertices, const char* name) {
            return gpu.CreateStatic(vertices.data(), vertices.size() * sizeof(vertices[0]),
                                    conduit::buffer_usage::vertex, name);
        };
        const auto uploadIndices = [&](const std::vector<uint16_t>& indices, const char* name) {
            return gpu.CreateStatic(indices.data(), indices.size() * sizeof(uint16_t),
                                    conduit::buffer_usage::index, name);
        };
        const std::vector<PositionVertex> topSkyV = BuildSkyDisc(16.0f);

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
        customSkyBox = upload(BuildCustomSkyBox(), "custom sky box");
        customSkyCube = upload(BuildCustomSkyCube(), "custom sky cube");
        endSkyBox = upload(BuildEndSkyBox(), "end sky box");

        if (!MakeTexture(lightmap, kLightmapSize, kLightmapSize, 1, 1, conduit::format::rgba8_unorm,
                         gpu::SamplerKind::LinearClamp, "lightmap"))
            return false;
        const std::vector<uint8_t> blank(kLightmapBytes, 0);
        gpu.WriteTexture(lightmap.handle, 0, 0, 0, kLightmapSize, kLightmapSize, blank.data());
        return CreateScreenTargets();
    }

    void RenderContext_rhi::DestroyStaticResources() {
        terrain.Shutdown();
        auto& dev = gpu.Dev();
        tuner.Release(dev);
        dev.destroy(uiCull.pipeline);
        dev.destroy(uiCull.layout);
        dev.destroy(uiCull.bindLayout);
        dev.destroy(uiCull.shader);
        dev.destroy(uiCull.output);
        uiCull = UiCull{};
        DestroyPostTargets();
        DestroyFrameGenTargets();
        fgenBlocked = false;
        post.failed = false;
        DestroyShadows();
        for (conduit::buffer_handle* b :
             {&topSky, &bottomSky, &skyBox, &skyBoxIdx, &screenTri, &starVerts, &starIdx, &sunriseVerts,
              &sunriseIdx, &sunVerts, &moonVerts, &quadIdx, &fanIdx, &customSkyBox, &customSkyCube, &endSkyBox}) {
            if (*b) {
                gpu.ForgetBuffer(*b);
                dev.destroy(*b);
            }
            *b = {};
        }
        cloudVb.Destroy(dev);
        cloudIb.Destroy(dev);
        cloudVertexCapacity = cloudIndexCapacity = 0;
        cloudBuilt = false;
        for (TexRec* t :
             {&lightmap, &sunTex, &moonTex, &endSkyTex, &cloudMapTex, &tileFlagsTex, &crosshair, &javaNoise,
              &javaNormalDef, &javaSpecDef, &javaBlack, &javaCloudWater, &javaMaterials})
            FreeTexture(*t);
        FreeCustomSkyVb();
        for (TexRec& t : customSkyTex)
            FreeTexture(t);
        customSkyTex.clear();
        FreeAtlas();
        javaTexReady = false;
        javaMaterialCount = 0;
        DestroyScreenTargets();
    }

    bool RenderContext_rhi::CreateScreenTargets() {
        const uint32_t w = std::max(1u, gpu.TargetWidth()), h = std::max(1u, gpu.TargetHeight());
        if (screenDepth && screenDepthW == w && screenDepthH == h)
            return true;
        DestroyScreenTargets();
        conduit::texture_desc td;
        td.format = conduit::format::d32_float;
        td.width = w;
        td.height = h;
        td.usage = conduit::texture_usage::depth_target | conduit::texture_usage::sampled | conduit::texture_usage::copy_src;
        td.name = "screen depth";
        screenDepth = gpu.Dev().create_texture(td);
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
        ApplyPresentMode(false);
    }

    void RenderContext_rhi::ApplyPresentMode(bool generating) {
        if (!initialized || offscreen)
            return;
        const conduit::present_mode want = vsync ? conduit::present_mode::fifo
                                      : generating || !gpu.Caps().features.tearing ? conduit::present_mode::mailbox
                                                                                   : conduit::present_mode::immediate;
        if (presentApplied == static_cast<int>(want))
            return;
        presentApplied = static_cast<int>(want);
        gpu.SetPresentMode(want);
    }

    bool RenderContext_rhi::QueryNativeHandles(NativeHandleInfo& out) const {
        if (!initialized)
            return false;
        out.api = rendererName;
        out.adapter = adapterName;
        out.window = nativeWindow;
        out.display = nativeDisplay;
        const conduit::native_handles h = const_cast<gpu::Gpu&>(gpu).Dev().native();
        out.device = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h.device));
        return true;
    }

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

    void RenderContext_rhi::BindTexture(const char* name, conduit::texture_handle texture,
                                        gpu::SamplerKind sampler) {
        const conduit::texture_slot* slot = gpu.Library().find_texture(name);
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
        const conduit::pipeline_handle pipeline = gpu.Pipeline(key);
        if (!pipeline)
            return false;

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
        const conduit::program_info& info = gpu.ProgramInfo(program);
        if (info.uniform_bytes) {
            if (uniformCache.size() <= static_cast<size_t>(program))
                uniformCache.resize(static_cast<size_t>(program) + 1);
            UniformCache& cache = uniformCache[static_cast<size_t>(program)];
            const uint64_t frameId = gpu.FrameNumber();
            if (cache.valid && cache.frame == frameId && cache.version == uni.Version()) {
                dc.uniformChunk = cache.chunk;
                dc.uniformOffset = cache.offset;
            } else {
                const gpu::UniformStore::Binder& binder = uni.For(info, program);
                const gpu::RingSlice slice = gpu.Ring(info.uniform_bytes, 256);
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
                                 const Source* indices, conduit::index_type indexType, uint32_t first,
                                 uint32_t count, const conduit::rect* scissor) {
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

    bool RenderContext_rhi::UseMultiDraw() const {
        static const bool off = PERLIN_ENV("PERLIN_NO_MDI") != nullptr;
        return !off && gpu.Caps().features.multi_draw_indirect;
    }

    bool RenderContext_rhi::PushRuns(View view, const DrawCall& templ, conduit::buffer_handle vertexBuffer,
                                     conduit::buffer_handle indexBuffer, const std::vector<gpu::ChunkMeshStore::Run>& list,
                                     size_t begin, size_t end) {
        const size_t n = end - begin;
        if (n < 2)
            return false;
        const gpu::RingSlice slice = gpu.Ring(n * sizeof(conduit::draw_indexed_indirect_command), 16);
        if (!slice.data)
            return false;
        auto* cmds = reinterpret_cast<conduit::draw_indexed_indirect_command*>(slice.data);
        for (size_t i = 0; i < n; i++)
            cmds[i] = {list[begin + i].count, 1, list[begin + i].first, 0, 0};
        DrawCall dc = templ;
        dc.vertices = vertexBuffer;
        dc.vertexOffset = 0;
        dc.indices = indexBuffer;
        dc.indexOffset = 0;
        dc.indexType = conduit::index_type::u32;
        dc.indirect = slice.buffer;
        dc.indirectOffset = slice.offset;
        dc.indirectDraws = static_cast<uint32_t>(n);
        views[static_cast<size_t>(view)].draws.push_back(dc);
        return true;
    }

    bool RenderContext_rhi::EnsureUiCull() {
        if (uiCull.pipeline)
            return true;
        if (uiCull.failed)
            return false;
        uiCull.failed = true;
        const conduit::shader_library& library = gpu.Library();
        const conduit::program_info* info = library.find("ui_cull");
        if (!info)
            return false;
        const conduit::shader_target target = gpu.ShaderTarget();
        const auto code = library.code(*info, conduit::shader_stage::compute, target);
        if (code.empty())
            return false;
        conduit::device& dev = gpu.Dev();
        conduit::shader_desc sd;
        sd.stage = conduit::shader_stage::compute;
        sd.format = conduit::shader_library::format_for(target);
        sd.code = code;
        sd.entry = conduit::shader_library::entry_for(conduit::shader_stage::compute, target);
        sd.name = "ui_cull";
        uiCull.shader = dev.create_shader(sd);
        const conduit::bind_layout_entry entries[] = {
            {0, conduit::binding_type::storage_buffer, conduit::shader_stage::compute},
            {1, conduit::binding_type::storage_buffer_rw, conduit::shader_stage::compute},
        };
        conduit::bind_layout_desc bl;
        bl.entries = entries;
        bl.name = "ui cull bindings";
        uiCull.bindLayout = dev.create_bind_layout(bl);
        conduit::pipeline_layout_desc pl;
        pl.sets[0] = uiCull.bindLayout;
        pl.push_constant_bytes = 112;
        pl.push_constant_stages = conduit::shader_stage::compute;
        pl.name = "ui cull layout";
        uiCull.layout = dev.create_pipeline_layout(pl);
        conduit::compute_pipeline_desc cp;
        cp.compute = uiCull.shader;
        cp.layout = uiCull.layout;
        cp.name = "ui_cull";
        uiCull.pipeline = uiCull.shader && uiCull.bindLayout && uiCull.layout ? dev.create_compute_pipeline(cp)
                                                                                : conduit::pipeline_handle{};
        uiCull.failed = !uiCull.pipeline;
        return !uiCull.failed;
    }

    void RenderContext_rhi::BatchUiDraws(View view, bool cull) {
        std::vector<DrawCall>& draws = views[static_cast<size_t>(view)].draws;
        if (draws.size() < 2)
            return;
        constexpr uint64_t kInst = sizeof(gpu::UiInstance);
        const auto batchable = [](const DrawCall& d) {
            return d.instances && d.indices && !d.indirectDraws && d.indexType == conduit::index_type::u32 &&
                   d.vertexOffset % sizeof(UiVertex) == 0 && d.indexOffset % 4 == 0 && d.instanceOffset % kInst == 0;
        };
        const auto same = [](const DrawCall& a, const DrawCall& b) {
            return a.pipeline == b.pipeline && a.uniformChunk == b.uniformChunk && a.uniformOffset == b.uniformOffset &&
                   a.textures == b.textures && a.vertices == b.vertices && a.indices == b.indices &&
                   a.instances == b.instances && a.scissor == b.scissor &&
                   (!a.scissor || (a.rect.x == b.rect.x && a.rect.y == b.rect.y && a.rect.width == b.rect.width &&
                                   a.rect.height == b.rect.height));
        };
        std::vector<DrawCall> out;
        out.reserve(draws.size());
        for (size_t i = 0; i < draws.size();) {
            size_t j = i + 1;
            if (batchable(draws[i]))
                while (j < draws.size() && batchable(draws[j]) && same(draws[i], draws[j]))
                    j++;
            const size_t n = j - i;
            if (n < 2) {
                out.push_back(draws[i]);
                i = j;
                continue;
            }
            bool cullRun = cull;
            for (size_t k = i; k < j && cullRun; k++)
                cullRun = draws[k].cullable;
            DrawCall merged = draws[i];
            merged.vertexOffset = 0;
            merged.indexOffset = 0;
            merged.instanceOffset = 0;
            merged.first = merged.count = 0;
            merged.indirectDraws = static_cast<uint32_t>(n);
            const auto command = [&](const DrawCall& d, uint32_t* c) {
                c[0] = d.count;
                c[1] = 1;
                c[2] = static_cast<uint32_t>(d.indexOffset / 4) + d.first;
                c[3] = static_cast<uint32_t>(d.vertexOffset / sizeof(UiVertex));
                c[4] = static_cast<uint32_t>(d.instanceOffset / kInst);
            };
            if (cullRun && uiCull.entries.size() + n <= uiCull.capacity) {
                merged.indirect = uiCull.output;
                merged.indirectOffset = uiCull.entries.size() * sizeof(conduit::draw_indexed_indirect_command);
                for (size_t k = i; k < j; k++) {
                    UiCullEntry e{};
                    command(draws[k], e.command);
                    std::memcpy(e.bounds, draws[k].bounds, sizeof(e.bounds));
                    uiCull.entries.push_back(e);
                }
            } else {
                const gpu::RingSlice slice = gpu.Ring(n * sizeof(conduit::draw_indexed_indirect_command), 16);
                if (!slice.data) {
                    out.insert(out.end(), draws.begin() + static_cast<std::ptrdiff_t>(i),
                               draws.begin() + static_cast<std::ptrdiff_t>(j));
                    i = j;
                    continue;
                }
                auto* cmds = reinterpret_cast<uint32_t*>(slice.data);
                for (size_t k = i; k < j; k++)
                    command(draws[k], cmds + (k - i) * 5);
                merged.indirect = slice.buffer;
                merged.indirectOffset = slice.offset;
            }
            out.push_back(merged);
            i = j;
        }
        draws.swap(out);
    }

    void RenderContext_rhi::RecordUiCull(conduit::command_list& list) {
        if (uiCull.entries.empty())
            return;
        const uint64_t align = std::max<uint64_t>(256, gpu.Caps().limits.storage_buffer_alignment);
        const uint64_t bytes = uiCull.entries.size() * sizeof(UiCullEntry);
        const gpu::RingSlice slice = gpu.Ring(bytes, align);
        if (!slice.data) {
            uiCull.entries.clear();
            return;
        }
        std::memcpy(slice.data, uiCull.entries.data(), bytes);
        conduit::bind_group_entry entries[2] = {};
        entries[0].binding = 0;
        entries[0].buffer = {slice.buffer, slice.offset, bytes};
        entries[1].binding = 1;
        entries[1].buffer = {uiCull.output, 0, 0};
        conduit::bind_group_desc bg;
        bg.layout = uiCull.bindLayout;
        bg.entries = entries;
        bg.per_frame = true;
        bg.name = "ui cull group";
        const conduit::bind_group_handle group = gpu.Dev().create_bind_group(bg);
        if (group) {
            struct {
                float planes[6][4];
                uint32_t count, pad[3];
            } push{};
            const Frustum f = Frustum::FromViewProjection(glm::make_mat4(frame.viewProjection));
            std::memcpy(push.planes, f.planes, sizeof(push.planes));
            push.count = static_cast<uint32_t>(uiCull.entries.size());
            list.barrier(conduit::buffer_barrier{uiCull.output, conduit::state::indirect_argument,
                                                 conduit::state::shader_write});
            list.set_pipeline(uiCull.pipeline);
            list.set_bind_group(0, group);
            list.push_constants(&push, sizeof push);
            list.dispatch((push.count + 63) / 64, 1, 1);
            list.barrier(conduit::buffer_barrier{uiCull.output, conduit::state::shader_write,
                                                 conduit::state::indirect_argument});
        }
        uiCull.entries.clear();
    }

    void RenderContext_rhi::Submit(View view, const DrawState& state, const Source& vertices,
                                   const Source* indices, conduit::index_type indexType, uint32_t first,
                                   uint32_t count, const conduit::rect* scissor) {
        DrawCall dc;
        if (count && vertices.buffer && (!indices || indices->buffer) && Prepare(view, state, dc))
            Push(view, dc, vertices, indices, indexType, first, count, scissor);
        ResetBindings();
    }

    void RenderContext_rhi::Replay(conduit::command_list& list, const std::vector<DrawCall>& draws) {
        conduit::pipeline_handle pipeline;
        conduit::bind_group_handle textures;
        conduit::buffer_handle vb, ib, instB;
        uint64_t vbo = 0, ibo = 0, instO = 0;
        conduit::index_type it = conduit::index_type::u32;
        uint32_t chunk = ~0u;
        uint32_t boundOffset = ~0u;
        bool scissored = false;
        for (const DrawCall& dc : draws) {
            bool pipelineChanged = false;
            if (dc.pipeline != pipeline) {
                list.set_pipeline(dc.pipeline);
                pipeline = dc.pipeline;
                pipelineChanged = true;
            }
            if (pipelineChanged || dc.uniformChunk != chunk || dc.uniformOffset != boundOffset) {
                const uint32_t offset = dc.uniformOffset;
                list.set_bind_group(0, gpu.UniformGroup(dc.uniformChunk), std::span<const uint32_t>(&offset, 1));
                chunk = dc.uniformChunk;
                boundOffset = offset;
            }
            if (pipelineChanged || dc.textures != textures) {
                list.set_bind_group(1, dc.textures);
                textures = dc.textures;
            }
            if (dc.vertices != vb || dc.vertexOffset != vbo) {
                list.set_vertex_buffer(0, dc.vertices, dc.vertexOffset);
                vb = dc.vertices;
                vbo = dc.vertexOffset;
            }
            if (dc.instances && (dc.instances != instB || dc.instanceOffset != instO)) {
                list.set_vertex_buffer(1, dc.instances, dc.instanceOffset);
                instB = dc.instances;
                instO = dc.instanceOffset;
            }
            if (dc.indices) {
                if (dc.indices != ib || dc.indexOffset != ibo || dc.indexType != it) {
                    list.set_index_buffer(dc.indices, dc.indexType, dc.indexOffset);
                    ib = dc.indices;
                    ibo = dc.indexOffset;
                    it = dc.indexType;
                }
            }
            if (dc.scissor) {
                list.set_scissor(dc.rect);
                scissored = true;
            } else if (scissored) {
                list.set_scissor({0, 0, gpu.TargetWidth(), gpu.TargetHeight()});
                scissored = false;
            }
            if (dc.indirectDraws && dc.countBuffer)
                list.draw_indexed_indirect_count(dc.indirect, dc.indirectOffset, dc.countBuffer, dc.countOffset,
                                                 dc.indirectDraws, sizeof(conduit::draw_indexed_indirect_command));
            else if (dc.indirectDraws)
                list.draw_indexed_indirect(dc.indirect, dc.indirectOffset, dc.indirectDraws,
                                         sizeof(conduit::draw_indexed_indirect_command));
            else if (dc.indices)
                list.draw_indexed(dc.count, 1, dc.first, 0, 0);
            else
                list.draw(dc.count, 1, dc.first, 0);
        }
        (void)chunk;
    }

    glm::mat4 RenderContext_rhi::ReverseDepth(const glm::mat4& m) {
        glm::mat4 r = m;
        for (int col = 0; col < 4; col++)
            r[col][2] = m[col][3] - m[col][2];
        return r;
    }

    conduit::compare_op RenderContext_rhi::ReverseOp(conduit::compare_op op) {
        switch (op) {
        case conduit::compare_op::less:
            return conduit::compare_op::greater;
        case conduit::compare_op::less_equal:
            return conduit::compare_op::greater_equal;
        case conduit::compare_op::greater:
            return conduit::compare_op::less;
        case conduit::compare_op::greater_equal:
            return conduit::compare_op::less_equal;
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
            v.depthFormat = conduit::format::d32_float;
        }
        for (View v : {View::ShadowBuild, View::ShadowGlass, View::ShadowEntity})
            views[static_cast<size_t>(v)].reversedZ = false;
        const glm::mat4 worldProj = ReverseDepth(glm::make_mat4(params.viewProjection));
        for (View v : {View::World, View::WorldLate, View::Entity, View::Late}) {
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

    void RenderContext_rhi::SavePipelineCache() {
        if (pipelineBlobPath.empty() || !gpu.Caps().features.pipeline_cache_blob || PERLIN_ENV("PERLIN_NO_PIPELINE_CACHE"))
            return;
        const std::vector<uint8_t> blob = gpu.Dev().serialize_pipeline_cache();
        if (blob.empty() || blob.size() == savedPipelineBytes)
            return;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(pipelineBlobPath).parent_path(), ec);
        const std::string tmp = pipelineBlobPath + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
                return;
            out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
        }
        std::filesystem::rename(tmp, pipelineBlobPath, ec);
        if (!ec)
            savedPipelineBytes = blob.size();
    }

    void RenderContext_rhi::BeginFrame(const FrameParams& paramsIn) {
        if (++pipelineSaveCounter % 7200 == 1800)
            SavePipelineCache();
        if (!initialized)
            return;
        if (shaderReloadPending)
            ReloadShaders();
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
        jpackShadowQuality = paramsIn.shadowQuality;
        ApplyPresentMode(paramsIn.frameGen > 1 && CanGenerateFrames());

        FrameParams packParams;
        const FrameParams* chosen = &paramsIn;
        if (PackRunning()) {
            packParams = paramsIn;

            jpackScale = paramsIn.upscaler != 0 ? paramsIn.renderScale : 1.0f;
            jpackUpscaler = paramsIn.upscaler;
            packParams.renderScale = 1.0f;
            packParams.upscaler = 0;
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

            static const int rebuildEvery = [] {
                const char* v = std::getenv("PERLIN_DEV_REBUILD_FRAMES");
                return v ? std::max(0, std::atoi(v)) : 0;
            }();
            static int rebuildCountdown = rebuildEvery;

            static const bool rebuildOnce = std::getenv("PERLIN_DEV_REBUILD_ONCE") != nullptr;
            if (rebuildEvery > 0 && --rebuildCountdown <= 0) {
                rebuildCountdown = rebuildOnce ? INT32_MAX : rebuildEvery;
                if (!RebuildDevice(conduit::api_id(gpu.Api())))
                    Log("render", "developer rebuild did not run (frame open: {})", frameOpen);
            } else if (!request.empty())
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
        gpuCullFrame = false;
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
        gpu.Dev().end_frame();
        for (ViewData& v : views)
            v.draws.clear();
    }

    void RenderContext_rhi::ExecuteFrame() {
        conduit::swapchain::image image;
        // A minimized window has nothing to draw to. Do not ask the swapchain for an image (it can block),
        // do not "heal" the surface, and do not spin: skip the frame and wait a little.
        if (!offscreen && platform && platform->WindowMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return;
        }
        if (!gpu.AcquireImage(image)) {
            if (!offscreen && platform) {
                const uint32_t failures = gpu.AcquireFailures();
                int fw = 0, fh = 0;
                platform->GetFramebufferSize(fw, fh);
                if (failures == 20 || failures == 600 || failures % 6000 == 0)
                    Log("render", "{} has had no image to draw to for {} frames (window {}x{})", rendererName,
                        failures, fw, fh);

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
        conduit::device& dev = gpu.Dev();
        conduit::command_list* list = dev.acquire_command_list(conduit::queue_type::graphics);
        if (!list)
            return;
        tuner.GpuBegin(dev, *list, gpu.FrameSlot(), gpu.Frames());
        gpu.SetState(image.texture, conduit::state::undefined);
        if (!modGpu)
            modGpu = std::make_unique<gpu::ModGpu>(gpu);
        if (!modGpu->Active())
            modGpu->Start(rendererName);
        modGpu->BeginFrame(gpu.TargetWidth(), gpu.TargetHeight());
        gpu.Flush(*list);
        gpu::ModGpu::Screen modScreen;
        modScreen.color = image.texture;
        modScreen.width = gpu.TargetWidth();
        modScreen.height = gpu.TargetHeight();
        modScreen.format = gpu.TargetFormat();
        modGpu->Record(*list, gpu::ModStage::Pre, &modScreen);
        if (gpuCullFrame)
            terrain.RecordEarly(*list);
        {
            const bool noBatch = !tuner.On(gpu::RenderTuner::GpuUi);
            const bool noCull = !tuner.On(gpu::RenderTuner::EntityCull);
            if (!noBatch && UseMultiDraw() && gpu.Caps().features.draw_indirect_first_instance) {
                // world draws (entities, particles, weather) are culled on the gpu: size the output for all of them
                size_t worldDraws = views[static_cast<size_t>(View::Entity)].draws.size() +
                                    views[static_cast<size_t>(View::Late)].draws.size();
                const bool cull = !noCull && worldDraws && gpu.RingStorage() && EnsureUiCull();
                if (cull && worldDraws > uiCull.capacity) {
                    uint32_t size = std::max(uiCull.capacity, 4096u);
                    while (size < worldDraws)
                        size *= 2;
                    if (uiCull.output)
                        gpu.Dev().destroy(uiCull.output);
                    conduit::buffer_desc bd;
                    bd.size = uint64_t{size} * sizeof(conduit::draw_indexed_indirect_command);
                    bd.usage = conduit::buffer_usage::storage | conduit::buffer_usage::indirect;
                    bd.memory = conduit::memory_class::gpu_only;
                    bd.stride = 4;
                    bd.name = "ui culled draws";
                    uiCull.output = gpu.Dev().create_buffer(bd);
                    uiCull.capacity = uiCull.output ? size : 0;
                }
                uiCull.entries.clear();
                for (View v : {View::Entity, View::Late})
                    BatchUiDraws(v, cull && uiCull.output);
                for (View v : {View::Hand, View::Gui, View::Doll, View::Top, View::ExportUi})
                    BatchUiDraws(v, false);
                RecordUiCull(*list);
            }
        }

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
        const bool generate = FrameGenWanted(image.texture) &&
                              EnsureFrameGenTargets(screen.w, screen.h, screen.colorFormat);
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
            po.endSky = endSkyTex.handle;
            const gpu::JavaPack::Output full = po;
            if (plan.packUpscale && post.packLow) {
                po.color = post.packLow;
                po.colorFormat = conduit::format::rgba8_unorm;
                po.depth = {};
                po.width = post.packLowW;
                po.height = post.packLowH;
                po.scale = 1.0f;
            }

            const bool recorded = jpack->Record(*list, frame, po);
            if (recorded && plan.packUpscale && post.packLow) {
                jpack->WriteEngineDepth(*list, full);
                gpu.Transition(*list, post.packLow, conduit::state::shader_read);
            } else if (plan.packUpscale) {
                views[static_cast<size_t>(View::Up0)].draws.clear();
                views[static_cast<size_t>(View::Up1)].draws.clear();
            }
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
        {
            size_t firstOverlay = stages.size();
            for (size_t i = 0; i < stages.size(); i++)
                if (stages[i].kind == Stage::Kind::Pass && !stages[i].views.empty() &&
                    stages[i].views.front() == View::Hand) {
                    firstOverlay = i;
                    break;
                }
            for (size_t i = 0; i < stages.size(); i++) {
                Stage& s = stages[i];
                s.discardDepth = false;
                if (s.kind != Stage::Kind::Pass || !s.target.depth || (generate && i < firstOverlay))
                    continue;
                bool decided = false;
                for (size_t j = i + 1; j < stages.size() && !decided; j++) {
                    const Stage& n = stages[j];
                    if (n.kind == Stage::Kind::Occlusion) {
                        decided = true;
                        continue;
                    }
                    if (n.kind == Stage::Kind::Copy) {
                        decided = n.copySrc == s.target.depth || n.copyDst == s.target.depth;
                        continue;
                    }
                    if (n.target.depth == s.target.depth) {
                        s.discardDepth = n.clearDepth;
                        decided = true;
                    } else if (s.target.depth != screenDepth) {
                        decided = true;
                    }
                }
                if (!decided && s.target.depth == screenDepth)
                    s.discardDepth = true;
            }
        }
        // per pass gpu time for PERLIN_PROFILE: a timestamp before each pass, labelled with its first view
        static constexpr const char* kViewNames[] = {"shadow", "shadow_glass", "shadow_entity", "world", "world_late", "entities", "late", "hand", "gui", "doll", "top", "export_ui", "blur", "post_out", "bloom0", "bloom1", "bloom2", "bloom3", "bloom4", "bloom5", "bloom6", "upscale", "sharpen", "java_deferred", "java_composite", "java_composite1", "java_composite3", "java_composite4", "java_composite5", "java_composite6", "java_composite7", "java_final"};
        static_assert(std::size(kViewNames) == static_cast<size_t>(View::Count));
        const auto stageLabel = [](const Stage& s) -> const char* {
            if (s.kind == Stage::Kind::Copy)
                return "copy";
            return s.views.empty() ? "pass" : kViewNames[static_cast<size_t>(s.views.front())];
        };
        for (const Stage& stage : shadowStages) {
            tuner.Mark(*list, stageLabel(stage));
            RunStage(*list, stage);
        }
        fgen.valid = false;
        bool hudlessTaken = false;
        bool underHudDone = false;
        conduit::texture_handle passDepth;
        for (const Stage& stage : stages) {
            if (stage.kind == Stage::Kind::Occlusion) {
                tuner.Mark(*list, "occlusion");
                terrain.RecordLate(*list, passDepth);
                continue;
            }
            if (stage.kind == Stage::Kind::Pass)
                passDepth = stage.target.depth;
            if (generate && !hudlessTaken && stage.kind == Stage::Kind::Pass && !stage.views.empty() &&
                stage.views.front() == View::Hand) {
                CaptureHudless(*list, screen);
                hudlessTaken = true;
            }
            if (!underHudDone && stage.kind == Stage::Kind::Pass &&
                std::any_of(stage.views.begin(), stage.views.end(),
                            [](View v) { return v == View::Hand || v == View::Gui; })) {
                modGpu->Record(*list, gpu::ModStage::UnderHud, &modScreen);
                underHudDone = true;
            }
            tuner.Mark(*list, stageLabel(stage));
            RunStage(*list, stage);
        }
        tuner.Mark(*list, "mods_overlay");
        if (!underHudDone)
            modGpu->Record(*list, gpu::ModStage::UnderHud, &modScreen);
        // Plugin drawing is screen-space like the HUD: it is recorded before the final capture, so generated frames
        // show it unwarped (the frame-gen shader keeps every pixel where the final image differs from the hudless one).
        modGpu->Record(*list, gpu::ModStage::Overlay, &modScreen);
        if (generate && hudlessTaken)
            CaptureFinal(*list, screen);

        if (!pendingSinks.empty() || !pendingShots.empty())
            ResolveCapture(*list, screen);
        tuner.GpuEnd(*list);
        gpu.Transition(*list, image.texture, offscreen ? conduit::state::copy_src : conduit::state::present);
        gpuCullFrame = false;
        gpu.FlushRings();
        dev.submit(conduit::queue_type::graphics, list);
        gpu.PresentImage();
        modGpu->EndFrame();
        tuner.EndFrame(tuneWorldFrame);
        tuneWorldFrame = false;
    }

    void RenderContext_rhi::UploadLightmap(const uint8_t* pixels) {
        if (lightmapValid && std::memcmp(lastLightmap, pixels, kLightmapBytes) == 0)
            return;
        std::memcpy(lastLightmap, pixels, kLightmapBytes);
        lightmapValid = true;
        gpu.WriteTexture(lightmap.handle, 0, 0, 0, kLightmapSize, kLightmapSize, pixels);
    }

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

    void RenderContext_rhi::ResolveCapture(conduit::command_list& list, const Target& target) {
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
            (static_cast<uint64_t>(w) * 4 + gpu.Caps().limits.copy_row_pitch_alignment - 1) /
            gpu.Caps().limits.copy_row_pitch_alignment * gpu.Caps().limits.copy_row_pitch_alignment);
        conduit::buffer_desc bd;
        bd.size = static_cast<uint64_t>(pitch) * h;
        bd.usage = conduit::buffer_usage::copy_dst;
        bd.memory = conduit::memory_class::readback;
        bd.name = "capture";
        const conduit::buffer_handle buffer = gpu.Dev().create_buffer(bd);
        if (!buffer)
            return;
        gpu.Transition(list, target.color, conduit::state::copy_src);
        conduit::buffer_texture_copy c;
        c.buffer_row_pitch = pitch;
        c.width = w;
        c.height = h;
        list.copy_texture_to_buffer(buffer, target.color, std::span<const conduit::buffer_texture_copy>(&c, 1));
        Readback r;
        r.buffer = buffer;
        r.w = w;
        r.h = h;
        r.pitch = pitch;
        r.bgra =
            target.colorFormat == conduit::format::bgra8_unorm || target.colorFormat == conduit::format::bgra8_srgb;
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
            gpu.Dev().invalidate(r.buffer, 0, static_cast<uint64_t>(r.pitch) * r.h);
            const uint8_t* data = gpu.Dev().map(r.buffer);
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
            gpu.Dev().destroy(r.buffer);
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
        const conduit::texture_handle image = gpu.OffscreenTexture();
        if (!image)
            return false;
        conduit::device& dev = gpu.Dev();
        dev.wait_idle();
        w = gpu.TargetWidth();
        h = gpu.TargetHeight();
        const uint32_t align = gpu.Caps().limits.copy_row_pitch_alignment;
        const uint32_t pitch = (w * 4 + align - 1) / align * align;
        conduit::buffer_desc bd;
        bd.size = static_cast<uint64_t>(pitch) * h;
        bd.usage = conduit::buffer_usage::copy_dst;
        bd.memory = conduit::memory_class::readback;
        bd.name = "frame readback";
        const conduit::buffer_handle buffer = dev.create_buffer(bd);
        if (!buffer)
            return false;
        conduit::command_list* list = dev.acquire_command_list(conduit::queue_type::graphics);
        gpu.Transition(*list, image, conduit::state::copy_src);
        conduit::buffer_texture_copy c;
        c.buffer_row_pitch = pitch;
        c.width = w;
        c.height = h;
        list->copy_texture_to_buffer(buffer, image, std::span<const conduit::buffer_texture_copy>(&c, 1));
        dev.submit(conduit::queue_type::graphics, list);
        dev.wait_idle();
        dev.invalidate(buffer, 0, bd.size);
        const uint8_t* data = dev.map(buffer);
        rgba.resize(static_cast<size_t>(w) * h * 4);
        for (uint32_t y = 0; y < h; y++)
            std::memcpy(rgba.data() + static_cast<size_t>(y) * w * 4, data + static_cast<size_t>(y) * pitch,
                        static_cast<size_t>(w) * 4);
        gpu.ForgetBuffer(buffer);
        dev.destroy(buffer);
        return true;
    }

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

    conduit::api RenderContext_rhi::AutoApi() {
#if defined(_WIN32)
        constexpr bool kWindows = true, kApple = false;
#elif defined(__APPLE__)
        constexpr bool kWindows = false, kApple = true;
#else
        constexpr bool kWindows = false, kApple = false;
#endif
        const bool debugger = gfxdebug::Attached() != nullptr;
        conduit::probe_options probeOptions;
        probeOptions.read_caps = !debugger;
        if (debugger && kWindows)
            probeOptions.apis = {conduit::api::d3d12, conduit::api::d3d11};
        const conduit::probe probe = conduit::probe_system(probeOptions);
        return conduit::choose_profile(probe, conduit::user_choice{},
                                       debugger ? std::vector<conduit::known_failure>{} : ReadFailures(), kWindows,
                                       kApple)
            .api;
    }

    void RenderContext_rhi::ApplySwitchTo(const std::string& id) {
        const conduit::api target = id == "auto" ? AutoApi() : conduit::api_from_id(id);
        if (target == conduit::api::none || target == gpu.Api()) {
            Log("render", "staying on {}", rendererName);
            return;
        }
        RebuildDevice(conduit::api_id(target));
    }

    bool RenderContext_rhi::RebuildDevice(const std::string& id) {
        if (!initialized || frameOpen)
            return false;
        const conduit::api target = conduit::api_from_id(id);
        const conduit::api previous = gpu.Api();
        if (target == conduit::api::none)
            return false;
        const std::vector<uint8_t>& pack = packData;
        if (pack.empty()) {
            Log("render", "cannot switch: the shader pack is missing");
            return false;
        }
        Log("render", "switching {} to {}", rendererName, conduit::api_name(target));

        if (cloudJobRunning) {
            cloudJob.wait();
            cloudJobRunning = false;
        }
        gpu.Dev().wait_idle();
        FinishReadbacks();
        ReleaseFrameMeshes();

        const bool hadPack = jpack != nullptr;
        {
            std::unique_lock jpackLock(jpackMutex);
            jpack.reset();
        }
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
        {
            // ui textures keep no cpu copy while running: read them back once, here
            std::vector<conduit::texture_handle> handles;
            for (const TexRec& t : uiTextures)
                handles.push_back(t.used ? t.handle : conduit::texture_handle{});
            std::vector<std::vector<uint8_t>> pixels = gpu.ReadTextures(handles);
            for (size_t i = 0; i < uiSaved.size() && i < pixels.size(); i++)
                uiSaved[i].backup = std::move(pixels[i]);
        }
        for (TexRec& t : uiSaved)
            t.handle = {};
        std::vector<uint8_t> flagsSaved = tileFlagData;
        std::vector<uint8_t> atlasSaved = std::move(atlasSource);
        const int atlasTilesSaved = atlasTiles, tilePxSaved = tileSize;
        const Texture sunSaved = sunSource, moonSaved = moonSource, cloudSaved = cloudSource,
                      iconSaved = iconSource, endSkySaved = endSkySource;
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
                    gpu.Dev().destroy(a->Buffer());
                }
                a->Detach();
            }
        if (modGpu)
            modGpu->Shutdown();
        modGpu.reset();
        gpu.Shutdown();
        initialized = false;
        uniformCache.clear();
        bound = {};
        lightmapValid = false;
        atlasTiles = 0;

        const auto bringUp = [&](const std::string& api) {

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
            if (offscreen && width > 0 && height > 0) {

                settings.width = static_cast<uint32_t>(width);
                settings.height = static_cast<uint32_t>(height);
            }
            settings.api = conduit::api_from_id(api);
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
            Log("render", "{} failed; returning to {}", conduit::api_name(target), conduit::api_name(previous));
            ok = bringUp(conduit::api_id(previous));
        }
        if (!ok) {
            Log("render", "no renderer could be restored");
            return false;
        }

        adapterName = gpu.Caps().adapter.name;
        rendererName = conduit::api_name(gpu.Api());

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
            if (MakeTexture(uiTextures[i], s.w, s.h, 1, 1, s.format, s.sampler, "ui texture"))
                gpu.WriteTexture(uiTextures[i].handle, 0, 0, 0, s.w, s.h, s.backup.data());
        }
        if (!atlasSaved.empty())
            UploadAtlas(atlasSaved.data(), tilePxSaved, atlasTilesSaved);
        if (sunSaved.width > 0 || cloudSaved.width > 0)
            UploadSkyTextures(sunSaved, moonSaved, cloudSaved);
        if (endSkySaved.width > 0)
            UploadEndSkyTexture(endSkySaved);
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
            config.blockEmission = blockEmission;
            config.shadowResolutionLimit = PackShadowLimit();
            config.shadowInterval = PackShadowInterval();
            config.shadowCache = !PERLIN_ENV("PERLIN_PACK_NO_SHADOW_CACHE");
            std::string why;
            if (pack->Load(config, why)) {
                pack->SetAtlas(atlasSource.data(), tileSize, atlasTiles);
                {
                    std::unique_lock jpackLock(jpackMutex);
                    jpack = std::move(pack);
                }
                AttachJavaPack();
            } else {
                jpackError = why;
                Log("javapack", "not available on {}: {}", conduit::api_name(gpu.Api()), why);
            }
        }
        Log("render", "now on {} ({})", rendererName, adapterName);
        return true;
    }
}
