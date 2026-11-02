#include "client/Options.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <fstream>
#include "util/Json.hpp"

#include <map>

namespace perlin {
    namespace {
        constexpr int kOptionsVersion = 5;
    }
    namespace stored {
        struct Stored {
            int maxFps = 120;
            bool vsync = true;
            bool eulaAccepted = false;
            int renderDistance = 0;
            float fov = 0;
            float gamma = 0;
            std::string shaderPack;
            float mouseSensitivity = 0;
            bool invertMouse = false;
            std::string server;
            std::string playerName;
            std::string resourcePackDir;
            std::string bedrockSamplesDir;
            std::string graphics = "fancy";
            int cloudRange = 128;
            bool rgss = false;
            bool fovEffects = true;
            float motionBlur = 0.0f;
            bool thirdPersonCrosshair = false;
            std::string glass = "normal";
            bool thirdPersonNametag = false;
            int damageTilt = 100;
            bool damageTint = true;
            std::string texturePack;
            bool shadows = true;
            int heldLight = 0;
            int shadowDistance = 0;
            ZoomSettings zoom;
            int version = 0;
            std::map<std::string, std::string> keys;
            std::string skin;
            std::string cape;
            bool slimArms = false;
            bool onlineLogin = false;
            std::string transport = "auto";
            HandSettings hand;
            int guiScale = 0;
            SoundSettings sound;
            std::string language = "en_US";
            std::string dynamicLights = "off";
            int menuBlur = 5;
            std::string sprintMode = "hold";
            std::string sneakMode = "hold";
            std::vector<std::string> texturePacks;
            std::vector<std::string> disabledMods;
            std::vector<std::string> approvedMods;
            bool hotReload = true;
            bool betterGrass = false;
            bool enableLods = false;
            bool skins3d = false;
            std::string upscaler = "off";
            std::string upscaleQuality = "quality";
            int upscaleSharpness = 50;
            int upscaleCustom = 70;
            int upscaleTargetFps = 0;
            std::string frameGen = "off";
            SkinParts skinParts;
            std::map<std::string, bool> skinSlim;
            std::map<std::string, double> shaderSettings;
            int shadowQuality = 2;
            bool shadowEntities = true;
            float bloom = 0.5f;
            std::string renderer = "auto";
            std::string capePhysics = "off";
            bool bedrockBridging = true;
            bool webTuned = false;
            std::map<std::string, std::string> padKeys;
            float padSensitivity = 1.0f;
            float padDeadzone = 0.15f;
            bool padInvertY = false;
            bool padVibration = true;
            float touchSensitivity = 1.0f;
            float touchScale = 1.0f;
            float touchOpacity = 0.85f;
            std::map<std::string, TouchPlace> touchLayout;
            bool fullscreen = false;
            bool centerCursor = true;
            bool autoUpdate = true;
            bool versionMismatchNoAsk = false;
        };
    }

    using stored::Stored;

    namespace {
        std::string SettingsPath() {
            return paths::DataDir() + "/options.json";
        }
    }

    namespace {
        void TuneForWeb(Options& o) {
            o.renderDistance = 6;
            o.shadows = false;
            o.shadowQuality = 0;
            o.shadowEntities = false;
            o.bloom = 0.0f;
            o.motionBlur = 0.0f;
            o.rgss = false;
            o.fancyGraphics = false;
            o.cloudRange = 32;
            o.menuBlur = 0;
            o.dynamicLights = "off";
            o.webTuned = true;
        }
    }

