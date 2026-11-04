#pragma once
// shared by the Perlin*.cpp translation units only
#include "client/CameraEffects.hpp"
#include "client/MemoryTrim.hpp"
#include "client/Perlin.hpp"
#include "client/PreviewWorld.hpp"
#include "client/SampleProfiler.hpp"
#include "client/Zoom.hpp"
#include "client/entity/BlockEntityView.hpp"
#include "client/i18n/Lang.hpp"
#include "client/input/Keys.hpp"
#include "client/input/TouchControls.hpp"
#include "client/input/TouchState.hpp"
#include "client/javashaders/PackSource.hpp"
#include "client/multiplayer/SkinLoader.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/platform/web/WebLoop.hpp"
#include "client/player/BlockCollision.hpp"
#include "client/player/BlockDestroy.hpp"
#include "client/player/BlockUse.hpp"
#include "client/player/Bridging.hpp"
#include "client/player/DamageEffect.hpp"
#include "client/renderer/DynamicLights.hpp"
#include "client/renderer/Environment.hpp"
#include "client/renderer/chunk/ChunkMesher.hpp"
#include "client/renderer/chunk/LodStore.hpp"
#include "client/resources/AssetJobs.hpp"
#include "client/resources/BedrockSamples.hpp"
#include "client/resources/BundledShaderPack.hpp"
#include "client/resources/MojangFonts.hpp"
#include "client/resources/ServerBlocks.hpp"
#include "client/resources/ServerPacks.hpp"
#include "client/resources/ShaderPack.hpp"
#include "client/resources/TextureLoader.hpp"
#include "client/resources/TexturePack.hpp"
#include "client/ui/BedScreen.hpp"
#include "client/ui/CreativeScreen.hpp"
#include "client/ui/CreditsScreen.hpp"
#include "client/ui/DeathScreen.hpp"
#include "client/ui/DebugScreen.hpp"
#include "client/ui/DisconnectScreen.hpp"
#include "client/ui/GuiScale.hpp"
#include "client/ui/HudRenderers.hpp"
#include "client/ui/ItemData.hpp"
#include "client/ui/ItemTooltip.hpp"
#include "client/ui/JsonScreen.hpp"
#include "client/ui/LoadingScreen.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/PauseScreen.hpp"
#include "client/ui/ScreenModels.hpp"
#include "client/ui/ServerLayer.hpp"
#include "client/ui/SettingInfo.hpp"
#include "client/ui/SkinPreview.hpp"
#include "client/ui/TextEdit.hpp"
#include "client/ui/TitlePanorama.hpp"
#include "client/ui/UiAssets.hpp"
#include "client/ui/UiLayout.hpp"
#include "network/Featured.hpp"
#include "network/Friends.hpp"
#include "network/Http.hpp"
#include "network/NetherNet.hpp"
#include "network/RakNet.hpp"
#include "network/Realms.hpp"
#include "network/XboxAuth.hpp"
#include <conduit/device.hpp>
#include "util/Env.hpp"
#include "util/FileIo.hpp"
#include "util/Paths.hpp"
#include "util/Relaunch.hpp"
#include "util/Resources.hpp"
#include "util/Strings.hpp"
#include "util/SystemInfo.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "world/level/lighting/ChunkLighting.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// windows.h, when a header above pulled it in, renames these to their A/W variants; mod abi names collide
#ifdef _WIN32
#undef CopyFile
#undef MoveFile
#undef PlaySound
#endif

#ifndef PERLIN_VERSION
#define PERLIN_VERSION "1.0.0"
#endif

namespace perlin::detail {
    using Clock = std::chrono::steady_clock;

