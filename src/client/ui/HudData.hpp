#pragma once
#include "client/systems/model/HudState.hpp"
#include "client/ui/UiLayout.hpp"

namespace perlin::ui {
    class HudData final : public UiData {
    public:
        explicit HudData(const HudState& state) : s(state) {}

        bool Global(const std::string& name, json::Value& out) const override;
        int CollectionSize(const std::string& collection) const override;
        bool CollectionValue(const std::string& collection, int index, const std::string& binding,
                             json::Value& out) const override;
        std::vector<FactoryItem> Factory(const std::string& name) const override;

    private:
        const HudState& s;
    };

    Vars EngineRootVariables();

    uint64_t HudSignature(const HudState& s);
}
