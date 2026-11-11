#pragma once
#include "client/input/KeyMapping.hpp"
#include "client/input/TouchState.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace perlin {
    class Input;
    class RenderContext;

    class TouchControls {
    public:
        enum Button {
            Jump,
            Sneak,
            Attack,
            Use,
            Sprint,
            Pause,
            Chat,
            Perspective,
            Debug,
            Players,
            HideHud,
            Inventory,
            ButtonCount
        };

        void SetViewport(int width, int height);
        void SetState(const TouchState& state);
        void FingerDown(Input& input, int64_t id, float x, float y);
        void FingerMove(Input& input, int64_t id, float x, float y, float dx, float dy);
        void FingerUp(Input& input, int64_t id, float x, float y);
        void Cancel(Input& input);
        void Update(Input& input);
        void Draw(RenderContext& renderer, bool visible);
        bool Gameplay(const Input& input) const;
        bool TakeEditResult(std::map<std::string, TouchPlace>& out);
        bool Editing() const { return editing; }

    private:
        enum class Role { None, Stick, Button, Look, Pointer, Scroll, Drag, EditorButton };

        struct Finger {
            int64_t id = 0;
            Role role = Role::None;
            int button = -1;
            float startX = 0, startY = 0, lastX = 0, lastY = 0;
            bool moved = false;
            bool scrolling = false;
            std::chrono::steady_clock::time_point restoreAt;
            bool pointerParked = false;
            float grabX = 0, grabY = 0;
        };

        enum class Kind { Hold, Tap, SneakKey, SprintToggle };

        struct Layout {
            float cx = 0, cy = 0, r = 0;
            Action action = Action::Count;
            Kind kind = Kind::Hold;
            const char* label = "";
            bool system = false;
            bool visible = false;
        };

        struct Sprite {
            uint32_t id = 0;
            int w = 0, h = 0;
            int nextTry = 0;
        };

        void BuildLayout();
        TouchPlace Place(const char* name) const;
        float BaseScale(const char* name) const;
        int EditorHit(float x, float y) const;
        void EditorPress(int which);
        void FlushReleases(Input& input, bool all);
        int HitButton(float x, float y) const;
        void PressButton(Input& input, int index);
        void ReleaseButton(Input& input, int index);
        Binding PressAction(Input& input, Action action);
        static void ReleaseBinding(Input& input, const Binding& binding);
        void Tap(Input& input, Action action);
        void ApplyStick(Input& input);
        void ReleaseAll(Input& input);
        bool InHotbar(float x, float y, int& slot) const;
        Finger* Find(int64_t id);
        void Erase(int64_t id);
        void Scroll(Input& input, float dy);

        std::mutex mutex;
        int width = 0, height = 0;
        TouchState state;
        std::array<Layout, ButtonCount> layout{};
        std::array<bool, ButtonCount> down{};
        std::vector<Finger> fingers;
        float hotbarX = 0, hotbarY = 0, hotbarW = 0, hotbarH = 0;

        bool stickActive = false;
        float stickX = 0, stickY = 0, knobX = 0, knobY = 0;
        bool stickHeld[5] = {false, false, false, false, false};
        Binding stickBinding[5];
        bool sneakLatched = false, sneakMomentary = false;
        bool sprintLatched = false;
        bool wasGameplay = false;
        bool editing = false;
        bool editDone = false;
        bool cancelPending = false;
        int editSelected = -1;
        std::map<std::string, TouchPlace> editLayout;
        float stickCX = 0, stickCY = 0, stickR = 0;
        struct PendingRelease {
            Binding binding;
            std::chrono::steady_clock::time_point at;
        };
        std::vector<PendingRelease> pendingReleases;
        float scrollAccum = 0.0f;

        std::array<Binding, ButtonCount> pressedBinding{};

        std::vector<Sprite> sprites;
        uint32_t discTexture = 0, fontTexture = 0;
        uint32_t fontVersion = 0;
    };
}
