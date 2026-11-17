#pragma once
#include "client/systems/mods/ModManager.hpp"
#include "client/systems/renderer/gpu/ModGpuHub.hpp"
#include "client/systems/resources/ServerBlocks.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/systems/resources/ShaderPack.hpp"
#include "client/systems/resources/TexturePack.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/systems/text/McFont.hpp"
#include "client/ui/JsonScreen.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "world/level/lighting/LightRegistry.hpp"

namespace perlin::selftest {
    struct Globals {
        ui::UiAssets& uiAssets = ui::UiAssets::Instance();
        TexturePack& texturePack = TexturePack::Instance();
        ServerPacks& serverPacks = ServerPacks::Instance();
        ServerBlocks& serverBlocks = ServerBlocks::Instance();
        ui::JsonScreens& jsonScreens = ui::JsonScreens::Instance();
        ui::McFont& mcFont = ui::McFont::Instance();
        ShaderPack& shaderPack = ShaderPack::Instance();
        BlockPalette& blockPalette = BlockPalette::Instance();
        LightRegistry& lightRegistry = LightRegistry::Instance();
        mods::ModManager& modManager = mods::ModManager::Instance();
        gpu::ModGpuHub& modGpuHub = gpu::ModGpuHub::Instance();
    };

    inline Globals& G() {
        static Globals globals;
        return globals;
    }
}
