#include "client/features/packs/PackManager.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/selftest/TestEnvironment.hpp"
#include "util/Paths.hpp"

#include <miniz.h>

#include <chrono>
#include <fstream>
#include <thread>

namespace {
    using namespace perlin;

    struct Rig {
        Options options;
        std::unique_ptr<RenderContext> renderer;
        std::unique_ptr<LevelRenderer> levelRenderer;
        std::unique_ptr<ui::Hud> hud;
        ui::HudState hudState;
        HandRenderer handRenderer;
        entity::EntityRenderer entityRenderer;
        audio::AudioEngine audioEngine;
        audio::SoundSystem sounds{audioEngine};
        ui::ChatLog chat;
        uint64_t ticks = 0;
        ShaderPackCache shaderPackCache;
        int stopHeld = 0, reloaded = 0, levelScreensReset = 0;
        std::vector<std::string> modPacks;
        packs::PackManager manager{packs::PackManager::Deps{
            options, selftest::G().uiAssets, selftest::G().texturePack, selftest::G().serverPacks,
            selftest::G().serverBlocks, selftest::G().jsonScreens, selftest::G().mcFont, selftest::G().shaderPack,
            selftest::G().blockPalette, selftest::G().lightRegistry, renderer, levelRenderer, hud, entityRenderer,
            handRenderer, hudState, audioEngine, sounds, chat, ticks, shaderPackCache, [this] { stopHeld++; },
            [this] { return modPacks; }, [this] { reloaded++; }, [this] { levelScreensReset++; }}};
    };

    void Pause(int ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    std::string MakeZip(const std::string& path) {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        mz_zip_archive zip{};
        mz_zip_writer_init_file(&zip, path.c_str(), 0);
        const std::string manifest = "{}";
        mz_zip_writer_add_mem(&zip, "manifest.json", manifest.data(), manifest.size(), MZ_DEFAULT_LEVEL);
        mz_zip_writer_add_mem(&zip, "textures/probe.txt", "x", 1, MZ_DEFAULT_LEVEL);
        mz_zip_writer_finalize_archive(&zip);
        mz_zip_writer_end(&zip);
        return path;
    }

    void Write(const std::string& path, const std::string& body) {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream(path, std::ios::binary) << body;
    }
}

namespace perlin::selftest {
    int PackManagerTest() {
        Checker t("pack_manager_test");
        TempDataDir data;
        const std::string packs = paths::DataDir() + "/texturepacks";
        const std::string shaders = paths::DataDir() + "/shaderpacks";

        {
            Rig r;
            r.options.texturePacks = {"a"};
            r.manager.CommitTexturePacks({"a"});
            t.Equal(r.reloaded, 0, "committing an unchanged stack reloads nothing");

            r.manager.CommitTexturePacks({"a", "b"});
            t.Equal(r.reloaded, 1, "committing a changed stack reloads resources once");
            t.Check(r.options.texturePacks == std::vector<std::string>({"a", "b"}), "options record the new stack");

            r.manager.CommitTexturePacks({});
            t.Equal(r.reloaded, 2, "clearing the stack reloads again");
            t.Check(r.options.texturePacks.empty(), "options record the empty stack");
        }
        {
            Rig r;
            r.options.texturePacks = {"missing_pack_that_does_not_exist"};
            r.manager.ApplyTexturePacks();
            t.Equal(r.reloaded, 1, "a missing pack still completes the reload with default textures");
            t.Check(!selftest::G().texturePack.Active(), "missing pack leaves no active layer");

            Write(packs + "/real/pack.mcmeta", "{}");
            Write(packs + "/real/assets/minecraft/textures/block/probe.png", "x");
            r.options.texturePacks = {"real"};
            r.modPacks = {"also_missing"};
            r.manager.ApplyTexturePacks();
            t.Equal(r.reloaded, 2, "mod packs are appended to the stack without breaking the reload");
        }
        {
            Rig r;
            r.options.shaderPack = "does_not_exist";
            r.manager.ApplyShaderPack();
            t.Check(r.manager.JavaPackPending().empty(), "a missing shader pack leaves nothing pending");

            Write(shaders + "/javapack/shaders/final.fsh", "void main(){}");
            r.options.shaderPack = "javapack";
            r.manager.ApplyShaderPack();
            t.Check(r.manager.JavaPackPending() == shaders + "/javapack", "a java shader pack is deferred until blocks load");

            r.options.shaderPack.clear();
            r.manager.ApplyShaderPack();
            t.Check(r.manager.JavaPackPending().empty(), "clearing the shader pack clears the pending pack");
        }
        {
            Rig r;
            Write(shaders + "/startuppack/shaders/final.fsh", "void main(){}");
            r.options.shaderPack = "startuppack";
            r.manager.PollJavaPack(nullptr);
            t.Check(r.manager.JavaPackPending().empty(), "polling without a renderer does nothing");
        }
        {
            Rig r;
            t.Check(!r.manager.TakeLevelUnloadPending(), "no unload pending initially");
            r.manager.RequestLevelUnload();
            t.Check(r.manager.TakeLevelUnloadPending(), "requested unload is reported");
            t.Check(!r.manager.TakeLevelUnloadPending(), "unload request is consumed once");

            selftest::G().serverPacks.Clear();
            r.manager.UnloadServerPacks(false);
            t.Check(!r.manager.TakeLevelUnloadPending(), "no server packs means no deferred unload");
            selftest::G().serverPacks.SetRoot(paths::DataDir() + "/serverpacks");
            selftest::G().serverPacks.Replace({{MakeZip(paths::DataDir() + "/zips/server.zip"), "serverpack", "key"}});
            t.Check(selftest::G().serverPacks.HasServerPacks(), "a server pack is mounted");
            r.manager.UnloadServerPacks(false);
            t.Check(r.manager.TakeLevelUnloadPending(), "server packs defer the unload to the level boundary");

            r.hudState.health = 5;
            r.manager.UnloadServerPacks(true);
            t.Equal(r.levelScreensReset, 1, "leaving resets level screens");
            t.Equal(r.stopHeld, 1, "leaving stops held-build");
            t.Check(!selftest::G().serverPacks.HasServerPacks(), "leaving clears mounted server packs");
            t.Check(r.hudState.health != 5, "leaving resets the hud state");
            t.Check(!r.levelRenderer, "leaving without a renderer does not recreate a level renderer");
        }
        {
            Rig r;
            r.options.hotReload = false;
            r.options.texturePacks = {"hot"};
            Write(packs + "/hot/a.txt", "one");
            for (int i = 0; i < 3; i++) {
                r.manager.PollHotReload();
                Pause(600);
            }
            t.Equal(r.reloaded, 0, "hot reload off never reloads");

            r.options.hotReload = true;
            r.manager.PollHotReload();
            Pause(600);
            r.manager.PollHotReload();
            Pause(600);
            r.manager.PollHotReload();
            Pause(600);
            r.manager.PollHotReload();
            Pause(600);
            t.Equal(r.reloaded, 0, "first stamp and an unchanged tree do not reload");

            Write(packs + "/hot/a.txt", "two, now longer");
            for (int i = 0; i < 4; i++) {
                r.manager.PollHotReload();
                Pause(600);
            }
            t.Check(r.reloaded >= 1, "a changed pack tree triggers a reload");
            const int after = r.reloaded;
            for (int i = 0; i < 4; i++) {
                r.manager.PollHotReload();
                Pause(600);
            }
            t.Equal(r.reloaded, after, "no further reloads after the change settles");
        }
        return t.Finish();
    }
}
