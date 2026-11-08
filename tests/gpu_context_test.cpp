#include "client/renderer/Frustum.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/gpu/RenderContext_rhi.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

using namespace perlin;

namespace {
    int failures = 0;
    void Check(bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok)
            failures++;
    }

    struct Pixel {
        int r, g, b;
    };

    Pixel At(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t x, uint32_t y) {
        const uint8_t* p = rgba.data() + (static_cast<size_t>(y) * w + x) * 4;
        return {p[0], p[1], p[2]};
    }

    bool Red(Pixel p) {
        return p.r > 150 && p.g < 90 && p.b < 90;
    }
    bool Background(Pixel p) {
        return p.b > 150 && p.r < 90 && p.g < 90;
    }

    FrameParams MakeFrame(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up, uint32_t w,
                          uint32_t h) {
        FrameParams f{};
        const glm::mat4 view = glm::lookAt(eye, target, up);
        const glm::mat4 proj = glm::perspectiveRH_ZO(
            glm::radians(70.0f), static_cast<float>(w) / static_cast<float>(h), 0.1f, 200.0f);
        const glm::mat4 vp = proj * view;
        std::memcpy(f.viewProjection, glm::value_ptr(vp), sizeof(f.viewProjection));
        std::memcpy(f.skyViewProjection, glm::value_ptr(vp), sizeof(f.skyViewProjection));
        std::memcpy(f.view, glm::value_ptr(view), sizeof(f.view));
        std::memcpy(f.projection, glm::value_ptr(proj), sizeof(f.projection));
        f.cameraPosition[0] = eye.x;
        f.cameraPosition[1] = eye.y;
        f.cameraPosition[2] = eye.z;
        f.fogColor[2] = 1.0f;
        f.fogColor[3] = 1.0f;
        f.environmentalFogStart = 1000.0f;
        f.environmentalFogEnd = 2000.0f;
        f.renderDistanceFogStart = 1000.0f;
        f.renderDistanceFogEnd = 2000.0f;
        f.skyFogEnd = 2000.0f;
        f.cloudFogEnd = 2000.0f;
        f.renderScale = 1.0f;
        f.frameGen = 0;
        f.timeSeconds = 1.0f;
        std::memset(f.lightmap, 255, sizeof(f.lightmap));
        return f;
    }

    bool Frame(RenderContext_rhi& ctx, const FrameParams& f, std::vector<uint8_t>& rgba, uint32_t& w,
               uint32_t& h) {
        ctx.BeginFrame(f);
        ctx.DrawChunkMeshes(Frustum::FromViewProjection(glm::make_mat4(f.viewProjection)),
                            f.cameraPosition[0], f.cameraPosition[2]);
        ctx.EndFrame();
        return ctx.ReadBackFrame(rgba, w, h);
    }
}

namespace perlin {
    void TestSetBundledPack(bool on);
}

