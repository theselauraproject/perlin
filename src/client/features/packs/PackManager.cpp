#include "client/features/packs/PackManager.hpp"
#include "client/systems/audio/AudioEngine.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "client/systems/i18n/Lang.hpp"
#include "client/systems/javashaders/PackSource.hpp"
#include "client/systems/resources/ServerBlocks.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/systems/resources/ShaderPack.hpp"
#include "client/systems/resources/TexturePack.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/systems/text/McFont.hpp"
#include "client/ui/JsonScreen.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include "util/Log.hpp"
#include "world/level/lighting/LightRegistry.hpp"

namespace perlin::packs {
    PackManager::PackManager(Deps d)
        : options(d.options), uiAssets(d.uiAssets), texturePack(d.texturePack), serverPacks(d.serverPacks), serverBlocks(d.serverBlocks), jsonScreens(d.jsonScreens), mcFont(d.mcFont), shaderPack(d.shaderPack), blockPalette(d.blockPalette), lightRegistry(d.lightRegistry), renderer(d.renderer), levelRenderer(d.levelRenderer), hud(d.hud),
          entityRenderer(d.entityRenderer), handRenderer(d.handRenderer), hudState(d.hudState),
          audioEngine(d.audioEngine), sounds(d.sounds), stopHeldBuild(std::move(d.stopHeldBuild)), chat(d.chat), hudTicks(d.hudTicks),
          shaderPackCache(d.shaderPackCache), modTexturePacks(std::move(d.modTexturePacks)),
          resourcesReloaded(std::move(d.resourcesReloaded)), resetLevelScreens(std::move(d.resetLevelScreens)) {}

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

    void PackManager::ApplyTexturePacks() {
        {
            std::string error;

            std::vector<std::string> stack = options.texturePacks;
            for (const std::string& pack : modTexturePacks())
                stack.push_back(pack);

            if (levelRenderer)
                levelRenderer->WaitAssetJob();
            texturePack.OpenStack(paths::DataDir() + "/texturepacks", stack, error);
            if (!error.empty())
                Log("textures", "{} (using default textures)", error);
        }
        if (levelRenderer)
            levelRenderer->ReloadResources(true);
        entityRenderer.ClearCaches();
        uiAssets.ClearItemCache();
        if (hud)
            hud->Reload();
        hotTexStamp.clear();
        resourcesReloaded();
    }

    void PackManager::UnloadServerPacks(bool leavingLevel) {
        if (!leavingLevel) {
            if (serverPacks.HasServerPacks())
                levelUnloadPending = true;
            return;
        }
        Log("packs", "unloading the server's resource packs");

        resetLevelScreens();
        jsonScreens.Reset();
        if (hud)
            hud->UnloadLevelUi();
        if (levelRenderer) {
            levelRenderer->Stop();
            levelRenderer.reset();
        }
        entityRenderer.ClearCaches();
        handRenderer.UnloadLevelResources();
        serverPacks.Clear();
        serverBlocks.Reset();
        serverBlocks.ClearItems();
        if (renderer) {
            renderer->UnloadLevelResources();
            levelRenderer = std::make_unique<LevelRenderer>(*renderer, options);
            levelRenderer->OnLibrarySwap([&assets = uiAssets](const BlockLibrary* library) {
                if (!assets.VanillaRoot().empty())
                    assets.Configure(assets.VanillaRoot(), library);
            });
        }
        auto& assets = uiAssets;
        const std::string vanilla = assets.VanillaRoot();
        assets.ClearLevelCache();
        assets.Configure(vanilla, levelRenderer ? &levelRenderer->Blocks() : nullptr);
        mcFont.UnloadLevelResources();
        i18n::Lang::Get().Invalidate();
        audioEngine.StopAll();
        sounds.Configure({});
        hudState = {};
        stopHeldBuild();
    }

    void PackManager::CommitTexturePacks(const std::vector<std::string>& pending) {
        if (pending == options.texturePacks)
            return;
        options.texturePacks = pending;
        options.Save();
        ApplyTexturePacks();
    }

    void PackManager::ApplyShaderPack() {
        ShaderPack& pack = shaderPack;
        pack.Close();
        shaderPackCache.Invalidate();
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

    void PackManager::PollJavaPack(BedrockSession* live) {
        if (!renderer)
            return;
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

            renderer->SetBlockNameLookup([&palette = blockPalette](uint16_t id) {
                std::string out = palette.Name(id);
                for (const auto& [key, value] : palette.States(id)) {
                    out += '|';
                    out += key;
                    out += '=';
                    out += value.kind == BlockPalette::StateValue::String ? value.s : std::to_string(value.i);
                }
                return out;
            });
            renderer->SetBlockEmissionLookup(
                [&registry = lightRegistry](uint16_t id) { return static_cast<int>(registry.Resolve(id).emission); });
            if (renderer->SetJavaShaderPack(path, jshader::ReadJavaPackOptions(path), error)) {
                javaPackPending.clear();
                levelRenderer->RemeshAll();
                Log("javapack", "loaded {}", path);
            } else if (!error.contains("atlas is not loaded")) {
                javaPackPending.clear();
                Log("javapack", "{}", error);
                chat.AddMessage("Shader pack failed: " + error, hudTicks);
            }
        }
        if (renderer->JavaShaderPackActive() && live) {
            int dim = std::clamp(live->DimensionId(), 0, 2);

            if (const char* forced = std::getenv("PERLIN_DEV_PACK_DIMENSION"))
                dim = std::clamp(std::atoi(forced), 0, 2);
            if (dim != javaPackDimension) {
                javaPackDimension = dim;
                renderer->SetJavaShaderPackDimension(dim == 1 ? "world-1" : dim == 2 ? "world1" : "world0");
                if (levelRenderer)
                    levelRenderer->RemeshAll();
            }
        }
    }

    void PackManager::PollHotReload() {
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
    }
}
