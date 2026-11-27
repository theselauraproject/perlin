#include "client/systems/multiplayer/ItemCooldowns.hpp"

#include <cstdio>
#include <limits>

using namespace perlin;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                            \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

int main() {
    using itemcooldown::State;
    using itemcooldown::Type;
    State state;
    CHECK(!state.Active("ender_pearl"));
    state.Start("ender_pearl", 3);
    CHECK(state.Remaining("minecraft:ender_pearl") == 3);
    CHECK(state.Active("ender_pearl"));
    CHECK(!state.Active("example:ender_pearl"));
    state.Tick();
    CHECK(state.Remaining("minecraft:ender_pearl") == 2);
    state.Start("minecraft:ender_pearl", 1);
    CHECK(state.Remaining("ender_pearl") == 1);
    state.Tick();
    CHECK(state.Remaining("ender_pearl") == 0);
    CHECK(!state.Active("ender_pearl"));
    state.Tick();
    CHECK(state.Remaining("ender_pearl") == 0);

    state.Start("example:group", 20);
    state.Start("example:group", 2);
    state.Start("example:other", 7);
    state.Tick();
    CHECK(state.Remaining("example:group") == 1);
    CHECK(state.Remaining("example:other") == 6);
    CHECK(state.Remaining("group") == 0);
    state.Start("example:group", 0);
    CHECK(!state.Active("example:group"));

    for (int32_t duration : {-1, std::numeric_limits<int32_t>::min()}) {
        state.Start("negative", duration);
        CHECK(!state.Active("negative"));
        state.Tick();
        CHECK(state.Remaining("negative") == duration);
    }
    state.Start("maximum", std::numeric_limits<int32_t>::max());
    state.Tick();
    CHECK(state.Remaining("maximum") == std::numeric_limits<int32_t>::max() - 1);
    state.Start("", 2);
    CHECK(state.Remaining("minecraft:") == 2);
    state.Clear();
    CHECK(state.Remaining("example:other") == 0);
    CHECK(state.Remaining("maximum") == 0);
    CHECK(state.Remaining("negative") == 0);

    for (const char* name : {"ender_pearl", "goat_horn", "ice_bomb", "shield"}) {
        const auto definition = itemcooldown::BuiltinDefinition(std::string("minecraft:") + name);
        CHECK(definition && definition->category == name && definition->type == Type::Use);
    }
    CHECK(!itemcooldown::BuiltinDefinition("example:ender_pearl"));
    CHECK(!itemcooldown::BuiltinDefinition("minecraft:stone"));

    json::Value component = json::Value::MakeObject();
    CHECK(!itemcooldown::ComponentDefinition(nullptr));
    CHECK(!itemcooldown::ComponentDefinition(&component));
    component.Set("category", json::Value("example:shared"));
    auto definition = itemcooldown::ComponentDefinition(&component);
    CHECK(definition && definition->category == "example:shared" && definition->type == Type::Use);
    component.Set("type", json::Value("attack"));
    definition = itemcooldown::ComponentDefinition(&component);
    CHECK(definition && definition->type == Type::Attack);
    component.Set("type", json::Value("use"));
    definition = itemcooldown::ComponentDefinition(&component);
    CHECK(definition && definition->type == Type::Use);
    component.Set("type", json::Value("invalid"));
    CHECK(!itemcooldown::ComponentDefinition(&component));
    component.Set("type", json::Value(1));
    CHECK(!itemcooldown::ComponentDefinition(&component));
    component.Set("type", json::Value("use"));
    component.Set("category", json::Value(1));
    CHECK(!itemcooldown::ComponentDefinition(&component));

    std::printf(failures ? "%d item cooldown checks failed\n" : "item_cooldown_selftest: ok\n", failures);
    return failures ? 1 : 0;
}
