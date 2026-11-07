#include "client/input/TouchControls.hpp"

#include "client/input/Input.hpp"
#include "client/input/Keys.hpp"
#include "client/input/TouchLayout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace perlin {
    using namespace touchlayout;
    namespace {
        using Clock = std::chrono::steady_clock;
        enum StickKey { KeyForward, KeyBack, KeyLeft, KeyRight, KeySprint };

        constexpr float kStickRadius = 0.14f;
        constexpr float kDeadZone = 0.22f;
        constexpr float kSprintZone = 0.92f;
        constexpr float kLookScale = 0.55f;
        constexpr float kTapSlop = 18.0f;
        constexpr float kScrollStep = 36.0f;
        constexpr const char* kButtonNames[] = {"jump", "sneak", "attack", "use", "sprint", "pause",
                                                "chat", "f5",    "f3",     "tab", "f1",     "inventory"};
    }

    bool TouchControls::Gameplay(const Input& input) const {
        return input.Captured() && !input.TextMode();
    }

    void TouchControls::SetViewport(int w, int h) {
        std::lock_guard lock(mutex);
        if (w == width && h == height)
            return;
        width = w;
        height = h;
        BuildLayout();
    }

    void TouchControls::SetState(const TouchState& s) {
        std::lock_guard lock(mutex);
        const bool relayout =
            s.hudScale != state.hudScale || s.scale != state.scale || (!editing && s.layout != state.layout);
        const bool startEditing = s.editing && !editing && !editDone;
        const bool stopEditing = !s.editing && editing;
        if (startEditing || stopEditing)
            cancelPending = true;
        state = s;
        if (startEditing) {
            editing = true;
            editDone = false;
            editSelected = -1;
            editLayout = s.layout;
        } else if (!s.editing && editing) {
            editing = false;
        }
        if ((relayout || startEditing || stopEditing) && width > 0)
            BuildLayout();
    }

    bool TouchControls::TakeEditResult(std::map<std::string, TouchPlace>& out) {
        std::lock_guard lock(mutex);
        if (!editDone)
            return false;
        editDone = false;
        out = editLayout;
        return true;
    }

    TouchPlace TouchControls::Place(const char* name) const {
        const auto& source = editing ? editLayout : state.layout;
        const auto it = source.find(name);
        return it == source.end() ? TouchPlace{} : it->second;
    }

    void TouchControls::BuildLayout() {
        const float u = static_cast<float>(std::min(width, height));
        const float W = static_cast<float>(width);
        const float H = static_cast<float>(height);
        const float global = state.scale;
        layout = {};
        auto set = [&](Button b, float cx, float cy, float r, Action action, Kind kind, const char* label,
                       bool system) {
            const TouchPlace p = Place(kButtonNames[b]);
            Layout& l = layout[b];
            l.cx = p.x >= 0.0f ? p.x * W : cx;
            l.cy = p.y >= 0.0f ? p.y * H : cy;
            l.r = r * global * std::max(0.4f, p.scale);
            l.action = action;
            l.kind = kind;
            l.label = label;
            l.system = system;
            l.visible = true;
        };
        set(Jump, W - 0.14f * u, H - 0.17f * u, 0.095f * u, Action::Jump, Kind::Hold, "^", false);
        set(Sneak, W - 0.36f * u, H - 0.13f * u, 0.07f * u, Action::Sneak, Kind::SneakKey, "v", false);
        set(Use, W - 0.14f * u, H - 0.42f * u, 0.08f * u, Action::Use, Kind::Hold, "+", false);
        set(Attack, W - 0.36f * u, H - 0.36f * u, 0.085f * u, Action::Attack, Kind::Hold, "X", false);
        set(Sprint, 0.20f * u, H - 0.52f * u, 0.058f * u, Action::Sprint, Kind::SprintToggle, ">>", false);

        const float r = 0.043f * u, pitch = 0.112f * u;
        set(Pause, W * 0.5f - pitch * 0.5f, r + 0.02f * u, r, Action::Pause, Kind::Tap, "II", true);
        set(Chat, W * 0.5f + pitch * 0.5f, r + 0.02f * u, r, Action::Chat, Kind::Tap, "T", true);

        const float scale =
            state.hudScale > 0 ? static_cast<float>(state.hudScale) : std::max(1.0f, std::floor(u / 240.0f));
        hotbarW = 182.0f * scale;
        hotbarH = 22.0f * scale;
        hotbarX = W * 0.5f - hotbarW * 0.5f;
        hotbarY = H - hotbarH;
        const float ir = std::max(0.035f * u, 10.0f * scale);
        set(Inventory, hotbarX + hotbarW + ir * 1.3f, H - hotbarH * 0.5f - 2.0f * scale, ir,
            Action::Inventory, Kind::Tap, "...", true);

        const TouchPlace sp = Place("stick");
        stickR = kStickRadius * u * global * std::max(0.4f, sp.scale);
        stickCX = sp.x >= 0.0f ? sp.x * W : 0.20f * u;
        stickCY = sp.y >= 0.0f ? sp.y * H : H - 0.20f * u;
    }

    int TouchControls::EditorHit(float x, float y) const {
        ToolBox boxes[4];
        ToolBoxes(width, height, boxes);
        for (int i = 0; i < 4; i++)
            if (x >= boxes[i].x && x < boxes[i].x + boxes[i].w && y >= boxes[i].y &&
                y < boxes[i].y + boxes[i].h)
                return i;
        return -1;
    }

    void TouchControls::EditorPress(int which) {
        if (which == 0 || which == 1) {
            if (editSelected < 0)
                return;
            const char* name = editSelected == ButtonCount ? "stick" : kButtonNames[editSelected];
            TouchPlace p = editLayout.contains(name) ? editLayout[name] : TouchPlace{};
            p.scale = std::clamp(p.scale * (which == 1 ? 1.12f : 1.0f / 1.12f), 0.4f, 2.5f);
            if (p.x < 0.0f && editSelected != ButtonCount) {
                p.x = layout[editSelected].cx / static_cast<float>(width);
                p.y = layout[editSelected].cy / static_cast<float>(height);
            } else if (p.x < 0.0f) {
                p.x = stickCX / static_cast<float>(width);
                p.y = stickCY / static_cast<float>(height);
            }
            editLayout[name] = p;
            BuildLayout();
        } else if (which == 2) {
            editLayout.clear();
            editSelected = -1;
            BuildLayout();
        } else if (which == 3) {
            editDone = true;
            editing = false;
            cancelPending = true;
        }
    }

    bool TouchControls::InHotbar(float x, float y, int& slot) const {
        if (hotbarW <= 0)
            return false;
        const float margin = hotbarH * 0.3f;
        if (x < hotbarX || x >= hotbarX + hotbarW || y < hotbarY - margin)
            return false;
        const float unit = hotbarW / 182.0f * 20.0f;
        slot = std::clamp(static_cast<int>((x - hotbarX - hotbarW / 182.0f) / unit), 0, 8);
        return true;
    }

    int TouchControls::HitButton(float x, float y) const {
        int best = -1;
        float bestD = 1e30f;
        for (int i = 0; i < ButtonCount; i++) {
            const Layout& l = layout[i];
            if (!l.visible)
                continue;
            const float dx = x - l.cx, dy = y - l.cy;
            const float r = l.r * 1.2f;
            const float d = dx * dx + dy * dy;
            if (d <= r * r && d < bestD) {
                best = i;
                bestD = d;
            }
        }
        return best;
    }

    TouchControls::Finger* TouchControls::Find(int64_t id) {
        for (Finger& f : fingers)
            if (f.id == id)
                return &f;
        return nullptr;
    }

    void TouchControls::Erase(int64_t id) {
        fingers.erase(
            std::remove_if(fingers.begin(), fingers.end(), [&](const Finger& f) { return f.id == id; }),
            fingers.end());
    }

    Binding TouchControls::PressAction(Input& input, Action action) {
        const Binding b = state.bindings[static_cast<size_t>(action)];
        if (b.device == Binding::Device::Keyboard)
            input.OnKeyDown(b.code);
        else if (b.device == Binding::Device::Mouse)
            input.OnMouseDown(b.code);
        return b;
    }

    void TouchControls::ReleaseBinding(Input& input, const Binding& b) {
        if (b.device == Binding::Device::Keyboard)
            input.OnKeyUp(b.code);
        else if (b.device == Binding::Device::Mouse)
            input.OnMouseUp(b.code);
    }

    void TouchControls::Tap(Input& input, Action action) {
        Binding b;
        if (action == Action::DebugScreen) {
            b = Binding::KeyboardKey(Key::F3);
            input.OnKeyDown(Key::F3);
        } else {
            b = PressAction(input, action);
        }
        if (b.device == Binding::Device::None)
            return;
        pendingReleases.push_back({b, Clock::now() + std::chrono::milliseconds(120)});
    }

    void TouchControls::FlushReleases(Input& input, bool all) {
        const auto now = Clock::now();
        for (size_t i = 0; i < pendingReleases.size();) {
            if (all || now >= pendingReleases[i].at) {
                ReleaseBinding(input, pendingReleases[i].binding);
                pendingReleases.erase(pendingReleases.begin() + static_cast<long>(i));
            } else {
                i++;
            }
        }
    }

    void TouchControls::PressButton(Input& input, int index) {
        const Layout& l = layout[index];
        switch (l.kind) {
        case Kind::SneakKey: {
            const bool momentary = state.flying || state.inWater || state.riding;
            if (momentary) {
                if (!sneakLatched)
                    pressedBinding[index] = PressAction(input, l.action);
                sneakMomentary = true;
                down[index] = true;
            } else {
                sneakLatched = !sneakLatched;
                if (sneakLatched)
                    pressedBinding[index] = PressAction(input, l.action);
                else
                    ReleaseBinding(input, pressedBinding[index]);
                down[index] = sneakLatched;
            }
            return;
        }
        case Kind::SprintToggle:
            sprintLatched = !sprintLatched;
            down[index] = sprintLatched;
            ApplyStick(input);
            return;
        case Kind::Tap:
            down[index] = true;
            Tap(input, l.action);
            return;
        case Kind::Hold:
            down[index] = true;
            pressedBinding[index] = PressAction(input, l.action);
            return;
        }
    }

    void TouchControls::ReleaseButton(Input& input, int index) {
        const Layout& l = layout[index];
        switch (l.kind) {
        case Kind::SneakKey:
            if (sneakMomentary) {
                sneakMomentary = false;
                if (!sneakLatched)
                    ReleaseBinding(input, pressedBinding[index]);
                down[index] = sneakLatched;
            }
            return;
        case Kind::SprintToggle:
            return;
        case Kind::Tap:
            down[index] = false;
            return;
        case Kind::Hold:
            down[index] = false;
            ReleaseBinding(input, pressedBinding[index]);
            return;
        }
    }

    void TouchControls::ApplyStick(Input& input) {
        bool want[5] = {false, false, false, false, false};
        if (stickActive) {
            const float radius = std::max(1.0f, stickR);
            const float nx = knobX / radius, ny = knobY / radius;
            const float len = std::sqrt(nx * nx + ny * ny);
            if (len > kDeadZone) {
                want[KeyForward] = ny < -0.38f;
                want[KeyBack] = ny > 0.38f;
                want[KeyLeft] = nx < -0.38f;
                want[KeyRight] = nx > 0.38f;
                want[KeySprint] = len >= kSprintZone && ny < -0.5f;
            }
        }
        want[KeySprint] = want[KeySprint] || sprintLatched;
        static constexpr Action kActions[5] = {Action::Forward, Action::Back, Action::Left, Action::Right,
                                               Action::Sprint};
        for (int i = 0; i < 5; i++) {
            if (want[i] == stickHeld[i])
                continue;
            stickHeld[i] = want[i];
            if (want[i])
                stickBinding[i] = PressAction(input, kActions[i]);
            else
                ReleaseBinding(input, stickBinding[i]);
        }
    }

    void TouchControls::ReleaseAll(Input& input) {
        FlushReleases(input, true);
        stickActive = false;
        knobX = knobY = 0.0f;
        sprintLatched = false;
        down[Sprint] = false;
        ApplyStick(input);
        for (int i = 0; i < ButtonCount; i++)
            if (down[i] && layout[i].kind == Kind::Hold)
                ReleaseButton(input, i);
        for (Finger& f : fingers)
            if (f.role == Role::Pointer && !f.scrolling)
                input.OnMouseUp(Mouse::Left);
        if (sneakLatched || sneakMomentary) {
            ReleaseBinding(input, pressedBinding[Sneak]);
            sneakLatched = sneakMomentary = false;
            down[Sneak] = false;
        }
        down.fill(false);
        fingers.clear();
    }

    void TouchControls::Cancel(Input& input) {
        std::lock_guard lock(mutex);
        ReleaseAll(input);
    }

    void TouchControls::FingerDown(Input& input, int64_t id, float x, float y) {
        std::lock_guard lock(mutex);
        if (cancelPending) {
            ReleaseAll(input);
            cancelPending = false;
        }
        Finger f;
        f.id = id;
        f.startX = f.lastX = x;
        f.startY = f.lastY = y;
        if (editing) {
            const int tool = EditorHit(x, y);
            if (tool >= 0) {
                f.role = Role::EditorButton;
                f.button = tool;
                EditorPress(tool);
            } else {
                const int hit = HitButton(x, y);
                const float dx = x - stickCX, dy = y - stickCY;
                if (hit >= 0) {
                    editSelected = hit;
                    f.role = Role::Drag;
                    f.grabX = layout[hit].cx - x;
                    f.grabY = layout[hit].cy - y;
                } else if (dx * dx + dy * dy <= stickR * stickR * 2.25f) {
                    editSelected = ButtonCount;
                    f.role = Role::Drag;
                    f.grabX = stickCX - x;
                    f.grabY = stickCY - y;
                } else {
                    editSelected = -1;
                }
            }
            fingers.push_back(f);
            return;
        }
        if (!Gameplay(input)) {
            const bool havePointer = std::any_of(fingers.begin(), fingers.end(),
                                                 [](const Finger& o) { return o.role == Role::Pointer; });
            if (!havePointer) {
                f.role = Role::Pointer;
                input.OnMouseMove(x, y);
                input.OnMouseDown(Mouse::Left);
            } else {
                f.role = Role::Scroll;
            }
            fingers.push_back(f);
            return;
        }
        int slot = 0;
        const int hit = HitButton(x, y);
        if (hit >= 0) {
            f.role = Role::Button;
            f.button = hit;
            PressButton(input, hit);
        } else if (InHotbar(x, y, slot)) {
            Tap(input, static_cast<Action>(static_cast<int>(Action::Hotbar1) + slot));
            f.role = Role::None;
        } else if (!stickActive &&
                   (x - stickCX) * (x - stickCX) + (y - stickCY) * (y - stickCY) <= stickR * stickR * 2.56f) {
            f.role = Role::Stick;
            stickActive = true;
            stickX = stickCX;
            stickY = stickCY;
            knobX = x - stickCX;
            knobY = y - stickCY;
            const float len = std::sqrt(knobX * knobX + knobY * knobY);
            if (len > stickR) {
                knobX *= stickR / len;
                knobY *= stickR / len;
            }
            ApplyStick(input);
        } else {
            f.role = Role::Look;
        }
        fingers.push_back(f);
    }

    void TouchControls::Scroll(Input& input, float dy) {
        scrollAccum += dy;
        while (scrollAccum >= kScrollStep) {
            input.OnMouseWheel(1.0f);
            scrollAccum -= kScrollStep;
        }
        while (scrollAccum <= -kScrollStep) {
            input.OnMouseWheel(-1.0f);
            scrollAccum += kScrollStep;
        }
    }

    void TouchControls::FingerMove(Input& input, int64_t id, float x, float y, float dx, float dy) {
        std::lock_guard lock(mutex);
        if (cancelPending) {
            ReleaseAll(input);
            cancelPending = false;
        }
        Finger* f = Find(id);
        if (!f)
            return;
        const float fromStartX = x - f->startX, fromStartY = y - f->startY;
        if (fromStartX * fromStartX + fromStartY * fromStartY > kTapSlop * kTapSlop)
            f->moved = true;
        f->lastX = x;
        f->lastY = y;
        switch (f->role) {
        case Role::Pointer:
            if (!f->scrolling && f->moved && std::fabs(fromStartY) > std::fabs(fromStartX) * 1.2f) {
                f->scrolling = true;
                f->pointerParked = true;
                f->restoreAt = Clock::now() + std::chrono::milliseconds(120);
                scrollAccum = 0.0f;
                input.OnMouseMove(-10000.0f, -10000.0f);
                input.OnMouseUp(Mouse::Left);
            }
            if (f->scrolling) {
                Scroll(input, dy);
                if (f->pointerParked && Clock::now() >= f->restoreAt) {
                    f->pointerParked = false;
                    input.OnMouseMove(x, y);
                }
            } else {
                input.OnMouseMove(x, y);
            }
            break;
        case Role::Scroll:
            Scroll(input, dy);
            break;
        case Role::Stick: {
            const float radius = std::max(1.0f, stickR);
            float ox = x - stickCX, oy = y - stickCY;
            const float len = std::sqrt(ox * ox + oy * oy);
            if (len > radius) {
                ox *= radius / len;
                oy *= radius / len;
            }
            knobX = ox;
            knobY = oy;
            ApplyStick(input);
            break;
        }
        case Role::Button:
            if (f->button == Attack || f->button == Use || f->button == Jump)
                input.OnMouseDelta(dx * kLookScale * state.sensitivity, dy * kLookScale * state.sensitivity);
            break;
        case Role::Look:
            input.OnMouseDelta(dx * kLookScale * state.sensitivity, dy * kLookScale * state.sensitivity);
            break;
        case Role::Drag: {
            if (editSelected < 0)
                break;
            const char* name = editSelected == ButtonCount ? "stick" : kButtonNames[editSelected];
            TouchPlace p = editLayout.contains(name) ? editLayout[name] : TouchPlace{};
            p.x = std::clamp((x + f->grabX) / static_cast<float>(width), 0.02f, 0.98f);
            p.y = std::clamp((y + f->grabY) / static_cast<float>(height), 0.03f, 0.97f);
            editLayout[name] = p;
            BuildLayout();
            break;
        }
        default:
            break;
        }
    }

    void TouchControls::FingerUp(Input& input, int64_t id, float x, float y) {
        std::lock_guard lock(mutex);
        if (cancelPending) {
            ReleaseAll(input);
            cancelPending = false;
        }
        Finger* f = Find(id);
        if (!f)
            return;
        switch (f->role) {
        case Role::Pointer:
            input.OnMouseMove(x, y);
            if (!f->scrolling)
                input.OnMouseUp(Mouse::Left);
            break;
        case Role::Stick:
            stickActive = false;
            knobX = knobY = 0.0f;
            sprintLatched = false;
            down[Sprint] = false;
            ApplyStick(input);
            break;
        case Role::Button:
            ReleaseButton(input, f->button);
            break;
        default:
            break;
        }
        Erase(id);
    }

    void TouchControls::Update(Input& input) {
        std::lock_guard lock(mutex);
        if (cancelPending) {
            ReleaseAll(input);
            cancelPending = false;
        }
        FlushReleases(input, false);
        if (editing)
            return;
        const bool gameplay = Gameplay(input);
        if (wasGameplay != gameplay) {
            wasGameplay = gameplay;
            ReleaseAll(input);
            return;
        }
        if (!gameplay) {
            const auto t = Clock::now();
            for (Finger& f : fingers)
                if (f.role == Role::Pointer && f.pointerParked && t >= f.restoreAt) {
                    f.pointerParked = false;
                    input.OnMouseMove(f.lastX, f.lastY);
                }
        }
    }

}
