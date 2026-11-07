#include "client/hand/HandAnim.hpp"

#include <cstdio>

using namespace perlin;

static int checks = 0, failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

static ui::HudItem Stack(const char* name, int count) {
    ui::HudItem item;
    item.name = name;
    item.count = count;
    return item;
}

int main() {
    CHECK(hand::SwingTicks(6, -1, -1) == 6);
    CHECK(hand::SwingTicks(6, 1, -1) == 4);
    CHECK(hand::SwingTicks(6, 0, 2) == 5);
    CHECK(hand::SwingTicks(6, -1, 0) == 8);
    CHECK(hand::SwingTicks(6, -1, 2) == 12);

    {
        hand::Swing swing;
        swing.Start();
        CHECK(swing.Progress(0.5f) == 0.0f);
        const float expected[] = {0.0f, 1.0f / 6.0f, 2.0f / 6.0f, 3.0f / 6.0f, 4.0f / 6.0f, 5.0f / 6.0f};
        for (float e : expected) {
            swing.Tick(6);
            CHECK(swing.swinging && swing.anim == e);
        }
        CHECK(swing.Progress(1.0f) == 5.0f / 6.0f);
        swing.Tick(6);
        // ended: straight back to rest, no 5/6 -> 1 blend
        CHECK(!swing.swinging && swing.Progress(0.5f) == 0.0f);
    }
    {
        // held repeat every 4 ticks: restarts from the second half of a 6 tick swing
        hand::Swing swing;
        swing.Start();
        for (int i = 0; i < 2; i++)
            swing.Tick(6);
        swing.Start();
        CHECK(swing.time == 1);
        swing.Tick(6);
        swing.Tick(6);
        swing.Start();
        CHECK(swing.time == -1);
        swing.Tick(6);
        CHECK(swing.anim == 0.0f && swing.oAnim == 3.0f / 6.0f);
        // the wrap from 3/6 back to 0 keeps moving forward
        CHECK(swing.Progress(0.5f) == 3.0f / 6.0f + 0.25f);
    }
    {
        hand::Swing swing;
        swing.Start();
        swing.Tick(0);
        CHECK(!swing.swinging && swing.anim == 0.0f);
    }

    {
        // placing: the count drops, the hand stays up and shows the new count
        ui::HudItem shown = Stack("minecraft:stone", 64);
        int slot = 0;
        float height = 1.0f;
        hand::TickEquip(shown, slot, height, Stack("minecraft:stone", 63), 0, true);
        CHECK(height == 1.0f && shown.count == 63);
        // last block placed: empty hand lowers then swaps
        hand::TickEquip(shown, slot, height, ui::HudItem{}, 0, false);
        CHECK(height == 0.6f && shown.count == 63);
        hand::TickEquip(shown, slot, height, ui::HudItem{}, 0, false);
        hand::TickEquip(shown, slot, height, ui::HudItem{}, 0, false);
        CHECK(height == 0.0f && shown.Empty());
    }
    {
        // next stack of the same block in another slot: dips like any switch
        ui::HudItem shown = Stack("minecraft:stone", 1);
        int slot = 0;
        float height = 1.0f;
        const ui::HudItem next = Stack("minecraft:stone", 64);
        hand::TickEquip(shown, slot, height, next, 1, true);
        CHECK(height == 0.6f && slot == 0);
        hand::TickEquip(shown, slot, height, next, 1, true);
        hand::TickEquip(shown, slot, height, next, 1, true);
        CHECK(height == 0.0f && slot == 1 && shown.count == 64);
        hand::TickEquip(shown, slot, height, next, 1, true);
        CHECK(height == 0.4f);
    }
    {
        // switching between two empty slots keeps the arm up
        ui::HudItem shown;
        int slot = 0;
        float height = 1.0f;
        hand::TickEquip(shown, slot, height, ui::HudItem{}, 3, true);
        CHECK(height == 1.0f);
    }

    std::printf("hand_anim_selftest: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
