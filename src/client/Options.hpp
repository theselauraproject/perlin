#pragma once
#include "client/input/KeyMapping.hpp"
#include "client/input/TouchState.hpp"

#include <map>
#include <string>
#include <vector>

namespace perlin {
    struct ZoomSettings {
        float inSeconds = 0.2f;
        float outSeconds = 0.2f;
        std::string easing = "smooth";
        float level = 0.25f;
        float minLevel = 0.02f;
        float maxLevel = 1.0f;
        float scrollStep = 0.15f;
        bool resetOnPress = true;
        bool cinematic = true;
        float smoothing = 9.0f;
        bool scaleSensitivity = true;
    };

    struct HandSettings {
        bool show = true;
        bool viewBobbing = true;
        bool bobCamera = true;
        bool bobHand = true;
        float bobStrength = 1.0f;
        bool swing = true;
        float swingSpeed = 1.0f;
        bool equipAnimation = true;
        bool sway = true;
        bool leftHanded = false;
        bool showOffhand = true;
        bool bedrockRig = true;
        bool attachables = true;
        float scale = 1.0f;
        float armX = 0, armY = 0, armZ = 0, armRotX = 0, armRotY = 0, armRotZ = 0, armScale = 1.0f;
        float itemX = 0, itemY = 0, itemZ = 0, itemRotX = 0, itemRotY = 0, itemRotZ = 0, itemScale = 1.0f;
    };

    struct SoundSettings {
        float master = 1.0f, music = 1.0f, record = 1.0f, weather = 1.0f, block = 1.0f, hostile = 1.0f,
              neutral = 1.0f, player = 1.0f, ambient = 1.0f, voice = 1.0f;
        bool subtitles = false;
    };

    struct SkinParts {
        bool cape = true;
        bool jacket = true;
        bool leftSleeve = true;
        bool rightSleeve = true;
        bool leftPants = true;
        bool rightPants = true;
        bool hat = true;
    };

    struct Options {
        bool webTuned = false;
        // draw our own menus (title, pause, settings) the bedrock way instead of java layouts; server ui
        // (sidebars, forms, container screens a server pack restyles) always uses the server's json ui
        int maxFps = 120;
        bool vsync = true;
        bool fullscreen = false;
        // moves the pointer to the middle of the window when an in-game gui (inventory, menus, forms) opens
        bool centerCursor = true;
        // nightly builds fetch the newest nightly in the background and install it when the game closes
        bool autoUpdate = true;
        // join servers that advertise an older version without asking first (the disconnect screen's checkbox)
        bool versionMismatchNoAsk = false;
        bool eulaAccepted = false;
        int renderDistance = 8;
        float fov = 70.0f;
        float gamma = 0.5f;
        std::string shaderPack;
        float mouseSensitivity = 0.15f;
        bool invertMouse = false;
        std::string server = "127.0.0.1:19132";
        std::string playerName = "Perlin";
        std::string skin;
        std::string cape;
        bool slimArms = false;
        std::string transport = "auto";
        bool onlineLogin = false;
        std::string resourcePackDir;
        std::string bedrockSamplesDir;
        bool fancyGraphics = true;
        int cloudRange = 128;
        bool rgss = false;
        float motionBlur = 0.0f;
        bool thirdPersonCrosshair = false;
        std::string glass = "normal";
        bool thirdPersonNametag = false;
        bool fovEffects = true;
        int damageTilt = 100;
        bool damageTint = true;
        bool shadows = true;
        int heldLight = 0;
        int shadowDistance = 0;
        int shadowQuality = 2;
        bool shadowEntities = true;
        float bloom = 0.5f;
        ZoomSettings zoom;
        HandSettings hand;
        int guiScale = 0;
        SoundSettings sound;
        std::string dynamicLights = "off";
        int menuBlur = 5;
        std::string sprintMode = "hold";
        std::string sneakMode = "hold";
        std::string language = "en_US";
        bool showHud = true;
        std::string texturePack;
        std::vector<std::string> texturePacks;
        std::vector<std::string> disabledMods;
        std::vector<std::string> approvedMods;
        bool hotReload = true;
        bool betterGrass = false;
        std::string upscaler = "off";
        std::string upscaleQuality = "quality";
        int upscaleSharpness = 50;
        int upscaleCustom = 70;
        int upscaleTargetFps = 0;
        std::string frameGen = "off";
        bool skins3d = false;
        bool bedrockBridging = true;
        bool enableLods = false;
        std::string renderer = "auto";
        std::string capePhysics = "off";
        SkinParts skinParts;
        std::map<std::string, bool> skinSlim;
        std::map<std::string, double> shaderSettings;
        KeyBindings keys = DefaultBindings();
        KeyBindings padKeys = DefaultPadBindings();
        float padSensitivity = 1.0f;
        float padDeadzone = 0.15f;
        bool padInvertY = false;
        bool padVibration = true;
        float touchSensitivity = 1.0f;
        float touchScale = 1.0f;
        float touchOpacity = 0.85f;
        std::map<std::string, TouchPlace> touchLayout;

        const Binding& GetKey(Action action) const { return keys[static_cast<size_t>(action)]; }
        const Binding& GetPadKey(Action action) const { return padKeys[static_cast<size_t>(action)]; }
        void SetPadKey(Action action, Binding binding) { padKeys[static_cast<size_t>(action)] = binding; }
        void SetKey(Action action, Binding binding) { keys[static_cast<size_t>(action)] = binding; }

        static Options Load();
        void Save() const;
    };

    struct GraphicsSettings {
        bool ambientOcclusion = true;
        int biomeBlendRadius = 2;
        bool clouds = true;
        bool fancyClouds = true;
        int cloudRange = 128;
        int mipmapLevels = 4;
        bool cutoutLeaves = true;
        bool rgss = false;

        static GraphicsSettings From(const Options& options) {
            GraphicsSettings g;
            g.cloudRange = options.cloudRange;
            g.rgss = options.rgss && options.upscaler == "off";
            if (!options.fancyGraphics) {
                g.ambientOcclusion = false;
                g.biomeBlendRadius = 1;
                g.fancyClouds = false;
                g.cloudRange = 32;
                g.mipmapLevels = 2;
                g.cutoutLeaves = false;
                g.rgss = false;
            }
            return g;
        }
    };
}
