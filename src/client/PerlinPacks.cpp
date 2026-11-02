#include "client/PerlinInternal.hpp"
#include "util/Log.hpp"
#include "world/level/lighting/LightRegistry.hpp"

namespace perlin {
    namespace {
        std::string TreeStamp(const std::string& path) {
            std::error_code ec;
            uint64_t h = 1469598103934665603ull;
            const auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
            if (!std::filesystem::exists(path, ec))
                return "none";
            if (!std::filesystem::is_directory(path, ec)) {
                mix(static_cast<uint64_t>(
                    std::filesystem::last_write_time(path, ec).time_since_epoch().count()));
                mix(std::filesystem::file_size(path, ec));
                return std::to_string(h);
            }
            for (std::filesystem::recursive_directory_iterator
                     it(path, std::filesystem::directory_options::skip_permission_denied, ec),
                 end;
                 !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec))
                    continue;
                mix(static_cast<uint64_t>(it->last_write_time(ec).time_since_epoch().count()));
                mix(it->file_size(ec));
            }
            return std::to_string(h);
        }
    }

    void Perlin::ApplyTexturePacks() {
        {
            std::string error;
            // mod-shipped packs sit under the player's own, by absolute path
            std::vector<std::string> stack = options.texturePacks;
            {
                std::lock_guard lock(modHooks.mutex);
                for (const auto& pack : modHooks.packs)
                    stack.push_back(pack.second);
            }
            TexturePack::Instance().OpenStack(paths::DataDir() + "/texturepacks", stack, error);
            if (!error.empty())
                Log("textures", "{} (using default textures)", error);
        }
        if (levelRenderer)
            levelRenderer->ReloadResources();
        entityRenderer.ClearCaches();
        ui::UiAssets::Instance().ClearItemCache();
        if (hud)
            hud->Reload();
        hotTexStamp.clear();
        mods::ModManager::Instance().Queue(abi::Event::ResourcesReloaded, {});
    }

    void Perlin::UnloadServerPacks(bool leavingLevel) {
        if (!leavingLevel) {
            if (ServerPacks::Instance().HasServerPacks())
                levelUnloadPending = true;
            return;
        }
        Log("packs", "unloading the server's resource packs");
        // finish readers before freeing the definitions and textures they reference
        form.ui.Reset();
        inventory.chestUi.Reset();
        ui::JsonScreens::Instance().Reset();
        if (hud)
            hud->UnloadLevelUi();
        if (levelRenderer) {
            levelRenderer->Stop();
            levelRenderer.reset();
        }
        entityRenderer.ClearCaches();
        handRenderer.UnloadLevelResources();
        ServerPacks::Instance().Clear();
        ServerBlocks::Instance().Reset();
        ServerBlocks::Instance().ClearItems();
        if (renderer) {
            renderer->UnloadLevelResources();
            levelRenderer = std::make_unique<LevelRenderer>(*renderer, options);
        }
        auto& assets = ui::UiAssets::Instance();
        const std::string vanilla = assets.VanillaRoot();
        assets.ClearLevelCache();
        assets.Configure(vanilla, levelRenderer ? &levelRenderer->Blocks() : nullptr);
        ui::McFont::Instance().UnloadLevelResources();
        i18n::Lang::Get().Invalidate();
        audioEngine.StopAll();
        sounds.Configure({});
        form.model = {};
        form.engine = form.engineFailed = false;
        form.signature.clear();
        hudState = {};
        heldBuild.Stop();
    }

    void Perlin::CommitTexturePacks() {
        if (!packUi.pendingInit)
            return;
        packUi.pendingInit = false;
        if (packUi.pending == options.texturePacks)
            return;
        options.texturePacks = packUi.pending;
        options.Save();
        ApplyTexturePacks();
    }

    void Perlin::ApplyShaderPack() {
        ShaderPack& pack = ShaderPack::Instance();
        pack.Close();
        detail::PackCache().Invalidate();
        javaPackPending.clear();
        if (!options.shaderPack.empty()) {
            const std::string full = paths::DataDir() + "/shaderpacks/" + options.shaderPack;
            if (jshader::LooksLikeJavaPack(full)) {
                javaPackPending = full;
            } else {
                std::string error;
                if (!pack.Open(paths::DataDir() + "/shaderpacks", options.shaderPack, error))
                    Log("shaders", "{} - using the built-in shaders", error);
            }
        }
        if (javaPackPending.empty() && renderer && renderer->JavaShaderPackActive()) {
            renderer->ClearJavaShaderPack();
            if (levelRenderer)
                levelRenderer->RemeshAll();
        }
        if (renderer)
            renderer->ReloadShaders();
        hotShaderStamp.clear();
    }

    void Perlin::PollJavaPack(BedrockSession* live) {
        if (!renderer)
            return;
        static bool startupChecked = false;
        if (!startupChecked) {
            startupChecked = true;
            if (!options.shaderPack.empty()) {
                const std::string full = paths::DataDir() + "/shaderpacks/" + options.shaderPack;
                if (jshader::LooksLikeJavaPack(full))
                    javaPackPending = full;
            }
        }
        if (!javaPackPending.empty() && levelRenderer && levelRenderer->Blocks().Complete()) {
            std::string error;
            const std::string path = javaPackPending;
            renderer->SetBlockNameLookup([](uint16_t id) { return BlockPalette::Instance().Name(id); });
            renderer->SetBlockEmissionLookup(
                [](uint16_t id) { return static_cast<int>(LightRegistry::Instance().Resolve(id).emission); });
            if (renderer->SetJavaShaderPack(path, jshader::ReadJavaPackOptions(path), error)) {
                javaPackPending.clear();
                levelRenderer->RemeshAll();
                Log("javapack", "loaded {}", path);
            } else if (!error.contains("atlas is not loaded")) {
                javaPackPending.clear();
                Log("javapack", "{}", error);
                chat.AddMessage("Shader pack failed: " + error, hudAnim.ticks);
            }
        }
        if (renderer->JavaShaderPackActive() && live) {
            const int dim = std::clamp(live->DimensionId(), 0, 2);
            if (dim != javaPackDimension) {
                javaPackDimension = dim;
                renderer->SetJavaShaderPackDimension(dim == 1 ? "world-1" : dim == 2 ? "world1" : "world0");
                if (levelRenderer)
                    levelRenderer->RemeshAll();
            }
        }
    }

    void Perlin::PollHotReload() {
        if (!options.hotReload)
            return;
        const auto now = Clock::now();
        if (now - hotPollAt < std::chrono::milliseconds(500))
            return;
        hotPollAt = now;
        const auto scan = [](std::vector<std::string> texturePacks, std::string shaderPack) {
            HotStamps out;
            for (const std::string& name : texturePacks)
                out.tex += TreeStamp(paths::DataDir() + "/texturepacks/" + name) + ";";
            out.shader = "none";
            if (!shaderPack.empty()) {
                const std::string root = paths::DataDir() + "/shaderpacks/" + shaderPack;
                std::error_code isDir;
                if (std::filesystem::is_directory(root, isDir)) {
                    out.shader.clear();
                    for (const char* sub : {"dxbc", "glsl", "spirv", "essl", "metal", "pack.json"})
                        out.shader += TreeStamp(root + "/" + sub) + ";";
                } else {
                    out.shader = TreeStamp(root);
                }
            }
            return out;
        };
        HotStamps stamps;
#if defined(__EMSCRIPTEN__)
        stamps = scan(options.texturePacks, options.shaderPack);
#else
        if (!hotScan.valid()) {
            hotScan = std::async(std::launch::async, scan, options.texturePacks, options.shaderPack);
            return;
        }
        if (hotScan.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        stamps = hotScan.get();
#endif
        const std::string& tex = stamps.tex;
        if (hotTexStamp.empty())
            hotTexStamp = tex;
        else if (tex != hotTexStamp) {
            hotTexStamp = tex;
            Log("hot-reload", "texture packs changed");
            ApplyTexturePacks();
        }
        const std::string& shader = stamps.shader;
        if (hotShaderStamp.empty())
            hotShaderStamp = shader;
        else if (shader != hotShaderStamp) {
            hotShaderStamp = shader;
            Log("hot-reload", "shader pack changed");
            ApplyShaderPack();
        }
        std::string report;
        {
            std::lock_guard lock(statusMutex);
            report.swap(shaderBuildReport);
        }
        if (!report.empty())
            chat.AddMessage(report, hudAnim.ticks);
    }
}
