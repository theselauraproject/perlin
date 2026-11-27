#include "client/selftest/SelfTests.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "network/Stream.hpp"

#include <algorithm>
#include <cstdio>

namespace {
    int failures = 0;

    void Check(bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    }

    template<class Packet> void Inject(perlin::BedrockSession& session, const Packet& packet) {
        session.InjectIncoming(Packet::kId, perlin::net::Encode(packet));
    }

    perlin::proto::ShapelessRecipe Recipe(std::string id, std::string ingredient, int output,
                                         bool always = false) {
        using namespace perlin::proto;
        ShapelessRecipe recipe;
        recipe.recipeId = std::move(id);
        recipe.block = "crafting_table";
        recipe.networkId = static_cast<uint32_t>(output);
        RecipeIngredient input;
        input.type = RecipeIngredientType::Valid;
        input.descriptorType = "name";
        input.name = std::move(ingredient);
        input.count = 1;
        recipe.input.push_back(input);
        ItemLegacy result;
        result.networkId = output;
        result.count = 1;
        result.extra = ItemExtraDataWithoutBlockingTick{};
        recipe.output.push_back(result);
        recipe.unlockingRequirement.emplace();
        recipe.unlockingRequirement->context = always ? RecipeUnlockingRequirementContext::AlwaysUnlocked
                                                      : RecipeUnlockingRequirementContext::None;
        if (!always)
            recipe.unlockingRequirement->ingredients = std::vector<RecipeIngredient>{input};
        return recipe;
    }

    void Unlock(perlin::BedrockSession& session, int type, std::vector<std::string> names = {}) {
        perlin::proto::UnlockedRecipesPacket packet;
        packet.unlockType = static_cast<perlin::proto::UnlockedRecipesPacketUnlockType>(type);
        packet.recipes = std::move(names);
        Inject(session, packet);
    }

    void Rule(perlin::BedrockSession& session, bool value) {
        perlin::proto::GameRulesChangedPacket packet;
        perlin::proto::GameRuleI32 rule;
        rule.name = "recipesUnlock";
        rule.type = perlin::proto::GameRuleI32Type::Bool;
        rule.value = value;
        packet.rules.push_back(rule);
        Inject(session, packet);
    }

    bool Has(const std::vector<perlin::BedrockSession::RecipeEntry>& recipes, const char* item) {
        return std::ranges::any_of(recipes, [&](const auto& recipe) { return recipe.out.name == item; });
    }
}

