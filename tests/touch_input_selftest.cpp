#include "client/systems/input/TouchControls.hpp"
#include "client/systems/input/Input.hpp"
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <thread>
using namespace perlin;
static void Check(bool ok, const char* why) {
    if (!ok) {
        std::cerr << why << '\n';
        std::exit(1);
    }
}
int main() {
    TouchControls touch;
    Input input;
    TouchState state;
    {
        TouchControls pad;
        Input padInput;
        pad.SetViewport(1000, 1000);
        pad.SetState(state);
        padInput.SetCaptured(true);
        pad.Update(padInput);
        pad.FingerDown(padInput, 1, 222, 663);
        pad.Update(padInput);
        Check(padInput.IsKeyDown('W'), "dpad up did not move forward");
        pad.FingerUp(padInput, 1, 222, 663);
        pad.Update(padInput);
        Check(!padInput.IsKeyDown('W'), "dpad release leaves forward held");
    }
    {
        TouchControls t;
        Input in;
        TouchState ts;
        ts.interaction = 0;
        ts.holdDelay = 0.1f;
        t.SetViewport(1000, 1000);
        t.SetState(ts);
        in.SetCaptured(true);
        t.Update(in);
        t.FingerDown(in, 1, 500, 300);
        float ax = 0, ay = 0;
        const auto now = [] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        };
        Check(in.TouchAim(now(), ax, ay) && ax == 0.5f && ay == 0.3f, "touch aim not at finger");
        t.FingerUp(in, 1, 500, 300);
        Check(in.TakeTouchTaps() == 1, "touch tap not reported");
        t.FingerDown(in, 2, 500, 300);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        t.Update(in);
        Check(in.TouchHold(), "touch hold not reported");
        t.FingerUp(in, 2, 500, 300);
        Check(!in.TouchHold() && in.TakeTouchTaps() == 0, "touch hold leaks");
        t.FingerDown(in, 3, 500, 300);
        t.FingerMove(in, 3, 600, 300, 100, 0);
        Check(!in.TouchAim(now(), ax, ay), "drag keeps touch aim");
        t.FingerUp(in, 3, 600, 300);
        Check(in.TakeTouchTaps() == 0, "drag counted as tap");
        ts.interaction = 1;
        t.SetState(ts);
        t.FingerDown(in, 4, 500, 300);
        t.FingerUp(in, 4, 500, 300);
        Check(in.TakeTouchTaps() == 0, "crosshair mode taps world");
    }
    state.interaction = 1;
    state.dpad = false;
    touch.SetViewport(1000, 1000);
    touch.SetState(state);
    input.SetCaptured(true);
    touch.Update(input);
    touch.FingerDown(input, 1, 860, 830);
    Check(input.IsKeyDown(Key::Space), "jump touch did not press");
    state.editing = true;
    touch.SetState(state);
    touch.Update(input);
    Check(!input.IsKeyDown(Key::Space), "editor leaves jump held");
    touch.FingerUp(input, 1, 860, 830);
    state.editing = false;
    touch.SetState(state);
    touch.Update(input);
    touch.FingerDown(input, 2, 200, 800);
    touch.FingerMove(input, 2, 200, 660, 0, -140);
    Check(input.IsKeyDown('W'), "stick did not move forward");
    state.editing = true;
    touch.SetState(state);
    touch.FingerDown(input, 3, 500, 400);
    Check(!input.IsKeyDown('W'), "editor event leaves stick held");
    touch.Update(input);
    state.editing = false;
    touch.SetState(state);
    touch.Update(input);
    input.SetCaptured(false);
    touch.Update(input);
    touch.FingerDown(input, 4, 300, 300);
    Check(input.IsMouseDown(Mouse::Left), "menu pointer did not press");
    state.editing = true;
    touch.SetState(state);
    touch.Update(input);
    Check(!input.IsMouseDown(Mouse::Left), "editor loses pointer release");
    state.editing = false;
    touch.SetState(state);
    touch.Update(input);
    input.SetCaptured(true);
    touch.Update(input);
    touch.FingerDown(input, 5, 860, 830);
    Check(input.IsKeyDown(Key::Space), "jump broken after editor exit");
    touch.Cancel(input);
    Check(!input.IsKeyDown(Key::Space), "cancel leaves jump held");
    state.editing = true;
    touch.SetState(state);
    touch.Update(input);
    touch.FingerDown(input, 8, 860, 830);
    touch.FingerMove(input, 8, 800, 700, -60, -130);
    touch.FingerUp(input, 8, 800, 700);
    touch.FingerDown(input, 9, 840, 500);
    touch.SetState(state);
    std::map<std::string, TouchPlace> edited;
    Check(touch.TakeEditResult(edited), "Done result lost when state refreshes");
    Check(!touch.Editing(), "Done reopens the editor");
    Check(edited.contains("jump") && edited["jump"].x == .8f && edited["jump"].y == .7f,
          "Done lost the dragged button position");
    state.editing = false;
    touch.SetState(state);
    touch.Update(input);
    std::cout << "touch editor input lifetimes passed\n";
}
