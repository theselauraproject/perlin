#pragma once
#include "client/screen/Screen.hpp"

#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace perlin::screen {

    class Manager {
    public:
        Manager() = default;
        Manager(const Manager&) = delete;
        Manager& operator=(const Manager&) = delete;

        template <class T, class... Args> T& Emplace(Args&&... args) {
            auto screen = std::make_unique<T>(std::forward<Args>(args)...);
            T& ref = *screen;
            Adopt(std::move(screen));
            return ref;
        }

        Screen& Adopt(std::unique_ptr<Screen> screen);

        bool Open(Screen& screen);
        bool Close(Screen& screen);
        void CloseAll(std::initializer_list<Screen*> screens);

        bool IsOpen(Id id) const;
        bool AnyOpen(std::initializer_list<Id> ids) const;
        bool AnyCoveringOpen() const;
        Screen* Find(Id id) const;

        void Update(Frame& frame);
        std::optional<Id> FrameOwner() const { return owner; }
        bool TookFrame(Id id) const { return owner == id; }

    private:
        std::vector<std::unique_ptr<Screen>> owned;
        std::vector<Screen*> byId = std::vector<Screen*>(kScreenCount, nullptr);
        std::optional<Id> owner;
    };

}
