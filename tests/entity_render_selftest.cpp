#include "client/systems/attachable/Geometry.hpp"
#include "client/systems/entity/EntityQuery.hpp"
#include "client/systems/entity/EntityScale.hpp"
#include "client/systems/entity/BlockEntityView.hpp"
#include "world/level/block/BlockPalette.hpp"
#include <cstdlib>
#include <iostream>
using namespace perlin;
static void Check(bool ok, const char* why) {
    if (!ok) {
        std::cerr << why << '\n';
        std::exit(1);
    }
}
int main() {
    Texture sprite;
    sprite.width = 3;
    sprite.height = 1;
    sprite.pixels = {255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0};
    const auto mesh = attachable::ExtrudeSprite(sprite, 0, 0, 3, 1, 3, 1, 0.5f);
    Check(mesh.size() == 10, "sprite emitted transparent pixels or shared interior walls");
    bool front = false, back = false;
    for (const auto& q : mesh) {
        front |= q.n.z > 0;
        back |= q.n.z < 0;
        for (const auto& p : q.p)
            Check(std::abs(p.z) == 0.25f, "orb lost its depth");
    }
    Check(front && back, "orb is one-sided");

    entity::EntityTracker tracker;
    tracker.AddEntity(7, 9, "minecraft:skeleton", {}, 0, 0, 0, 0, 1, 0, 0, 0);
    tracker.SetHeld(7, "minecraft:bow");
    tracker.SetTarget(7, -9007199254740993ll);
    tracker.SetFlagsExtended(7, (1ull << 60) | entity::flag::FacingTargetToRangeAttack);
    entity::Entity actor;
    Check(tracker.Find(7, actor), "missing skeleton");
    entity::QueryContext context;
    context.e = &actor;
    const auto query = [&](const char* name) {
        molang::Value out;
        Check(entity::EvalEntityQuery(context, name, {}, out), "unknown skeleton query");
        return out.Truthy();
    };
    Check(query("has_target") && query("facing_target_to_range_attack") && actor.AimingBow(),
          "bow state discarded");
    Check(!query("is_charging"), "ranged aim confused with charge attack");
    tracker.SetFlagsExtended(7, 0);
    tracker.SetFlags(7, entity::flag::Charging);
    tracker.Find(7, actor);
    Check(!query("facing_target_to_range_attack") && query("is_charging"),
          "attack transitions remained latched");
    tracker.SetTarget(7, -1);
    tracker.Find(7, actor);
    Check(!query("has_target"), "invalid target still activates bow controller");

    tracker.AddPlayer(8, 10, "uuid", "Giant", {}, 0, 0, 0, 0, "");
    tracker.SetScale(8, 2.0f);
    entity::Entity giant;
    Check(tracker.Find(8, giant), "missing scaled player");
    Check(entity::ModelScale(giant, 0.9375f) == 1.875f, "player model ignores the metadata scale");
    Check(entity::PlayerBox(giant).second == 3.6f, "player name tag height ignores the metadata scale");
    tracker.SetScale(7, 0.5f);
    tracker.Find(7, actor);
    Check(entity::ModelScale(actor, 0.0f) == 0.5f, "mob model ignores the metadata scale");
    tracker.SetScale(7, 0.0f);
    tracker.Find(7, actor);
    Check(entity::ModelScale(actor, 1.0f) == 1.0f, "zero scale collapsed the model matrix");

    auto& palette = BlockPalette::Instance();
    using State = BlockPalette::StateValue;
    const std::vector<std::pair<std::string, State>> footStates = {{"direction", {State::Int, 0, ""}},
                                                                   {"head_piece_bit", {State::Bool, 0, ""}}};
    auto headStates = footStates;
    headStates[1].second.i = 1;
    const auto makeBed = [&](const auto& states) {
        const auto hash = BlockPalette::HashState("minecraft:bed", states);
        palette.LearnState(hash, "minecraft:bed", states);
        return palette.Intern(hash);
    };
    const auto foot = makeBed(footStates), head = makeBed(headStates);
    BlockEntityStore store;
    json::Value nbt;
    std::string error;
    Check(json::Value::Parse("{\"id\":\"Bed\",\"color\":3}", nbt, error), "invalid bed fixture");
    store.Set(0, 61, 1, nbt);
    std::vector<SpecialBlock> specials = {{0, 61, 0, foot}, {0, 61, 1, head}, {0, 317, 1, foot}};
    std::vector<entity::BlockEntityDraw> draws;
    entity::BuildBlockEntityDraws(specials, store, draws);
    Check(draws.size() == 2, "bed halves duplicated or different beds collided");
    Check(draws.front().color == 3, "bed head colour was lost");
    std::cout << "entity render regressions passed\n";
}
