#pragma once
#include "client/systems/multiplayer/HudItemConvert.hpp"

#include "client/systems/camera/CameraEffects.hpp"
#include "client/systems/diagnostics/MemoryTrim.hpp"
#include "client/features/mods/ModHost.hpp"
#include "client/systems/preview/PreviewWorld.hpp"
#include "client/systems/diagnostics/SampleProfiler.hpp"
#include "client/systems/input/Zoom.hpp"
#include "client/systems/entity/BlockEntityView.hpp"
#include "client/systems/i18n/Lang.hpp"
#include "client/systems/bindings/Keys.hpp"
#include "client/systems/input/TouchControls.hpp"
#include "client/systems/bindings/TouchState.hpp"
#include "client/systems/javashaders/PackSource.hpp"
#include "client/systems/resources/SkinLoader.hpp"
#include "client/systems/player/BlockCollision.hpp"
#include "client/systems/player/BlockDestroy.hpp"
#include "client/systems/blocks/BlockUse.hpp"
#include "client/systems/player/DamageEffect.hpp"
#include "client/systems/renderer/DynamicLights.hpp"
#include "client/systems/renderer/Environment.hpp"
#include "client/systems/renderer/chunk/ChunkMesher.hpp"
#include "client/systems/renderer/chunk/BlockColours.hpp"
#include "client/systems/resources/AssetJobs.hpp"
#include "client/systems/resources/BedrockSamples.hpp"
#include "client/systems/resources/BundledShaderPack.hpp"
#include "client/systems/resources/MojangFonts.hpp"
#include "client/systems/resources/ServerBlocks.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/systems/resources/ShaderPack.hpp"
#include "client/systems/resources/TextureLoader.hpp"
#include "client/systems/resources/TexturePack.hpp"
#include "client/systems/sprites/ItemData.hpp"
#include "client/systems/text/McFont.hpp"
#include "client/systems/sprites/UiAssets.hpp"
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
#include "util/ServerAddress.hpp"
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
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef _WIN32
#undef CopyFile
#undef MoveFile
#undef PlaySound
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

    inline bool IsAmmoFor(std::string_view weapon, std::string_view item) {
        if (item == "minecraft:arrow" || item == "minecraft:spectral_arrow" ||
            item == "minecraft:tipped_arrow")
            return true;
        return weapon == "minecraft:crossbow" && item == "minecraft:firework_rocket";
    }

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

    using i18n::TrOr;

}
