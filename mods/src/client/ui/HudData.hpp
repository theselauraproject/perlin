#pragma once
#include "client/ui/HudState.hpp"
#include "client/ui/UiLayout.hpp"

namespace perlin::ui {
    class HudData final : public UiData {
    public:
        explicit HudData(const HudState& state) : s(state) {}

        bool Global(const std::string& name, Json& out) const override;
        int CollectionSize(const std::string& collection) const override;
        bool CollectionValue(const std::string& collection, int index, const std::string& binding,
                             Json& out) const override;
        std::vector<FactoryItem> Factory(const std::string& name) const override;

    private:
        const HudState& s;
    };

    Vars EngineRootVariables();

    uint64_t HudSignature(const HudState& s);
}
