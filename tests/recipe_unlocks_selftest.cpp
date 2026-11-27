#include "client/systems/multiplayer/RecipeUnlocks.hpp"

#include <cstdio>
#include <set>

int main() {
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    };
    perlin::recipes::Unlocks state;
    std::set<std::string> known{"recipe:a", "recipe:b", "recipe:c"};
    const auto exists = [&](const std::string& id) { return known.contains(id); };
    check(state.Allows("recipe:a", false), "absent unlock component keeps pre-packet recipe visibility");
    state.Apply(99, {"recipe:a"}, exists);
    check(state.Allows("recipe:c", false), "unknown packet action does not establish recipe knowledge");
    state.Apply(0, {"recipe:a"}, exists);
    check(!state.Allows("recipe:a", false), "Empty establishes a component without adding its recipe list");
    state.Apply(1, {"recipe:a", "recipe:a", "recipe:missing"}, exists);
    check(state.Allows("recipe:a", false) && !state.Allows("recipe:b", false),
          "initial unlocks include only listed known recipe IDs");
    known.insert("recipe:missing");
    check(!state.Allows("recipe:missing", false), "an unrecognized ID is not deferred for a future registry");
    state.Apply(1, {"recipe:b"}, exists);
    check(state.Allows("recipe:a", false) && state.Allows("recipe:b", false),
          "successive initial lists add knowledge rather than replacing it");
    state.Apply(2, {"recipe:c"}, exists);
    check(state.Allows("recipe:a", false) && state.Allows("recipe:c", false),
          "new unlocks add to existing knowledge");
    state.Apply(3, {"recipe:a", "recipe:absent"}, exists);
    check(!state.Allows("recipe:a", false) && state.Allows("recipe:b", false),
          "remove revokes only the specified IDs");
    check(state.Allows("recipe:a", true), "AlwaysUnlocked context bypasses the server membership set");
    state.Apply(4, {}, exists);
    check(!state.Allows("recipe:b", false) && !state.Allows("recipe:c", false) &&
              state.Allows("recipe:a", true),
          "remove-all clears membership but preserves unconditional recipes");
    state.Clear();
    check(state.Allows("recipe:b", false), "session reset removes prior recipe knowledge and presence");
    if (failures == 0)
        std::puts("recipe_unlocks_selftest: all checks passed");
    return failures == 0 ? 0 : 1;
}
