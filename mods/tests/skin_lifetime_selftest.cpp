#include "client/entity/EntityTracker.hpp"
#include <cstdlib>
#include <iostream>

using namespace perlin::entity;
static void Check(bool ok, const char* why) {
    if (!ok) {
        std::cerr << why << '\n';
        std::exit(1);
    }
}
int main() {
    EntityTracker tracker;
    for (int i = 0; i < 2048; ++i) {
        auto skin = std::make_shared<SkinData>();
        skin->skinRgba.resize(128 * 128 * 4);
        std::weak_ptr<const SkinData> owner = skin;
        const auto uuid = std::to_string(i);
        tracker.SetSkin(uuid, std::move(skin));
        tracker.RemoveSkin(uuid);
        Check(owner.expired(), "departed tab-list skin retained");
    }
    auto skin = std::make_shared<SkinData>();
    std::weak_ptr<const SkinData> owner = skin;
    tracker.SetSkin("visible", std::move(skin));
    tracker.AddPlayer(10, 20, "visible", "player", {}, 0, 0, 0, 0, "");
    tracker.RemoveSkin("visible");
    Check(!owner.expired(), "tab removal discarded a visible actor's skin");
    auto replacement = std::make_shared<SkinData>();
    std::weak_ptr<const SkinData> replacementOwner = replacement;
    tracker.SetSkin("visible", std::move(replacement), false);
    Check(owner.expired(), "skin replacement retained old skin");
    tracker.Remove(20);
    Check(replacementOwner.expired(), "skin update recreated a removed tab-list cache entry");
    std::cout << "skin lifetimes passed\n";
}
