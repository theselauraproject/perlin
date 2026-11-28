#include "client/systems/config/Options.hpp"
#include "client/systems/input/Input.hpp"
#include "client/systems/input/InputManager.hpp"

#include <cstdio>

using namespace perlin;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

namespace {
    // The use-button decision a frame makes: a press starts a use, and a held button that was not
    // pressed this frame is a held repeat.
    struct Frame {
        bool press = false;
        bool repeat = false;
    };

    Frame Sample(const InputManager& manager) {
        const bool pressed = manager.Pressed(Action::Use);
        return {pressed, manager.IsDown(Action::Use) && !pressed};
    }
}

int main() {
    {
        Input input;
        Options options;
        InputManager manager(input, options);
        manager.Poll();
        input.OnMouseDown(Mouse::Right);
        const Frame clickedAfterPoll = Sample(manager);
        CHECK(!clickedAfterPoll.press && !clickedAfterPoll.repeat);
        manager.Poll();
        const Frame next = Sample(manager);
        CHECK(next.press && !next.repeat);
        manager.Poll();
        const Frame held = Sample(manager);
        CHECK(!held.press && held.repeat);
        input.OnMouseUp(Mouse::Right);
        CHECK(!manager.IsDown(Action::Use));
        manager.Poll();
        CHECK(!manager.IsDown(Action::Use) && !manager.Pressed(Action::Use));
    }
    {
        Input input;
        Options options;
        InputManager manager(input, options);
        int presses = 0, repeats = 0;
        for (int frame = 0; frame < 6; frame++) {
            manager.Poll();
            if (frame == 2)
                input.OnMouseDown(Mouse::Right);
            if (frame == 5)
                input.OnMouseUp(Mouse::Right);
            const Frame f = Sample(manager);
            presses += f.press;
            repeats += f.repeat;
        }
        CHECK(presses == 1);
        CHECK(repeats == 1);
    }
    {
        Input input;
        Options options;
        InputManager manager(input, options);
        input.OnMouseDown(Mouse::Right);
        input.OnMouseUp(Mouse::Right);
        manager.Poll();
        CHECK(manager.Pressed(Action::Use) && !manager.IsDown(Action::Use));
    }
    {
        Input input;
        Options options;
        InputManager manager(input, options);
        input.OnMouseDown(Mouse::Right);
        manager.Poll();
        CHECK(manager.IsDown(Action::Use));
        input.ReleaseAll();
        manager.Poll();
        CHECK(!manager.IsDown(Action::Use));
        input.OnMouseDown(Mouse::Right);
        CHECK(!manager.IsDown(Action::Use));
        manager.Poll();
        CHECK(manager.Pressed(Action::Use) && manager.IsDown(Action::Use));
    }
    {
        Input input;
        Options options;
        InputManager manager(input, options);
        input.OnMouseDown(Mouse::Left);
        CHECK(!manager.IsDown(Action::Attack));
        manager.Poll();
        CHECK(manager.Pressed(Action::Attack) && manager.IsDown(Action::Attack));
        input.OnKeyDown(options.GetKey(Action::Forward).code);
        CHECK(manager.IsDown(Action::Forward));
    }

    std::printf(failures ? "%d FAILED\n" : "input press self-tests passed\n", failures);
    return failures ? 1 : 0;
}
