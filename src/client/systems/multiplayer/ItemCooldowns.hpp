#pragma once

#include "util/JsonValue.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace perlin::itemcooldown {
    enum class Type : uint8_t { Use, Attack };

    struct Definition {
        std::string category;
        Type type = Type::Use;
    };

    inline std::string CategoryKey(std::string_view category) {
        if (category.contains(':'))
            return std::string(category);
        return "minecraft:" + std::string(category);
    }

    // These four items override Item::getCooldownCategory in the matched client.
    // Definition-shaped registry data can supply further categories.
    inline std::optional<Definition> BuiltinDefinition(std::string_view name) {
        if (name == "minecraft:ender_pearl")
            return Definition{"ender_pearl"};
        if (name == "minecraft:goat_horn")
            return Definition{"goat_horn"};
        if (name == "minecraft:ice_bomb")
            return Definition{"ice_bomb"};
        if (name == "minecraft:shield")
            return Definition{"shield"};
        return std::nullopt;
    }

    // Accept the documented item-definition shape when a server exposes it in
    // registry NBT. The protocol metadata leaves component payloads opaque.
    inline std::optional<Definition> ComponentDefinition(const json::Value* component) {
        if (!component || !component->IsObject())
            return std::nullopt;
        const json::Value* category = component->Find("category");
        if (!category || !category->IsString())
            return std::nullopt;
        Definition out{category->string};
        if (const json::Value* type = component->Find("type")) {
            if (!type->IsString())
                return std::nullopt;
            if (type->string == "attack")
                out.type = Type::Attack;
            else if (type->string != "use")
                return std::nullopt;
        }
        return out;
    }

    class State {
    public:
        void Start(std::string_view category, int32_t ticks) {
            remaining[CategoryKey(category)] = ticks;
        }

        int32_t Remaining(std::string_view category) const {
            const auto it = remaining.find(CategoryKey(category));
            return it == remaining.end() ? 0 : it->second;
        }

        bool Active(std::string_view category) const { return Remaining(category) > 0; }

        void Tick() {
            for (auto& [category, ticks] : remaining)
                if (ticks > 0)
                    --ticks;
        }

        void Clear() { remaining.clear(); }

    private:
        std::map<std::string, int32_t, std::less<>> remaining;
    };
}
