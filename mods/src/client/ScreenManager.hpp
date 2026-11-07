#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace perlin {
    class BedrockSession;
    namespace ui {
        class Hud;
    }

    // what every screen gets each frame
    struct ScreenFrame {
        ui::Hud& hud;
        BedrockSession* live = nullptr;
        int fbW = 0, fbH = 0;
    };

    // one ui screen: the title, the pause menu, the inventory, a server form...
    class Screen {
    public:
        virtual ~Screen() = default;
        virtual std::string_view Name() const = 0;
        // runs once per frame in registration order; true takes the frame: screens after it do not run
        virtual bool Update(ScreenFrame& frame) = 0;
        // true while the screen is on show
        virtual bool IsOpen() const = 0;
        // an open screen with this set hides the crosshair and the touch controls
        virtual bool CoversGame() const { return true; }
    };

    // the screens in the order they get the frame. A new screen is a Screen subclass (or two lambdas) added
    // once at startup; nothing else in the frame loop changes.
    class ScreenManager {
    public:
        struct Callbacks {
            std::string name;
            std::function<bool(ScreenFrame&)> update;
            std::function<bool()> isOpen;
            bool coversGame = true;
        };

        Screen& Add(std::unique_ptr<Screen> screen) {
            screens.push_back(std::move(screen));
            return *screens.back();
        }

        // a screen whose state lives elsewhere, as an update and an is-open callback
        Screen& Add(Callbacks callbacks) {
            return Add(std::make_unique<CallbackScreen>(std::move(callbacks)));
        }

        void Update(ScreenFrame& frame) {
            taker.clear();
            for (const auto& s : screens)
                if (s->Update(frame)) {
                    taker = s->Name();
                    break;
                }
        }

        // the screen that took the last frame, empty if none did
        std::string_view TookFrame() const { return taker; }
        bool TookFrame(std::string_view name) const { return taker == name; }

        bool IsOpen(std::string_view name) const {
            const Screen* s = Find(name);
            return s && s->IsOpen();
        }
        bool AnyCoveringOpen() const {
            return std::ranges::any_of(screens, [](const auto& s) { return s->CoversGame() && s->IsOpen(); });
        }

        const Screen* Find(std::string_view name) const {
            const auto it =
                std::ranges::find_if(screens, [name](const auto& s) { return s->Name() == name; });
            return it == screens.end() ? nullptr : it->get();
        }

    private:
        class CallbackScreen final : public Screen {
        public:
            explicit CallbackScreen(Callbacks c) : cb(std::move(c)) {}
            std::string_view Name() const override { return cb.name; }
            bool Update(ScreenFrame& frame) override { return cb.update(frame); }
            bool IsOpen() const override { return cb.isOpen ? cb.isOpen() : false; }
            bool CoversGame() const override { return cb.coversGame; }

        private:
            Callbacks cb;
        };

        std::vector<std::unique_ptr<Screen>> screens;
        std::string taker;
    };
}