    inline uint64_t MsNow() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
    }

    inline double SecondsSince(Clock::time_point since, Clock::time_point now = Clock::now()) {
        return std::chrono::duration<double>(now - since).count();
    }

    // "host", "host:port", "[v6]" or "[v6]:port"; the port defaults to 19132
    inline bool ParseServer(const std::string& raw, std::string& host, uint16_t& port) {
        const std::string_view text = str::TrimView(raw);
        host = text;
        port = 19132;
        std::string_view portText;
        if (text.starts_with('[')) {
            const size_t close = text.find(']');
            if (close == std::string_view::npos)
                return false;
            host = text.substr(1, close - 1);
            if (close + 1 == text.size())
                return !host.empty();
            if (text[close + 1] != ':')
                return false;
            portText = text.substr(close + 2);
        } else {
            const size_t colon = text.rfind(':');
            // more than one colon: a bare ipv6 address
            if (colon == std::string_view::npos || text.find(':') != colon)
                return !host.empty();
            host = text.substr(0, colon);
            portText = text.substr(colon + 1);
        }
        portText = str::TrimView(portText);
        int value = 0;
        const auto [end, ec] = std::from_chars(portText.data(), portText.data() + portText.size(), value);
        if (ec != std::errc() || end == portText.data() || value <= 0 || value > 65535)
            return false;
        port = static_cast<uint16_t>(value);
        return !host.empty();
    }

    inline bool IsAmmoFor(std::string_view weapon, std::string_view item) {
        if (item == "minecraft:arrow" || item == "minecraft:spectral_arrow" ||
            item == "minecraft:tipped_arrow")
            return true;
        return weapon == "minecraft:crossbow" && item == "minecraft:firework_rocket";
    }

    // bows and crossbows only draw with ammo in survival; anything else always counts as armed
    inline bool HasAmmo(const ui::HudState& st, BedrockSession* live, const std::string& weapon) {
        if ((weapon != "minecraft:bow" && weapon != "minecraft:crossbow") || !st.Survival())
            return true;
        const auto ammo = [&](const auto& stack) { return !stack.Empty() && IsAmmoFor(weapon, stack.name); };
        if (ammo(st.offhand))
            return true;
        if (!live)
            return std::ranges::any_of(st.hotbar, ammo);
        const auto inv = live->GetInventory();
        return std::ranges::any_of(inv.hotbar, ammo) || std::ranges::any_of(inv.main, ammo);
    }

    inline ui::HudItem ToHudItem(const BedrockSession::ItemStackInfo& info) {
        ui::HudItem item;
        if (info.Empty())
            return item;
        item.name = info.name;
        item.count = info.count;
        item.blockRuntimeId = info.blockRuntimeId;
        item.enchanted = info.enchanted;
        item.customName = info.customName;
        item.hasColor = info.hasColor;
        item.color = info.color;
        item.nbtTags = info.nbtTags;
        item.enchants = info.enchants;
        item.lore = info.lore;
        item.maxDamage = ui::MaxDurability(info.name);
        item.aux = item.maxDamage > 0 ? std::clamp(info.damage, 0, item.maxDamage) : info.aux;
        return item;
    }

    // the open shader pack's settings and presets, read again when the pack changes
    class ShaderPackCache {
    public:
        const std::vector<ShaderSetting>& Settings(const std::string& key) {
            return Refresh(settings, key, [] { return ShaderPack::Instance().Settings(); });
        }
        const std::vector<ShaderPreset>& Presets(const std::string& key) {
            return Refresh(presets, key, [] { return ShaderPack::Instance().Presets(); });
        }
        void Invalidate() { settings.key = presets.key = kStale; }

    private:
        static constexpr const char* kStale = "\x01";
        template <class T> struct Slot {
            std::string key = kStale;
            std::vector<T> items;
        };
        template <class T, class Load>
        static const std::vector<T>& Refresh(Slot<T>& slot, const std::string& key, Load&& load) {
            if (slot.key != key) {
                slot.key = key;
                slot.items = load();
            }
            return slot.items;
        }
        Slot<ShaderSetting> settings;
        Slot<ShaderPreset> presets;
    };

    inline ShaderPackCache& PackCache() {
        static ShaderPackCache cache;
        return cache;
    }

    // an overlay screen's size in gui pixels: the framebuffer over the overlay's integer scale, rounded up
    struct GuiViewport {
        int scale = 1;
        int w = 0, h = 0;
        float ToGui(float px) const { return px / static_cast<float>(scale); }
    };

    inline GuiViewport OverlayViewport(const ui::Hud& hud, int fbW, int fbH) {
        GuiViewport v;
        v.scale = hud.OverlayScale();
        v.w = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(v.scale)));
        v.h = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(v.scale)));
        return v;
    }

    // the pointer in framebuffer pixels; PERLIN_PREVIEW_MOUSE="x,y" pins it for screenshot tests
    inline glm::vec2 PointerOrPreview(const Input& input) {
        glm::vec2 p(input.MouseX(), input.MouseY());
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &p.x, &p.y);
        return p;
    }

    inline bool IsEscape(const InputManager::Typed& t) {
        return !t.character && t.key == Key::Escape;
    }

    inline bool EscapeTyped(const std::vector<InputManager::Typed>& typed) {
        return std::ranges::any_of(typed, IsEscape);
    }

    // a translation, or the fallback while the key is missing from every loaded language
    inline std::string TrOr(const char* key, const char* fallback) {
        std::string v = i18n::Tr(key);
        return v == key ? std::string(fallback) : v;
    }

    // a debug copy of a generated document under <data>/ui_generated
    inline void DumpGenerated(const std::string& relative, std::string_view text) {
        file::Write(paths::DataDir() + "/ui_generated/" + relative, text);
    }
}
