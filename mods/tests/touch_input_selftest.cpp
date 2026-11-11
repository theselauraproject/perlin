#include "client/input/TouchControls.hpp"
#include "client/input/Input.hpp"
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
    TouchControls touch;
    Input input;
    TouchState state;
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
    touch.FingerDown(input, 9, 755, 537);
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