namespace perlin::selftest {
    int RecipePackets() {
        failures = 0;
        BedrockSession session;
        session.BeginReplayMode("recipes", 7, kBedrockProtocol);
        int sends = 0;
        session.SetSentTap([&](uint32_t, const std::vector<uint8_t>&) { ++sends; });
        proto::ItemRegistryPacket registry;
        const char* names[]{"minecraft:oak_planks", "minecraft:cobblestone", "minecraft:stick",
                            "minecraft:crafting_table", "minecraft:bread", "minecraft:iron_ingot"};
        for (int index = 0; index < 6; index++) {
            proto::ItemstatesEntry item;
            item.name = names[index];
            item.runtimeId = static_cast<int16_t>(index + 1);
            registry.itemstates.push_back(item);
        }
        Inject(session, registry);
        proto::InventorySlotPacket inventory;
        inventory.windowId = proto::WindowIDVarint::Inventory;
        inventory.item.networkId = 1;
        inventory.item.count = 8;
        inventory.item.hasStackId = true;
        inventory.item.stackId = 51;
        Inject(session, inventory);
        proto::CraftingDataPacket data;
        data.clearRecipes = true;
        data.shapelessRecipes = {Recipe("test:stone_stick", "minecraft:cobblestone", 3),
                                 Recipe("test:wood_stick", "minecraft:oak_planks", 3),
                                 Recipe("test:table", "minecraft:oak_planks", 4, true),
                                 Recipe("test:bread", "minecraft:oak_planks", 5)};
        Inject(session, data);
        Check(session.GetRecipes(2).size() == 3,
              "before the first unlock packet the existing recipe catalog remains visible");
        Unlock(session, 0);
        Check(session.GetRecipes(2).size() == 1 && Has(session.GetRecipes(2), "minecraft:crafting_table"),
              "Empty creates recipe knowledge without unlocking conditional recipes");
        Unlock(session, 1, {"test:stone_stick", "test:stone_stick"});
        auto visible = session.GetRecipes(2);
        const auto stick = std::ranges::find_if(visible, [](const auto& item) { return item.out.name == "minecraft:stick"; });
        Check(visible.size() == 2 && stick != visible.end() && stick->craftable && stick->index == 1,
              "one unlocked alternative exposes its output while preserving craftable recipe selection");
        Unlock(session, 1, {"test:bread"});
        Check(session.GetRecipes(2).size() == 3, "initial recipe batches add to existing knowledge");
        Unlock(session, 3, {"test:stone_stick"});
        Check(!Has(session.GetRecipes(2), "minecraft:stick") && Has(session.GetRecipes(2), "minecraft:bread"),
              "remove revokes the specified output without clearing other unlocked recipes");
        const int sendsBeforeStaleSelection = sends;
        session.InvFillRecipe(1);
        Check(sends == sendsBeforeStaleSelection && session.GetInventory().hotbar[0].count == 8,
              "a stale locked recipe selection cannot move inventory items");
        Unlock(session, 2, {"test:wood_stick"});
        Check(Has(session.GetRecipes(2), "minecraft:stick"), "newly unlocked recipes reach the visible book");
        Unlock(session, 4);
        Check(session.GetRecipes(2).size() == 1, "remove-all keeps AlwaysUnlocked recipes visible");
        session.InjectIncoming(proto::UnlockedRecipesPacket::kId, {1, 0, 0, 0, 1, 6, 'x'});
        Check(session.GetRecipes(2).size() == 1, "truncated unlock packet leaves recipe knowledge unchanged");
        Unlock(session, 2, {"test:future"});
        data.clearRecipes = false;
        data.shapelessRecipes = {Recipe("test:future", "minecraft:oak_planks", 6)};
        Inject(session, data);
        Check(!Has(session.GetRecipes(2), "minecraft:iron_ingot"),
              "unknown recipe IDs are not deferred across a later catalog update");
        Unlock(session, 2, {"test:future"});
        Check(Has(session.GetRecipes(2), "minecraft:iron_ingot"),
              "an update for a now-known recipe changes the book");
        Rule(session, false);
        Check(session.GetRecipes(2).size() == 4, "recipesUnlock=false exposes the full crafting catalog");
        Rule(session, true);
        Check(session.GetRecipes(2).size() == 2, "reenabling recipe unlocking retains the server's knowledge");
        proto::SetPlayerGameTypePacket gameType;
        gameType.gamemode = static_cast<decltype(gameType.gamemode)>(1);
        Inject(session, gameType);
        Check(session.GetRecipes(2).size() == 4, "creative mode exposes the full recipe catalog");
        gameType.gamemode = static_cast<decltype(gameType.gamemode)>(0);
        Inject(session, gameType);
        Check(session.GetRecipes(2).size() == 2, "survival restores the retained unlock state");
        Check(sends == sendsBeforeStaleSelection, "recipe synchronization emits no network acknowledgements");
        Unlock(session, 2, {"test:wood_stick"});
        session.InvFillRecipe(1);
        Check(sends > sendsBeforeStaleSelection && session.GetInventory().craft[0].name == "minecraft:oak_planks",
              "an unlocked recipe remains usable through the normal inventory request path");
        session.SetSentTap({});
        if (failures == 0)
            std::puts("recipe_packets_test: ok");
        return failures == 0 ? 0 : 1;
    }
}
