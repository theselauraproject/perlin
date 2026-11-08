#include "client/renderer/gpu/GpuCore.hpp"
#include "client/renderer/gpu/ModGpu.hpp"
#include "client/renderer/gpu/ModGpuHub.hpp"

#include <perlin/gpu.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace conduit {
    std::vector<std::string> null_device_take_errors(device& device);
}

namespace {
    int failed = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                       \
            failed++;                                                                                                  \
        }                                                                                                              \
    } while (0)

    void Put(std::vector<uint8_t>& out, const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        out.insert(out.end(), b, b + n);
    }
    template <class T> void Put(std::vector<uint8_t>& out, T v) {
        Put(out, &v, sizeof(v));
    }
    void PutString(std::vector<uint8_t>& out, const std::string& s) {
        Put<uint16_t>(out, static_cast<uint16_t>(s.size()));
        Put(out, s.data(), s.size());
    }

    struct Member {
        std::string name;
        uint32_t offset, bytes;
    };
    struct ProgramSpec {
        std::string name;
        uint32_t uniformBytes;
        std::vector<Member> members;
        std::vector<uint8_t> stages;
    };

    std::vector<uint8_t> BuildPack() {
        const std::vector<ProgramSpec> programs = {
            {"quad", 32, {{"u_color", 0, 16}, {"u_scale", 16, 8}}, {0, 1}},
            {"fill", 16, {{"u_value", 0, 4}}, {2}},
            {"depthless", 0, {}, {0, 1}},
        };
        std::vector<uint8_t> index, data(64, 0xAB);
        Put<uint16_t>(index, 2);
        PutString(index, "s_tex");
        Put<uint8_t>(index, 0);
        Put<uint8_t>(index, 0);
        PutString(index, "s_noise");
        Put<uint8_t>(index, 1);
        Put<uint8_t>(index, 0);
        Put<uint32_t>(index, static_cast<uint32_t>(programs.size()));
        for (const ProgramSpec& p : programs) {
            PutString(index, p.name);
            Put<uint32_t>(index, p.uniformBytes);
            Put<uint16_t>(index, static_cast<uint16_t>(p.members.size()));
            for (const Member& m : p.members) {
                PutString(index, m.name);
                Put<uint32_t>(index, m.offset);
                Put<uint32_t>(index, m.bytes);
                Put<uint16_t>(index, 1);
            }
            Put<uint8_t>(index, static_cast<uint8_t>(p.stages.size()));
            for (uint8_t stage : p.stages) {
                Put<uint8_t>(index, stage);
                Put<uint8_t>(index, 5);
                for (uint8_t target = 0; target < 5; target++) {
                    Put<uint8_t>(index, target);
                    Put<uint32_t>(index, 0);
                    Put<uint32_t>(index, 16);
                }
            }
        }
        std::vector<uint8_t> pack;
        Put<uint32_t>(pack, 0x48534443u);
        Put<uint32_t>(pack, 1u);
        Put<uint32_t>(pack, static_cast<uint32_t>(index.size()));
        Put<uint32_t>(pack, 0u);
        Put(pack, index.data(), index.size());
        Put(pack, data.data(), data.size());
        return pack;
    }

    using perlin::gpu::ModGpuHub;

    void RunFrame(perlin::gpu::Gpu& gpu, perlin::gpu::ModGpu& mod) {
        gpu.BeginFrame();
        conduit::swapchain::image image;
        CHECK(gpu.AcquireImage(image));
        conduit::device& dev = gpu.Dev();
        conduit::command_list* list = dev.acquire_command_list(conduit::queue_type::graphics);
        CHECK(list != nullptr);
        gpu.SetState(image.texture, conduit::state::undefined);
        mod.BeginFrame(gpu.TargetWidth(), gpu.TargetHeight());
        gpu.Flush(*list);
        mod.Record(*list, perlin::gpu::ModStage::Pre, nullptr);
        perlin::gpu::ModGpu::Screen screen;
        screen.color = image.texture;
        screen.width = gpu.TargetWidth();
        screen.height = gpu.TargetHeight();
        screen.format = gpu.TargetFormat();
        mod.Record(*list, perlin::gpu::ModStage::UnderHud, &screen);
        mod.Record(*list, perlin::gpu::ModStage::Overlay, &screen);
        gpu.Transition(*list, image.texture, conduit::state::copy_src);
        gpu.FlushRings();
        dev.submit(conduit::queue_type::graphics, list);
        gpu.PresentImage();
        dev.end_frame();
        mod.EndFrame();
    }

    void Drain(const char* what, const std::string& plugin, bool expectEmpty) {
        const std::vector<std::string> errors = ModGpuHub::Instance().TakeErrors(plugin);
        if (expectEmpty)
            for (const std::string& e : errors)
                std::fprintf(stderr, "[%s] hub error: %s\n", what, e.c_str());
        if (expectEmpty)
            CHECK(errors.empty());
    }

    void CheckValidator(perlin::gpu::Gpu& gpu, const char* what) {
        const std::vector<std::string> errors = conduit::null_device_take_errors(gpu.Dev());
        for (const std::string& e : errors)
            std::fprintf(stderr, "[%s] validator: %s\n", what, e.c_str());
        CHECK(errors.empty());
    }
}