    Options Options::Load() {
        Options o;
#ifdef __EMSCRIPTEN__
        TuneForWeb(o);
#endif
        const std::string dir = paths::DataDir();
        paths::EnsureDir(dir);
        o.resourcePackDir = dir + "/resourcepacks";
        o.bedrockSamplesDir = dir + "/bedrock-samples";

        std::ifstream file(SettingsPath());
        if (!file.good())
            return o;
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        Stored stored{o.maxFps,
                      o.vsync,
                      o.eulaAccepted,
                      o.renderDistance,
                      o.fov,
                      o.gamma,
                      o.shaderPack,
                      o.mouseSensitivity,
                      o.invertMouse,
                      o.server,
                      o.playerName,
                      o.resourcePackDir,
                      o.bedrockSamplesDir,
                      "fancy",
                      o.cloudRange,
                      o.rgss,
                      o.fovEffects,
                      o.motionBlur,
                      o.thirdPersonCrosshair,
                      o.glass,
                      o.thirdPersonNametag,
                      o.damageTilt,
                      o.damageTint,
                      o.texturePack,
                      o.shadows,
                      o.heldLight,
                      o.shadowDistance,
                      o.zoom,
                      0,
                      {},
                      o.skin,
                      o.cape,
                      o.slimArms,
                      o.onlineLogin,
                      o.transport,
                      o.hand,
                      o.guiScale,
                      o.sound,
                      o.language,
                      o.dynamicLights,
                      o.menuBlur,
                      o.sprintMode,
                      o.sneakMode,
                      o.texturePacks,
                      o.disabledMods,
                      o.approvedMods,
                      o.hotReload,
                      o.betterGrass,
                      o.enableLods,
                      o.skins3d,
                      o.upscaler,
                      o.upscaleQuality,
                      o.upscaleSharpness,
                      o.upscaleCustom,
                      o.upscaleTargetFps,
                      o.frameGen,
                      o.skinParts,
                      o.skinSlim,
                      o.shaderSettings,
                      o.shadowQuality,
                      o.shadowEntities,
                      o.bloom,
                      o.renderer,
                      o.capePhysics,
                      o.bedrockBridging};
        auto parsed = json::Read<Stored>(text);
        if (!parsed)
            return o;
        stored = std::move(*parsed);

        o.maxFps = stored.maxFps;
        o.vsync = stored.vsync;
        o.eulaAccepted = stored.eulaAccepted;
        o.renderDistance = std::clamp(stored.renderDistance, 2, 256);
        o.fov = std::clamp(stored.fov, 30.0f, 110.0f);
        o.gamma = std::clamp(stored.gamma, 0.0f, 1.0f);
        o.shaderPack = stored.shaderPack;
        o.mouseSensitivity = std::clamp(stored.mouseSensitivity, 0.01f, 2.0f);
        o.invertMouse = stored.invertMouse;
        o.server = stored.server;
        o.playerName = stored.playerName;
        o.skin = stored.skin;
        o.cape = stored.cape;
        o.slimArms = stored.slimArms;
        o.onlineLogin = stored.onlineLogin;
        o.transport =
            stored.transport == "raknet" || stored.transport == "nethernet" ? stored.transport : "auto";
        o.resourcePackDir = stored.resourcePackDir;
        o.bedrockSamplesDir = stored.bedrockSamplesDir;
        o.fancyGraphics = stored.graphics != "fast";
        o.cloudRange = std::clamp(stored.cloudRange, 2, 128);
        o.rgss = stored.rgss;
        o.fovEffects = stored.fovEffects;
        o.motionBlur = std::clamp(stored.motionBlur, 0.0f, 1.0f);
        o.thirdPersonCrosshair = stored.thirdPersonCrosshair;
        o.glass = (stored.glass == "connected" || stored.glass == "connectedClear" || stored.glass == "clear")
                      ? stored.glass
                      : std::string("normal");
        o.thirdPersonNametag = stored.thirdPersonNametag;
        o.damageTilt = std::clamp(stored.damageTilt, 0, 100);
        o.damageTint = stored.damageTint;
        o.texturePack = stored.texturePack;
        o.texturePacks = stored.texturePacks;
        if (o.texturePacks.empty() && !stored.texturePack.empty())
            o.texturePacks.push_back(stored.texturePack);
        o.disabledMods = stored.disabledMods;
        o.approvedMods = stored.approvedMods;
        o.hotReload = stored.hotReload;
        o.betterGrass = stored.betterGrass;
        o.enableLods = stored.enableLods;
        o.skins3d = stored.skins3d;
        o.bedrockBridging = stored.bedrockBridging;
        o.upscaler = stored.upscaler;
        o.upscaleQuality = stored.upscaleQuality;
        o.upscaleSharpness = std::clamp(stored.upscaleSharpness, 0, 100);
        o.upscaleCustom = std::clamp(stored.upscaleCustom, 25, 100);
        o.upscaleTargetFps = std::clamp(stored.upscaleTargetFps, 0, 360);
        o.frameGen = stored.frameGen;
        if (!o.enableLods)
            o.renderDistance = std::min(o.renderDistance, 32);
        o.skinParts = stored.skinParts;
        o.skinSlim = stored.skinSlim;
        o.shaderSettings = stored.shaderSettings;
        o.shadowQuality = std::clamp(stored.shadowQuality, 0, 4);
        o.shadowEntities = stored.shadowEntities;
        o.bloom = std::clamp(stored.bloom, 0.0f, 1.0f);
        o.shadows = stored.shadows;
        o.heldLight = std::clamp(stored.heldLight, 0, 15);
        o.shadowDistance = std::clamp(stored.shadowDistance, 0, 256);
        o.hand = stored.hand;
        o.hand.bobStrength = std::clamp(o.hand.bobStrength, 0.0f, 3.0f);
        o.hand.swingSpeed = std::clamp(o.hand.swingSpeed, 0.1f, 5.0f);
        o.hand.scale = std::clamp(o.hand.scale, 0.25f, 2.0f);
        o.hand.armScale = std::clamp(o.hand.armScale, 0.1f, 4.0f);
        o.hand.itemScale = std::clamp(o.hand.itemScale, 0.1f, 4.0f);
        o.guiScale = std::clamp(stored.guiScale, 0, 16);
        o.sound = stored.sound;
        o.menuBlur = std::clamp(stored.menuBlur, 0, 10);
        o.renderer = stored.renderer == "d3d12" || stored.renderer == "d3d11" ||
                             stored.renderer == "vulkan" || stored.renderer == "metal" ||
                             stored.renderer == "webgpu"
                         ? stored.renderer
                         : std::string("auto");
        o.capePhysics = stored.capePhysics == "off" || stored.capePhysics == "shoulder" ? stored.capePhysics
                                                                                        : std::string("off");
        o.sprintMode = stored.sprintMode == "toggle" ? "toggle" : "hold";
        o.sneakMode = stored.sneakMode == "toggle" ? "toggle" : "hold";
        o.dynamicLights =
            stored.dynamicLights == "fast" || stored.dynamicLights == "fancy" ? stored.dynamicLights : "off";
        o.language = stored.language.empty() ? std::string("en_US") : stored.language;
        for (float* v :
             {&o.sound.master, &o.sound.music, &o.sound.record, &o.sound.weather, &o.sound.block,
              &o.sound.hostile, &o.sound.neutral, &o.sound.player, &o.sound.ambient, &o.sound.voice})
            *v = std::clamp(*v, 0.0f, 1.0f);
        o.zoom = stored.zoom;
        o.zoom.inSeconds = std::clamp(o.zoom.inSeconds, 0.0f, 5.0f);
        o.zoom.outSeconds = std::clamp(o.zoom.outSeconds, 0.0f, 5.0f);
        o.zoom.minLevel = std::clamp(o.zoom.minLevel, 0.005f, 1.0f);
        o.zoom.maxLevel = std::clamp(o.zoom.maxLevel, o.zoom.minLevel, 1.0f);
        o.zoom.level = std::clamp(o.zoom.level, o.zoom.minLevel, o.zoom.maxLevel);
        o.zoom.scrollStep = std::clamp(o.zoom.scrollStep, 0.0f, 0.9f);
        o.zoom.smoothing = std::clamp(o.zoom.smoothing, 0.0f, 100.0f);
        if (stored.version < kOptionsVersion) {
            o.vsync = Options().vsync;
            o.maxFps = Options().maxFps;
            o.damageTint = Options().damageTint;
            o.betterGrass = Options().betterGrass;
            o.skins3d = Options().skins3d;
            o.capePhysics = Options().capePhysics;
        }

        for (size_t i = 0; i < kActionCount; i++) {
            const auto it = stored.keys.find(ActionId(static_cast<Action>(i)));
            if (it == stored.keys.end())
                continue;
            const Binding binding = Binding::Parse(it->second);
            if (binding.device != Binding::Device::None)
                o.keys[i] = binding;
        }
        for (size_t i = 0; i < kActionCount; i++) {
            const auto it = stored.padKeys.find(ActionId(static_cast<Action>(i)));
            if (it == stored.padKeys.end())
                continue;
            const Binding binding = Binding::Parse(it->second);
            if (binding.device == Binding::Device::Gamepad || it->second.empty())
                o.padKeys[i] = binding;
        }
#ifdef __EMSCRIPTEN__
        if (!stored.webTuned)
            TuneForWeb(o);
        else
            o.webTuned = true;
#endif
        o.padSensitivity = std::clamp(stored.padSensitivity, 0.1f, 3.0f);
        o.padDeadzone = std::clamp(stored.padDeadzone, 0.0f, 0.6f);
        o.padInvertY = stored.padInvertY;
        o.padVibration = stored.padVibration;
        o.touchSensitivity = std::clamp(stored.touchSensitivity, 0.1f, 3.0f);
        o.touchScale = std::clamp(stored.touchScale, 0.5f, 1.8f);
        o.touchOpacity = std::clamp(stored.touchOpacity, 0.2f, 1.0f);
        o.touchLayout = stored.touchLayout;
        o.fullscreen = stored.fullscreen;
        o.centerCursor = stored.centerCursor;
        o.autoUpdate = stored.autoUpdate;
        o.versionMismatchNoAsk = stored.versionMismatchNoAsk;
        return o;
    }