namespace {
    void WriteText(const std::filesystem::path& file, const std::string& text) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << text;
    }

    std::filesystem::path WriteSyntheticPack() {
        const std::filesystem::path root = std::filesystem::temp_directory_path() / "perlin_synthetic_pack";
        std::filesystem::remove_all(root);
        const std::string vertexTerrain =
            "#version 120\nvarying vec2 texcoord;\nvarying vec4 glcolor;\nattribute vec4 mc_Entity;\n"
            "void main() { gl_Position = ftransform(); texcoord = (gl_TextureMatrix[0] * "
            "gl_MultiTexCoord0).xy; glcolor = gl_Color; }\n";
        const std::string fragmentTerrain =
            "#version 120\nuniform sampler2D texture;\nvarying vec2 texcoord;\nvarying vec4 glcolor;\n/* "
            "DRAWBUFFERS:0 */\n"
            "void main() { gl_FragData[0] = texture2D(texture, texcoord) * glcolor; }\n";
        const std::string vertexScreen = "#version 120\nvarying vec2 texcoord;\nvoid main() { gl_Position = "
                                         "ftransform(); texcoord = gl_MultiTexCoord0.xy; }\n";
        const std::string fragmentComposite =
            "#version 120\nuniform sampler2D colortex0;\nvarying vec2 texcoord;\n/* DRAWBUFFERS:0 */\n"
            "void main() { vec4 c = texture2D(colortex0, texcoord); gl_FragData[0] = vec4(c.g, c.r, c.b, "
            "1.0); }\n";
        const std::string fragmentFinal =
            "#version 120\nuniform sampler2D colortex0;\nvarying vec2 texcoord;\n"
            "void main() { gl_FragColor = texture2D(colortex0, texcoord); }\n";
        WriteText(root / "shaders/gbuffers_terrain.vsh", vertexTerrain);
        WriteText(root / "shaders/gbuffers_terrain.fsh", fragmentTerrain);
        WriteText(root / "shaders/composite.vsh", vertexScreen);
        WriteText(root / "shaders/composite.fsh", fragmentComposite);
        WriteText(root / "shaders/final.vsh", vertexScreen);
        WriteText(root / "shaders/final.fsh", fragmentFinal);
        return root;
    }

    std::filesystem::path WriteShadowPack() {
        const std::filesystem::path root = WriteSyntheticPack();
        WriteText(root / "shaders/shadow.vsh",
                  "#version 120\nvarying vec2 texcoord;\nvoid main() { gl_Position = ftransform(); texcoord "
                  "= (gl_TextureMatrix[0] * gl_MultiTexCoord0).xy; }\n");
        WriteText(root / "shaders/shadow.fsh",
                  "#version 120\nuniform sampler2D texture;\nvarying vec2 texcoord;\n"
                  "void main() { gl_FragData[0] = texture2D(texture, texcoord); }\n");
        WriteText(root / "shaders/gbuffers_terrain.vsh",
                  "#version 120\nvarying vec3 viewPos;\nvoid main() { vec4 v = gl_ModelViewMatrix * "
                  "gl_Vertex; viewPos = v.xyz; gl_Position = gl_ProjectionMatrix * v; }\n");
        WriteText(root / "shaders/gbuffers_terrain.fsh",
                  "#version 120\nuniform sampler2DShadow shadowtex0;\nuniform mat4 "
                  "gbufferModelViewInverse;\nuniform mat4 shadowModelView;\n"
                  "uniform mat4 shadowProjection;\nvarying vec3 viewPos;\n/* DRAWBUFFERS:0 */\n"
                  "void main() {\n"
                  "    vec4 feet = gbufferModelViewInverse * vec4(viewPos, 1.0);\n"
                  "    vec4 sp = shadowProjection * (shadowModelView * feet);\n"
                  "    sp.xyz = sp.xyz * 0.5 + 0.5;\n"
                  "    float lit = shadow2D(shadowtex0, vec3(sp.xy, sp.z - 0.003)).r;\n"
                  "    gl_FragData[0] = vec4(0.0, lit, 0.0, 1.0);\n"
                  "}\n");
        return root;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: gpu_context_test <pack> [api]\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<uint8_t> pack{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    gpu::GpuSettings settings;
    settings.api = argc > 2 ? conduit::api_from_id(argv[2]) : conduit::api::vulkan;
    settings.width = 128;
    settings.height = 96;
    settings.offscreen = true;
    settings.validation = true;

    RenderContext_rhi ctx;
    std::string error;
    if (!ctx.InitializeOffscreen(settings, pack, &error)) {
        std::printf("no device: %s\n", error.c_str());
        return 77;
    }

    for (bool diagonal : {false, true}) {
        Tessellator tess;
        const float top[4][3] = {{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}};
        const float colors[4][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
        const float uv[4][2] = {{0, 0}, {0.01f, 0}, {0.01f, 0.01f}, {0, 0.01f}};
        tess.Begin();
        tess.AddQuad(top, colors, uv, diagonal, 0, 15);
        tess.End();
        bool upward = true;
        for (const Vertex& vertex : tess.Vertices())
            upward &= ((vertex.normal >> 8) & 255) == 255;
        Check(upward, diagonal ? "alternate triangle diagonal preserves upward normals"
                               : "top face normal points upward");
    }

    std::vector<uint8_t> tile(16 * 16 * 4);
    for (size_t i = 0; i < tile.size(); i += 4) {
        tile[i] = 255;
        tile[i + 1] = 0;
        tile[i + 2] = 0;
        tile[i + 3] = 255;
    }
    ctx.UploadAtlas(tile.data(), 16, 1);

    Tessellator tess;
    tess.Begin();
    tess.EnterSection(0);
    const float v[4][3] = {{-2, 0, -2}, {2, 0, -2}, {2, 0, 2}, {-2, 0, 2}};
    const float c[4][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
    const float uv[4][2] = {{0, 0}, {0.01f, 0}, {0.01f, 0.01f}, {0, 0.01f}};
    tess.AddQuad(v, c, uv, false, 15.0f, 15.0f);
    tess.End();
    ctx.UpdateChunkMesh(0, 0, tess, 1);

    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;

    Check(Frame(ctx, MakeFrame({0, 3, 0}, {0, 0, 0}, {0, 0, -1}, 128, 96), rgba, w, h),
          "frame read back from above");
    if (!rgba.empty()) {
        Check(Red(At(rgba, w, w / 2, h / 2)), "front face visible from above");
        Check(Background(At(rgba, w, 1, 1)), "corner shows the clear colour");
    }
    Check(Frame(ctx, MakeFrame({0, -3, 0}, {0, 0, 0}, {0, 0, 1}, 128, 96), rgba, w, h),
          "frame read back from below");
    if (!rgba.empty())
        Check(!Red(At(rgba, w, w / 2, h / 2)), "back face culled from below");

    for (int order = 0; order < 2; order++) {
        Tessellator stack;
        stack.Begin();
        stack.EnterSection(0);
        const float nearV[4][3] = {{-2, 1, -2}, {2, 1, -2}, {2, 1, 2}, {-2, 1, 2}};
        const float farV[4][3] = {{-2, 0, -2}, {2, 0, -2}, {2, 0, 2}, {-2, 0, 2}};
        const float bright[4][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
        const float dim[4][3] = {
            {0.4f, 0.4f, 0.4f}, {0.4f, 0.4f, 0.4f}, {0.4f, 0.4f, 0.4f}, {0.4f, 0.4f, 0.4f}};
        if (order == 0) {
            stack.AddQuad(farV, dim, uv, false, 15.0f, 15.0f);
            stack.AddQuad(nearV, bright, uv, false, 15.0f, 15.0f);
        } else {
            stack.AddQuad(nearV, bright, uv, false, 15.0f, 15.0f);
            stack.AddQuad(farV, dim, uv, false, 15.0f, 15.0f);
        }
        stack.End();
        ctx.UpdateChunkMesh(0, 0, stack, 10 + order);
        Check(Frame(ctx, MakeFrame({0, 3, 0}, {0, 0, 0}, {0, 0, -1}, 128, 96), rgba, w, h),
              "frame with stacked quads");
        if (!rgba.empty()) {
            const Pixel p = At(rgba, w, w / 2, h / 2);
            Check(p.r > 200,
                  order == 0 ? "near quad wins (far drawn first)" : "near quad wins (near drawn first)");
        }
    }

    Tessellator right;
    right.Begin();
    right.EnterSection(0);
    const float vr[4][3] = {{0.5f, 0, -2}, {2, 0, -2}, {2, 0, 2}, {0.5f, 0, 2}};
    right.AddQuad(vr, c, uv, false, 15.0f, 15.0f);
    right.End();
    ctx.UpdateChunkMesh(0, 0, right, 2);
    Check(Frame(ctx, MakeFrame({0, 3, 0}, {0, 0, 0}, {0, 0, -1}, 128, 96), rgba, w, h),
          "frame with the right-hand quad");
    if (!rgba.empty()) {
        Check(Red(At(rgba, w, w - 20, h / 2)), "+x lands on the right");
        Check(Background(At(rgba, w, 20, h / 2)), "-x stays background");
    }

    const char* switchTo = std::getenv("PERLIN_TEST_SWITCH_TO");
    Check(ctx.RebuildDevice(switchTo ? switchTo : conduit::api_id(settings.api)), "device rebuilt");
    Check(Frame(ctx, MakeFrame({0, 3, 0}, {0, 0, 0}, {0, 0, -1}, 128, 96), rgba, w, h),
          "frame after rebuild");
    if (!rgba.empty()) {
        Check(Red(At(rgba, w, w - 20, h / 2)), "mesh and atlas restored");
        Check(Background(At(rgba, w, 20, h / 2)), "layout unchanged after rebuild");
    }

    struct PostCase {
        const char* name;
        float scale;
        int upscaler;
        float bloom, blur, sharp;
    };
    const PostCase cases[] = {
        PostCase{"scaled bilinear", 0.5f, 1, 0, 0, 0},
        PostCase{"scaled spatial sharpened", 0.5f, 2, 0, 0, 0.5f}, PostCase{"bloom", 1.0f, 0, 0.6f, 0, 0},
        PostCase{"motion blur", 1.0f, 0, 0, 0.5f, 0}, PostCase{"temporal", 0.5f, 3, 0.4f, 0.3f, 0.3f}};
    for (const PostCase& pc : cases) {

        ctx.RemoveChunkMesh(0, 0);
        Frame(ctx, MakeFrame({0, 3, 0}, {0, 0, 0}, {0, 0, -1}, 128, 96), rgba, w, h);
        ctx.UpdateChunkMesh(0, 0, right, 20 + static_cast<uint64_t>(&pc - cases));
        bool got = true;
        for (int i = 0; i < 4 && got; i++) {
            FrameParams f = MakeFrame({0, 3, 0}, {0, 0, 0}, {0, 0, -1}, 128, 96);
            f.renderScale = pc.scale;
            f.upscaler = pc.upscaler;
            f.bloom = pc.bloom;
            f.motionBlur = pc.blur;
            f.upscaleSharpness = pc.sharp;
            f.timeSeconds = 0.02f * static_cast<float>(i);
            got = Frame(ctx, f, rgba, w, h);
        }
        Check(got, (std::string("post frame: ") + pc.name).c_str());
        if (got && !rgba.empty()) {
            Check(w == 128 && h == 96, (std::string("post size: ") + pc.name).c_str());
            if (!Red(At(rgba, w, w - 36, h / 2))) {
                const auto p = At(rgba, w, w - 36, h / 2);
                std::printf("     pixel %d %d %d\n", p.r, p.g, p.b);
            }
            Check(Red(At(rgba, w, w - 36, h / 2)), (std::string("post right quad: ") + pc.name).c_str());
            Check(!Red(At(rgba, w, 20, h / 2)), (std::string("post left empty: ") + pc.name).c_str());
        }
    }
    {
        FrameParams f = MakeFrame({0, 3, 0}, {0, 0, 0}, {0, 0, -1}, 128, 96);
        Check(Frame(ctx, f, rgba, w, h), "direct frame after post");
        if (!rgba.empty())
            Check(Red(At(rgba, w, w - 36, h / 2)), "direct path restored");
    }

    ctx.RemoveChunkMesh(0, 0);
    Check(Frame(ctx, MakeFrame({0, 3, 0}, {0, 0, 0}, {0, 0, -1}, 128, 96), rgba, w, h),
          "frame after removal");
    if (!rgba.empty())
        Check(Background(At(rgba, w, w / 2, h / 2)), "removed mesh is gone");

    {
        Tessellator lodTile;
        lodTile.Begin();
        const float tv[4][3] = {{-64, 60, -192}, {0, 60, -192}, {0, 60, -128}, {-64, 60, -128}};
        lodTile.AddQuad(tv, c, uv, false, 15.0f, 15.0f);
        lodTile.End();

        for (int tz = -10; tz <= 10; tz++)
            for (int tx = -10; tx <= 10; tx++)
                if (!(tx == -1 && tz == -3)) {
                    Tessellator far;
                    far.Begin();
                    const float fx = static_cast<float>(tx * 64), fz = static_cast<float>(tz * 64);
                    const float fv[4][3] = {
                        {fx, 60, fz}, {fx + 64, 60, fz}, {fx + 64, 60, fz + 64}, {fx, 60, fz + 64}};
                    far.AddQuad(fv, c, uv, false, 15.0f, 15.0f);
                    far.End();

                    if (tz > -1 || std::abs(tx) > 2)
                        ctx.UpdateLodTile(tx, tz, 64, far, 1);
                }
        ctx.UpdateLodTile(-1, -3, 64, lodTile, 1);
        const FrameParams f = MakeFrame({-32, 100, 0}, {-32, 60, -168}, {0, 1, 0}, 128, 96);
        Check(Frame(ctx, f, rgba, w, h), "frame with lod tiles");
        if (!rgba.empty())
            Check(Red(At(rgba, w, w / 2, h / 2)), "lod tile in view is drawn");
        ctx.RemoveLodTile(-1, -3);
        Check(Frame(ctx, f, rgba, w, h), "frame after lod removal");
        if (!rgba.empty())
            Check(!Red(At(rgba, w, w / 2, h / 2)), "removed lod tile is gone");
    }

    perlin::TestSetBundledPack(true);
    Check(ctx.ReloadShaders(), "bundled variant resolves");
    {
        Tessellator scene;
        scene.Begin();
        scene.EnterSection(0);
        const float ground[4][3] = {{-3, 0, -3}, {3, 0, -3}, {3, 0, 3}, {-3, 0, 3}};
        const float occl[4][3] = {{-1, 1.5f, -1}, {0.2f, 1.5f, -1}, {0.2f, 1.5f, 1}, {-1, 1.5f, 1}};
        scene.AddQuad(ground, c, uv, false, 0.0f, 15.0f);
        scene.AddQuad(occl, c, uv, false, 0.0f, 15.0f);
        scene.End();
        ctx.UpdateChunkMesh(0, 0, scene, 7);
        std::vector<uint8_t> lit, dark;
        const auto shot = [&](int quality, std::vector<uint8_t>& out) {
            bool ok = true;
            for (int i = 0; i < 3 && ok; i++) {
                FrameParams f = MakeFrame({0.5f, 5, 5}, {0.5f, 0, 0}, {0, 1, 0}, 128, 96);
                f.shadowQuality = quality;
                f.shadowEntities = false;
                f.sky.enabled = true;
                f.waterTile = f.lavaTile = f.waterFlowTile = -1.0f;
                f.sky.sunAngle = 0.6f;

                for (float& v : f.packSettings)
                    v = 0.5f;
                f.timeSeconds = 2.0f + 0.5f * static_cast<float>(i);
                ok = Frame(ctx, f, out, w, h);
            }
            return ok;
        };
        Check(shot(0, lit), "frame without shadows");
        Check(shot(2, dark), "frame with shadows");
        if (!lit.empty() && lit.size() == dark.size()) {
            int darker = 0, brighter = 0;
            for (size_t i = 0; i + 3 < lit.size(); i += 4) {
                const int a = lit[i] + lit[i + 1] + lit[i + 2], b = dark[i] + dark[i + 1] + dark[i + 2];
                darker += b < a - 6;
                brighter += b > a + 6;
            }
            if (const char* dump = std::getenv("PERLIN_TEST_DUMP"))
                for (int k = 0; k < 2; k++) {
                    const std::string path = std::string(dump) + (k ? "_dark.ppm" : "_lit.ppm");
                    if (FILE* fp = std::fopen(path.c_str(), "wb")) {
                        std::fprintf(fp, "P6\n%u %u\n255\n", w, h);
                        const std::vector<uint8_t>& img = k ? dark : lit;
                        for (size_t i = 0; i + 3 < img.size(); i += 4)
                            std::fwrite(&img[i], 1, 3, fp);
                        std::fclose(fp);
                    }
                }
            std::printf("     shadow pixels: %d darker, %d brighter\n", darker, brighter);
            Check(darker > 40, "the occluder darkens the ground");
        }

        {
            bool ok = true;
            for (int i = 0; i < 3 && ok; i++) {
                FrameParams f = MakeFrame({0.5f, 5, 5}, {0.5f, 0, 0}, {0, 1, 0}, 128, 96);
                f.shadowQuality = 2;
                f.shadowEntities = false;
                f.sky.enabled = true;
                f.sky.sunAngle = 0.6f;
                f.waterTile = f.lavaTile = f.waterFlowTile = -1.0f;
                for (float& v : f.packSettings)
                    v = 0.5f;
                f.timeSeconds = 5.0f + static_cast<float>(i);
                ok = Frame(ctx, f, rgba, w, h);
            }
            Check(ok, "frame with the scene copy");
            if (ok && !rgba.empty())
                Check(!Background(At(rgba, w, w / 2, h / 2)), "scene copy keeps the ground");
        }
    }

    TestSetBundledPack(false);
    ctx.ReloadShaders();
    ctx.UnloadLevelResources();
    ctx.UnloadLevelResources();
    const auto afterLeave = MakeFrame({0, 3, 0}, {0, 0, 0}, {0, 0, -1}, 128, 96);
    Check(Frame(ctx, afterLeave, rgba, w, h), "frame after leaving the level");
    if (!rgba.empty())
        Check(Background(At(rgba, w, w / 2, h / 2)), "previous level mesh is gone");
    ctx.UploadAtlas(tile.data(), 16, 1);
    ctx.UpdateChunkMesh(0, 0, tess, 100);
    Check(Frame(ctx, afterLeave, rgba, w, h), "frame after loading the next level");
    if (!rgba.empty())
        Check(Red(At(rgba, w, w / 2, h / 2)), "next level atlas and mesh draw correctly");

    {
        const std::filesystem::path root = WriteSyntheticPack();
        ctx.SetBlockNameLookup([](uint16_t) { return std::string("stone"); });
        std::string why;
        if (!ctx.SetJavaShaderPack(root.string(), {}, why)) {
            std::printf("skip java pack: %s\n", why.c_str());
        } else {
            Check(ctx.JavaShaderPackActive(), "java pack loaded");
            Tessellator packTess;
            packTess.Begin();
            packTess.EnterSection(0);
            const float pv[4][3] = {{-2, 0, -2}, {2, 0, -2}, {2, 0, 2}, {-2, 0, 2}};
            const float pc[4][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
            const float puv[4][2] = {{0, 0}, {0.01f, 0}, {0.01f, 0.01f}, {0, 0.01f}};
            packTess.AddQuad(pv, pc, puv, false, 15.0f, 15.0f);
            packTess.End();
            ctx.UpdateChunkMesh(0, 0, packTess, 1);
            bool ready = false;
            for (int i = 0; i < 400 && !ready; i++) {
                ready = Frame(ctx, MakeFrame({0, 3, 0}, {0, 0, 0}, {0, 0, -1}, 128, 96), rgba, w, h) &&
                        ctx.JavaPackReady();
                if (!ready)
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            Check(ready, "java pack programs compiled");
            if (ready) {
                bool drawn = false;
                for (int i = 0; i < 3; i++)
                    drawn = Frame(ctx, MakeFrame({0, 3, 0}, {0, 0, 0}, {0, 0, -1}, 128, 96), rgba, w, h);
                Check(drawn, "frame through the java pack");
                if (!rgba.empty()) {
                    const Pixel mid = At(rgba, w, w / 2, h / 2), corner = At(rgba, w, 1, 1);
                    std::printf("     pack pixels: centre %d,%d,%d corner %d,%d,%d\n", mid.r, mid.g, mid.b,
                                corner.r, corner.g, corner.b);
                    Check(mid.g > 150 && mid.r < 90 && mid.b < 90,
                          "composite ran (red tile comes out green)");
                    Check(Background(corner), "clear colour survives the pack");
                    if (const char* dump = std::getenv("PERLIN_TEST_DUMP"))
                        if (FILE* fp = std::fopen((std::string(dump) + "_pack.ppm").c_str(), "wb")) {
                            std::fprintf(fp, "P6\n%u %u\n255\n", w, h);
                            for (size_t i = 0; i + 3 < rgba.size(); i += 4)
                                std::fwrite(&rgba[i], 1, 3, fp);
                            std::fclose(fp);
                        }
                }
            }
            ctx.ClearJavaShaderPack();
            Check(!ctx.JavaShaderPackActive(), "java pack cleared");
        }
    }

    if (const char* packPath = std::getenv("PERLIN_TEST_PACK")) {
        std::string why;
        const auto loadStart = std::chrono::steady_clock::now();
        const bool loadedPack = ctx.SetJavaShaderPack(packPath, {}, why);
        std::printf("     real pack load took %lld ms\n",
                    static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now() - loadStart)
                                               .count()));
        if (!loadedPack) {
            std::printf("FAIL real pack load: %s\n", why.c_str());
            failures++;
        } else {
            Tessellator scene;
            scene.Begin();
            scene.EnterSection(0);
            const float white[4][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
            const float uvs[4][2] = {{0, 0}, {0.01f, 0}, {0.01f, 0.01f}, {0, 0.01f}};
            const float ground[4][3] = {{-6, 0, -6}, {6, 0, -6}, {6, 0, 6}, {-6, 0, 6}};
            const float roof[4][3] = {{-1.5f, 2, -1.5f}, {1.5f, 2, -1.5f}, {1.5f, 2, 1.5f}, {-1.5f, 2, 1.5f}};
            scene.AddQuad(ground, white, uvs, false, 15.0f, 15.0f);
            scene.AddQuad(roof, white, uvs, false, 15.0f, 15.0f);
            scene.End();
            ctx.UpdateChunkMesh(0, 0, scene, 1);
            bool ready = false;
            const auto started = std::chrono::steady_clock::now();
            for (int i = 0; i < 1200 && !ready; i++) {
                FrameParams f = MakeFrame({0, 3, 9}, {0, 1, 0}, {0, 1, 0}, 128, 96);
                f.sky.enabled = true;
                ready = Frame(ctx, f, rgba, w, h) && ctx.JavaPackReady();
                if (!ready)
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            std::printf("     real pack ready after %lld ms\n",
                        static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                   std::chrono::steady_clock::now() - started)
                                                   .count()));
            Check(ready, "real pack compiled");
            if (ready) {
                for (int i = 0; i < 3; i++) {
                    FrameParams f = MakeFrame({0, 3, 9}, {0, 1, 0}, {0, 1, 0}, 128, 96);
                    f.sky.enabled = true;
                    Frame(ctx, f, rgba, w, h);
                }
                if (const char* dump = std::getenv("PERLIN_TEST_DUMP"))
                    if (FILE* fp = std::fopen((std::string(dump) + "_realpack.ppm").c_str(), "wb")) {
                        std::fprintf(fp, "P6\n%u %u\n255\n", w, h);
                        for (size_t i = 0; i + 3 < rgba.size(); i += 4)
                            std::fwrite(&rgba[i], 1, 3, fp);
                        std::fclose(fp);
                    }
            }
            ctx.ClearJavaShaderPack();
        }
    }

    {
        const std::filesystem::path root = WriteShadowPack();
        std::string why;
        if (!ctx.SetJavaShaderPack(root.string(), {}, why)) {
            std::printf("skip java pack shadows: %s\n", why.c_str());
        } else {
            Tessellator scene;
            scene.Begin();
            scene.EnterSection(0);
            const float white[4][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
            const float uvs[4][2] = {{0, 0}, {0.01f, 0}, {0.01f, 0.01f}, {0, 0.01f}};
            const float ground[4][3] = {{-6, 0, -6}, {6, 0, -6}, {6, 0, 6}, {-6, 0, 6}};
            const float roof[4][3] = {{-1.5f, 2, -1.5f}, {1.5f, 2, -1.5f}, {1.5f, 2, 1.5f}, {-1.5f, 2, 1.5f}};
            scene.AddQuad(ground, white, uvs, false, 15.0f, 15.0f);
            scene.AddQuad(roof, white, uvs, false, 15.0f, 15.0f);
            scene.End();
            ctx.UpdateChunkMesh(0, 0, scene, 1);
            bool ready = false;
            for (int i = 0; i < 400 && !ready; i++) {
                ready = Frame(ctx,
                              MakeFrame(
                                  std::getenv("PERLIN_TEST_TOP") ? glm::vec3(0, 6, 7) : glm::vec3(0, 1.2f, 9),
                                  std::getenv("PERLIN_TEST_TOP") ? glm::vec3(0, 0, 0) : glm::vec3(0, 0.4f, 0),
                                  {0, 1, 0}, 128, 96),
                              rgba, w, h) &&
                        ctx.JavaPackReady();
                if (!ready)
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            Check(ready, "java pack with shadows compiled");
            if (ready) {
                bool drawn = false;
                for (int i = 0; i < 3; i++) {
                    FrameParams f =
                        MakeFrame(std::getenv("PERLIN_TEST_TOP") ? glm::vec3(0, 6, 7) : glm::vec3(0, 1.2f, 9),
                                  std::getenv("PERLIN_TEST_TOP") ? glm::vec3(0, 0, 0) : glm::vec3(0, 0.4f, 0),
                                  {0, 1, 0}, 128, 96);
                    f.sky.enabled = true;
                    f.sky.sunAngle = 0.0f;
                    drawn = Frame(ctx, f, rgba, w, h);
                }
                Check(drawn, "frame through the java pack with shadows");
                int lit = 0, dark = 0;
                for (uint32_t y = 0; y < h && !rgba.empty(); y++)
                    for (uint32_t x = 0; x < w; x++) {
                        const Pixel px = At(rgba, w, x, y);
                        if (Background(px))
                            continue;

                        if (px.r > 200)
                            lit++;
                        else if (px.r < 40 && px.g < 40)
                            dark++;
                    }
                std::printf("     pack shadow pixels: %d lit, %d dark\n", lit, dark);
                Check(lit > 300, "pack terrain is lit by the sun");
                Check(dark > 30, "the occluder casts a shadow in the pack");
                if (const char* dump = std::getenv("PERLIN_TEST_DUMP"))
                    if (FILE* fp = std::fopen((std::string(dump) + "_packshadow.ppm").c_str(), "wb")) {
                        std::fprintf(fp, "P6\n%u %u\n255\n", w, h);
                        for (size_t i = 0; i + 3 < rgba.size(); i += 4)
                            std::fwrite(&rgba[i], 1, 3, fp);
                        std::fclose(fp);
                    }
            }
            ctx.ClearJavaShaderPack();
        }
    }

    ctx.Shutdown();
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