int main() {
    using namespace perlin::gpu;
    namespace S = perlin::gpu::stream;
    const std::vector<uint8_t> pack = BuildPack();

    Gpu gpu;
    GpuSettings settings;
    settings.api = conduit::api::none;
    settings.offscreen = true;
    settings.width = 640;
    settings.height = 360;
    settings.validation = true;
    std::string error;
    if (!gpu.Init(settings, pack, &error)) {
        std::fprintf(stderr, "init failed: %s\n", error.c_str());
        return 2;
    }
    ModGpu mod(gpu);
    ModGpuHub& hub = ModGpuHub::Instance();
    CHECK(!hub.Info().available);
    mod.Start("test");
    CHECK(hub.Info().available);
    CHECK(hub.Info().width == 640);
    const std::string plugin = "tester";

    ModPackSummary summary;
    std::vector<uint8_t> garbage(100, 1);
    CHECK(hub.LoadPack(plugin, garbage, summary) == HubStatus::BadArguments);
    Drain("garbage pack", plugin, false);
    CHECK(hub.LoadPack(plugin, pack, summary) == HubStatus::Ok);
    CHECK(summary.programs.size() == 3 && summary.slots.size() == 2);
    CHECK(summary.programs[0].vertex && summary.programs[0].fragment && !summary.programs[0].compute);
    CHECK(summary.programs[1].compute && !summary.programs[1].vertex);
    CHECK(summary.programs[0].uniforms.size() == 2 && summary.programs[0].uniforms[1].offset == 16);

    struct V {
        float x, y, u, v;
        uint32_t color;
    };
    const V verts[4] = {{0, 0, 0, 0, 0xFFFFFFFFu}, {1, 0, 1, 0, 0xFFFFFFFFu}, {1, 1, 1, 1, 0xFFFFFFFFu},
                        {0, 1, 0, 1, 0xFFFFFFFFu}};
    const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    uint32_t vb = 0, ib = 0, storage = 0, indirect = 0;
    ModBufferDesc bd;
    bd.usage = 1u << 0;
    bd.data.assign(reinterpret_cast<const uint8_t*>(verts), reinterpret_cast<const uint8_t*>(verts) + sizeof(verts));
    CHECK(hub.CreateBuffer(plugin, bd, vb) == HubStatus::Ok);
    bd.usage = 1u << 1;
    bd.data.assign(reinterpret_cast<const uint8_t*>(idx), reinterpret_cast<const uint8_t*>(idx) + sizeof(idx));
    CHECK(hub.CreateBuffer(plugin, bd, ib) == HubStatus::Ok);
    bd.usage = (1u << 2) | (1u << 4);
    bd.size = 256;
    bd.data.clear();
    CHECK(hub.CreateBuffer(plugin, bd, storage) == HubStatus::Ok);
    bd.usage = 1u << 3;
    bd.size = 20;
    CHECK(hub.CreateBuffer(plugin, bd, indirect) == HubStatus::Ok);
    bd.usage = 0;
    CHECK(hub.CreateBuffer(plugin, bd, indirect) == HubStatus::BadArguments);

    ModTextureDesc td;
    td.width = td.height = 4;
    td.usage = 1u << 0;
    td.data.assign(4 * 4 * 4, 0x80);
    uint32_t tex = 0, target = 0, screenCopy = 0, depth = 0;
    CHECK(hub.CreateTexture(plugin, td, tex) == HubStatus::Ok);
    td = {};
    td.width = td.height = 64;
    td.usage = (1u << 0) | (1u << 2);
    CHECK(hub.CreateTexture(plugin, td, target) == HubStatus::Ok);
    td = {};
    td.format = 1;
    td.screenSized = true;
    td.usage = (1u << 0) | (1u << 2);
    CHECK(hub.CreateTexture(plugin, td, screenCopy) == HubStatus::Ok);
    td = {};
    td.width = td.height = 64;
    td.format = 18;
    td.usage = 1u << 3;
    CHECK(hub.CreateTexture(plugin, td, depth) == HubStatus::Ok);
    td = {};
    td.width = 0;
    td.usage = 1u;
    uint32_t bad = 0;
    CHECK(hub.CreateTexture(plugin, td, bad) == HubStatus::BadArguments);

    uint32_t sampler = 0;
    CHECK(hub.CreateSampler(plugin, {}, sampler) == HubStatus::Ok);

    ModPipelineDesc pd;
    pd.pack = summary.id;
    pd.program = "quad";
    pd.streams = {{20, false}};
    pd.attributes = {{0, 0, 11, 0}, {1, 0, 11, 8}, {2, 0, 4, 16}};
    pd.blend = {{true, 4, 5, 0, 1, 5, 0, 15}};
    pd.colorFormats = {1};
    uint32_t screenPipe = 0, targetPipe = 0, computePipe = 0, depthPipe = 0, missing = 0;
    CHECK(hub.CreatePipeline(plugin, pd, screenPipe) == HubStatus::Ok);
    pd.colorFormats = {4};
    CHECK(hub.CreatePipeline(plugin, pd, targetPipe) == HubStatus::Ok);
    pd.depthFormat = 18;
    pd.depthTest = pd.depthWrite = true;
    CHECK(hub.CreatePipeline(plugin, pd, depthPipe) == HubStatus::Ok);
    pd.program = "nope";
    CHECK(hub.CreatePipeline(plugin, pd, missing) == HubStatus::NotFound);
    pd = {};
    pd.pack = summary.id;
    pd.program = "fill";
    pd.compute = true;
    pd.storage = {{0, 1}};
    CHECK(hub.CreatePipeline(plugin, pd, computePipe) == HubStatus::Ok);
    pd.program = "quad";
    CHECK(hub.CreatePipeline(plugin, pd, missing) == HubStatus::BadArguments);
    Drain("pipeline errors", plugin, false);

    {
        S::Writer w;
        S::SetPipeline sp{computePipe};
        w.add(S::Cmd::SetPipeline, sp);
        w.add(S::Cmd::BindStorage, S::BindStorage{0, storage});
        const uint32_t value = 7;
        S::SetUniforms u{0, 4};
        w.add(S::Cmd::SetUniforms, &u, sizeof(u), &value, 4);
        w.add(S::Cmd::Dispatch, S::Dispatch{4, 1, 1});
        S::FillBuffer fb;
        fb.dst = storage;
        fb.sizeLo = 64;
        fb.value = 0;
        w.add(S::Cmd::FillBuffer, fb);
        S::CopyBuffer cb;
        cb.dst = storage;
        cb.src = vb;
        cb.dstOffsetLo = 128;
        cb.sizeLo = 20;
        w.add(S::Cmd::CopyBuffer, cb);
        const auto bytes = w.finish();
        CHECK(hub.Submit(plugin, "compute", ModStage::Pre, 0, true,
                         std::vector<uint8_t>(bytes.begin(), bytes.end())) == HubStatus::Ok);
    }
    {
        S::Writer w;
        S::BeginPass bp;
        bp.colorCount = 1;
        bp.colors[0].texture = target;
        bp.colors[0].load = static_cast<uint32_t>(S::Load::Clear);
        w.add(S::Cmd::BeginPass, bp);
        w.add(S::Cmd::SetPipeline, S::SetPipeline{targetPipe});
        w.add(S::Cmd::BindTexture, S::BindTexture{0, tex, sampler, 0});
        w.add(S::Cmd::SetVertexBuffer, S::SetVertexBuffer{0, vb, 0, 0});
        w.add(S::Cmd::SetIndexBuffer, S::SetIndexBuffer{ib, 1, 0, 0});
        w.add(S::Cmd::DrawIndexed, S::DrawIndexed{6, 1, 0, 0, 0});
        w.add(S::Cmd::EndPass);
        const auto bytes = w.finish();
        CHECK(hub.Submit(plugin, "offscreen", ModStage::Pre, 1, false,
                         std::vector<uint8_t>(bytes.begin(), bytes.end())) == HubStatus::Ok);
    }
    {
        S::Writer w;
        w.add(S::Cmd::CopyScreen, S::CopyScreen{screenCopy});
        S::BeginPass bp;
        bp.colorCount = 1;
        bp.colors[0].texture = 0;
        w.add(S::Cmd::BeginPass, bp);
        w.add(S::Cmd::SetPipeline, S::SetPipeline{screenPipe});
        w.add(S::Cmd::BindTexture, S::BindTexture{0, target, sampler, 0});
        w.add(S::Cmd::BindTexture, S::BindTexture{1, screenCopy, 0, 0});
        w.add(S::Cmd::SetVertexBuffer, S::SetVertexBuffer{0, vb, 0, 0});
        w.add(S::Cmd::SetIndexBuffer, S::SetIndexBuffer{ib, 1, 0, 0});
        w.add(S::Cmd::SetViewport, S::SetViewport{0, 0, 320, 180, 0, 1});
        w.add(S::Cmd::SetScissor, S::SetScissor{10, 10, 300, 100});
        w.add(S::Cmd::DrawIndexed, S::DrawIndexed{6, 1, 0, 0, 0});
        w.add(S::Cmd::EndPass);
        const auto bytes = w.finish();
        CHECK(hub.Submit(plugin, "under", ModStage::UnderHud, 0, false,
                         std::vector<uint8_t>(bytes.begin(), bytes.end())) == HubStatus::Ok);
    }
    {
        S::Writer w;
        S::BeginPass bp;
        bp.colorCount = 1;
        bp.colors[0].texture = 0;
        bp.colors[0].load = static_cast<uint32_t>(S::Load::Load);
        w.add(S::Cmd::BeginPass, bp);
        w.add(S::Cmd::SetPipeline, S::SetPipeline{screenPipe});
        w.add(S::Cmd::BindTexture, S::BindTexture{0, tex, 0, 0});
        w.add(S::Cmd::SetVertexBuffer, S::SetVertexBuffer{0, vb, 0, 0});
        w.add(S::Cmd::Draw, S::Draw{4, 1, 0, 0});
        w.add(S::Cmd::EndPass);
        const auto bytes = w.finish();
        CHECK(hub.Submit(plugin, "over", ModStage::Overlay, 0, false,
                         std::vector<uint8_t>(bytes.begin(), bytes.end())) == HubStatus::Ok);
    }
    CHECK(hub.ReadBuffer(plugin, storage, 128, 20, 77) == HubStatus::Ok);

    // MSAA + resolve, depth, indirect draws and storage textures.
    {
        uint32_t msaa = 0, resolve = 0, depthTex = 0, indirectBuf = 0, storageTex = 0, msaaPipe = 0, texComputePipe = 0;
        ModTextureDesc m;
        m.width = m.height = 64;
        m.samples = 4;
        m.format = 4;
        m.usage = 1u << 2;
        CHECK(hub.CreateTexture(plugin, m, msaa) == HubStatus::Ok);
        m.samples = 1;
        m.usage = (1u << 0) | (1u << 2);
        CHECK(hub.CreateTexture(plugin, m, resolve) == HubStatus::Ok);
        m.format = 18;
        m.usage = 1u << 3;
        m.samples = 4;
        CHECK(hub.CreateTexture(plugin, m, depthTex) == HubStatus::Ok);
        m = {};
        m.width = m.height = 16;
        m.format = 4;
        m.usage = (1u << 0) | (1u << 1);
        CHECK(hub.CreateTexture(plugin, m, storageTex) == HubStatus::Ok);
        const uint32_t args[5] = {6, 1, 0, 0, 0};
        ModBufferDesc ab;
        ab.usage = 1u << 3;
        ab.data.assign(reinterpret_cast<const uint8_t*>(args), reinterpret_cast<const uint8_t*>(args) + sizeof(args));
        CHECK(hub.CreateBuffer(plugin, ab, indirectBuf) == HubStatus::Ok);
        ModPipelineDesc pm;
        pm.pack = summary.id;
        pm.program = "quad";
        pm.streams = {{20, false}};
        pm.attributes = {{0, 0, 11, 0}, {1, 0, 11, 8}, {2, 0, 4, 16}};
        pm.colorFormats = {4};
        pm.depthFormat = 18;
        pm.depthTest = pm.depthWrite = true;
        pm.samples = 4;
        CHECK(hub.CreatePipeline(plugin, pm, msaaPipe) == HubStatus::Ok);
        ModPipelineDesc pc;
        pc.pack = summary.id;
        pc.program = "fill";
        pc.compute = true;
        pc.storage = {{0, 2}};
        CHECK(hub.CreatePipeline(plugin, pc, texComputePipe) == HubStatus::Ok);
        S::Writer w;
        S::BeginPass bp;
        bp.colorCount = 1;
        bp.hasDepth = 1;
        bp.colors[0].texture = msaa;
        bp.colors[0].resolve = resolve;
        bp.colors[0].load = static_cast<uint32_t>(S::Load::Clear);
        bp.depth.texture = depthTex;
        w.add(S::Cmd::BeginPass, bp);
        w.add(S::Cmd::SetPipeline, S::SetPipeline{msaaPipe});
        w.add(S::Cmd::SetVertexBuffer, S::SetVertexBuffer{0, vb, 0, 0});
        w.add(S::Cmd::SetIndexBuffer, S::SetIndexBuffer{ib, 1, 0, 0});
        w.add(S::Cmd::DrawIndexedIndirect, S::DrawIndexedIndirect{indirectBuf, 0, 0, 1, 20});
        w.add(S::Cmd::EndPass);
        w.add(S::Cmd::SetPipeline, S::SetPipeline{texComputePipe});
        w.add(S::Cmd::BindStorage, S::BindStorage{0, storageTex});
        w.add(S::Cmd::Dispatch, S::Dispatch{2, 2, 1});
        w.add(S::Cmd::BeginPass, [] {
            S::BeginPass p;
            p.colorCount = 1;
            return p;
        }());
        w.add(S::Cmd::SetPipeline, S::SetPipeline{screenPipe});
        w.add(S::Cmd::BindTexture, S::BindTexture{0, resolve, 0, 0});
        w.add(S::Cmd::BindTexture, S::BindTexture{1, storageTex, 0, 0});
        w.add(S::Cmd::SetVertexBuffer, S::SetVertexBuffer{0, vb, 0, 0});
        w.add(S::Cmd::Draw, S::Draw{4, 1, 0, 0});
        w.add(S::Cmd::EndPass);
        const auto bytes = w.finish();
        CHECK(hub.Submit(plugin, "advanced", ModStage::UnderHud, 5, false,
                         std::vector<uint8_t>(bytes.begin(), bytes.end())) == HubStatus::Ok);
    }
    for (int i = 0; i < 6; i++)
        RunFrame(gpu, mod);
    Drain("main", plugin, true);
    CheckValidator(gpu, "main");
    const conduit::device_stats stats = gpu.Dev().stats();
    CHECK(stats.draws >= 6 * 3);
    CHECK(stats.dispatches >= 1);

    std::vector<ModReadback> reads;
    hub.TakeReadbacks(reads);
    CHECK(reads.size() == 1 && reads[0].tag == 77 && reads[0].ok && reads[0].data.size() == 20);
    if (reads.size() == 1 && reads[0].data.size() == 20)
        { CHECK(std::memcmp(reads[0].data.data(), verts, 20) == 0); for (int i = 0; i < 20; i++) std::fprintf(stderr, "%02x ", reads[0].data[i]); std::fprintf(stderr, "\n"); }

    {
        S::Writer w;
        w.add(S::Cmd::Draw, S::Draw{3, 1, 0, 0});
        w.add(S::Cmd::SetPipeline, S::SetPipeline{9999});
        w.add(S::Cmd::BeginPass, S::BeginPass{});
        w.add(S::Cmd::EndPass);
        S::BeginPass bp;
        bp.colorCount = 1;
        bp.colors[0].texture = 4242;
        w.add(S::Cmd::BeginPass, bp);
        w.add(S::Cmd::SetPipeline, S::SetPipeline{screenPipe});
        w.add(S::Cmd::Draw, S::Draw{3, 1, 0, 0});
        w.add(S::Cmd::EndPass);
        bp.colors[0].texture = 0;
        w.add(S::Cmd::BeginPass, bp);
        w.add(S::Cmd::SetPipeline, S::SetPipeline{targetPipe});
        w.add(S::Cmd::SetVertexBuffer, S::SetVertexBuffer{0, vb, 0, 0});
        w.add(S::Cmd::Draw, S::Draw{3, 1, 0, 0});
        w.add(S::Cmd::EndPass);
        w.add(S::Cmd::SetPipeline, S::SetPipeline{screenPipe});
        w.add(S::Cmd::Dispatch, S::Dispatch{1, 1, 1});
        w.add(S::Cmd::CopyBuffer, S::CopyBuffer{});
        const auto bytes = w.finish();
        CHECK(hub.Submit(plugin, "broken", ModStage::Overlay, 5, false,
                         std::vector<uint8_t>(bytes.begin(), bytes.end())) == HubStatus::Ok);
        std::vector<uint8_t> truncated(bytes.begin(), bytes.end() - 4);
        CHECK(hub.Submit(plugin, "truncated", ModStage::Overlay, 5, false, truncated) == HubStatus::BadArguments);
    }
    const uint64_t drawsBefore = gpu.Dev().stats().draws;
    RunFrame(gpu, mod);
    CHECK(gpu.Dev().stats().draws == drawsBefore + 6);
    const std::vector<std::string> broken = hub.TakeErrors(plugin);
    CHECK(broken.size() >= 5);
    for (const std::string& e : broken)
        std::fprintf(stderr, "expected error: %s\n", e.c_str());
    CheckValidator(gpu, "broken stream");
    CHECK(hub.ClearLayer(plugin, "broken") == HubStatus::Ok);

    hub.SetScreen(640, 360, 4);
    for (int i = 0; i < 2; i++) {
        gpu.Resize(800, 450);
        RunFrame(gpu, mod);
    }
    Drain("resize", plugin, true);
    CheckValidator(gpu, "resize");

    CHECK(hub.Destroy(plugin, ModResource::Texture, target) == HubStatus::Ok);
    CHECK(hub.Destroy(plugin, ModResource::Texture, target) == HubStatus::NotFound);
    CHECK(hub.Destroy(plugin, ModResource::Pipeline, targetPipe) == HubStatus::Ok);
    RunFrame(gpu, mod);
    const std::vector<std::string> afterDestroy = hub.TakeErrors(plugin);
    CHECK(!afterDestroy.empty());
    CheckValidator(gpu, "destroyed resource");
    CHECK(hub.ClearLayer(plugin, "under") == HubStatus::Ok);
    RunFrame(gpu, mod);
    CheckValidator(gpu, "after destroy");

    hub.DropPlugin(plugin);
    RunFrame(gpu, mod);
    CHECK(gpu.Dev().stats().live_pipelines <= 5);

    CHECK(hub.LoadPack("other", pack, summary) == HubStatus::Ok);
    mod.Shutdown();
    std::vector<std::pair<std::string, uint32_t>> resets;
    hub.TakeResets(resets);
    CHECK(resets.size() == 1 && resets[0].first == "other");
    CHECK(!hub.Info().available);
    CHECK(hub.LoadPack("other", pack, summary) == HubStatus::Unavailable);

    if (failed) {
        std::fprintf(stderr, "mod_gpu_selftest: %d failure(s)\n", failed);
        return 1;
    }
    std::printf("mod_gpu_selftest: ok\n");
    return 0;
}