    void Options::Save() const {
        Stored stored{maxFps,
                      vsync,
                      eulaAccepted,
                      renderDistance,
                      fov,
                      gamma,
                      shaderPack,
                      mouseSensitivity,
                      invertMouse,
                      server,
                      playerName,
                      resourcePackDir,
                      bedrockSamplesDir,
                      fancyGraphics ? "fancy" : "fast",
                      cloudRange,
                      rgss,
                      fovEffects,
                      motionBlur,
                      thirdPersonCrosshair,
                      glass,
                      thirdPersonNametag,
                      damageTilt,
                      damageTint,
                      texturePack,
                      shadows,
                      heldLight,
                      shadowDistance,
                      zoom,
                      kOptionsVersion,
                      {},
                      skin,
                      cape,
                      slimArms,
                      onlineLogin,
                      transport,
                      hand,
                      guiScale,
                      sound,
                      language,
                      dynamicLights,
                      menuBlur,
                      sprintMode,
                      sneakMode,
                      texturePacks,
                      disabledMods,
                      approvedMods,
                      hotReload,
                      betterGrass,
                      enableLods,
                      skins3d,
                      upscaler,
                      upscaleQuality,
                      upscaleSharpness,
                      upscaleCustom,
                      upscaleTargetFps,
                      frameGen,
                      skinParts,
                      skinSlim,
                      shaderSettings,
                      shadowQuality,
                      shadowEntities,
                      bloom,
                      renderer,
                      capePhysics,
                      bedrockBridging};
        for (size_t i = 0; i < kActionCount; i++)
            stored.keys[ActionId(static_cast<Action>(i))] = keys[i].Serialize();
        for (size_t i = 0; i < kActionCount; i++)
            stored.padKeys[ActionId(static_cast<Action>(i))] = padKeys[i].Serialize();
        stored.webTuned = webTuned;
        stored.padSensitivity = padSensitivity;
        stored.padDeadzone = padDeadzone;
        stored.padInvertY = padInvertY;
        stored.padVibration = padVibration;
        stored.touchSensitivity = touchSensitivity;
        stored.touchScale = touchScale;
        stored.touchOpacity = touchOpacity;
        stored.touchLayout = touchLayout;
        stored.fullscreen = fullscreen;
        stored.centerCursor = centerCursor;
        stored.autoUpdate = autoUpdate;
        stored.versionMismatchNoAsk = versionMismatchNoAsk;

        std::string out;
        if (glz::write<glz::opts{.prettify = true}>(stored, out))
            return;
        paths::EnsureDir(paths::DataDir());
        std::ofstream(SettingsPath()) << out;
    }
}
