// ui, entity and particle draws through the instanced path on the null device: neighbours with the same state
// become multi draws, world draws go through the gpu cull pass, and the PERLIN_NO_GPU_UI fallback still draws
#include "client/renderer/Frustum.hpp"
#include "client/renderer/gpu/RenderContext_rhi.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

using namespace perlin;

int main(int argc, char** argv) {
    if (argc < 2)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<uint8_t> pack{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    gpu::GpuSettings settings;
    settings.api = conduit::api_from_id("null");
    settings.width = 320;
    settings.height = 240;
    settings.offscreen = true;
    settings.validation = true;
    RenderContext_rhi ctx;
    std::string error;
    if (!ctx.InitializeOffscreen(settings, pack, &error)) {
        std::printf("no device: %s\n", error.c_str());
        return 77;
    }
    const UiVertex quad[4] = {{0, 0, 0, 0xFFFFFFFFu, 0, 0}, {1, 0, 0, 0xFFFFFFFFu, 1, 0},
                              {1, 1, 0, 0xFFFFFFFFu, 1, 1}, {0, 1, 0, 0xFFFFFFFFu, 0, 1}};
    const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    const uint32_t mesh = ctx.UiCreateMesh(quad, 4, idx, 6);
    std::vector<uint8_t> px(16 * 16 * 4, 255);
    const uint32_t tex = ctx.UiCreateTexture(16, 16, px.data(), false);

    FrameParams f{};
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 2, -10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(70.0f), 4.0f / 3.0f, 0.05f, 300.0f);
    const glm::mat4 vp = proj * view;
    std::memcpy(f.viewProjection, glm::value_ptr(vp), 64);
    std::memcpy(f.skyViewProjection, glm::value_ptr(vp), 64);
    std::memcpy(f.view, glm::value_ptr(view), 64);
    std::memcpy(f.projection, glm::value_ptr(proj), 64);
    f.renderScale = 1.0f;
    std::memset(f.lightmap, 255, sizeof(f.lightmap));
    uint64_t lastDraws = 0, lastIndirect = 0;
    for (int frame = 0; frame < 3; frame++) {
        ctx.BeginFrame(f);
        for (int i = 0; i < 200; i++) {
            UiDraw d;
            d.material = i < 150 ? DrawMaterial::Entity : DrawMaterial::Particle;
            d.mesh = mesh;
            d.texture = tex;
            d.depth = true;
            // half in front of the camera, half far behind it: the cull pass drops those
            const glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3((i % 20) - 10.0f, 0.0f, i < 100 ? 5.0f : -60.0f));
            std::memcpy(d.model, glm::value_ptr(m), 64);
            ctx.UiSubmit(UiLayer::World, d);
        }
        for (int i = 0; i < 100; i++) {
            UiDraw d;
            d.mesh = mesh;
            d.texture = tex;
            const glm::mat4 m = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(i * 3.0f, 10.0f, 0.0f)), glm::vec3(8.0f));
            std::memcpy(d.model, glm::value_ptr(m), 64);
            ctx.UiSubmit(UiLayer::Gui, d);
        }
        const auto before = ctx.Gpu().Dev().stats();
        ctx.EndFrame();
        const auto after = ctx.Gpu().Dev().stats();
        lastDraws = after.draws - before.draws;
        lastIndirect = after.indirect_draws - before.indirect_draws;
    }
    std::printf("draws %llu, indirect %llu\n", static_cast<unsigned long long>(lastDraws),
                static_cast<unsigned long long>(lastIndirect));
    const bool batching = std::getenv("PERLIN_NO_GPU_UI") == nullptr;
    const bool ok = batching ? lastIndirect >= 3 : lastDraws >= 300;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", batching ? "ui draws batch into multi draws" : "fallback draws one by one");
    return ok ? 0 : 1;
}
