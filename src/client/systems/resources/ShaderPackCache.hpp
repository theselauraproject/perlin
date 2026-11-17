#pragma once
#include "client/systems/resources/ShaderPack.hpp"

#include <string>
#include <vector>

namespace perlin {
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
}
