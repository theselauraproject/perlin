#include "client/systems/multiplayer/GuiDataState.hpp"

#include <cstdio>

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    };
    perlin::GuiDataState state;
    perlin::GuiDataState::Notice notice;
    check(!state.TakeNotice(notice), "initial state has no notice");
    check(state.GetFlash().slot == -1, "initial state has no flash slot");
    const auto time = perlin::GuiDataState::Clock::time_point{} + std::chrono::seconds(5);
    state.Pick("old", "old effect", 1, time);
    state.Pick("new", "new effect", 8, time + std::chrono::seconds(1));
    check(state.TakeNotice(notice) && notice.name == "new" && notice.effects == "new effect",
          "a second packet replaces an unconsumed popup");
    check(!state.TakeNotice(notice), "a popup is consumed once");
    check(state.GetFlash().slot == 8 && state.GetFlash().at == time + std::chrono::seconds(1),
          "consuming the popup preserves independent flash state");
    state.Pick("edge", "", -3, time);
    for (int i = 0; i < 40; ++i)
        state.Tick();
    check(state.TakeNotice(notice), "single-precision reference age is still below two after 40 ticks");
    check(state.GetFlash().slot == -3, "producer preserves the signed slot without selecting it");
    state.Pick("expired", "", 100, time);
    for (int i = 0; i < 41; ++i)
        state.Tick();
    check(!state.TakeNotice(notice), "an unread popup expires at the reference age boundary");
    state.Pick("", "effects only", 0, time);
    check(state.TakeNotice(notice) && notice.name.empty() && notice.effects == "effects only",
          "an empty title does not suppress a new notice");
    state.Clear();
    check(!state.TakeNotice(notice) && state.GetFlash().slot == -1 &&
              state.GetFlash().at == perlin::GuiDataState::Clock::time_point{},
          "reset clears both notice and flash state");
    if (failures == 0)
        std::puts("gui_data_selftest: ok");
    return failures ? 1 : 0;
}
