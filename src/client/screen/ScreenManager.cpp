#include "client/screen/ScreenManager.hpp"

#include <algorithm>
#include <stdexcept>

namespace perlin::screen {

    Screen& Manager::Adopt(std::unique_ptr<Screen> screen) {
        if (!screen)
            throw std::invalid_argument("null screen");
        Screen*& slot = byId.at(static_cast<std::size_t>(screen->GetId()));
        if (slot)
            throw std::invalid_argument("screen id registered twice");
        slot = screen.get();
        owned.push_back(std::move(screen));
        return *owned.back();
    }

    bool Manager::Open(Screen& screen) {
        const bool changed = !screen.open;
        screen.open = true;
        return changed;
    }

    bool Manager::Close(Screen& screen) {
        const bool changed = screen.open;
        screen.open = false;
        return changed;
    }

    void Manager::CloseAll(std::initializer_list<Screen*> screens) {
        for (Screen* screen : screens)
            Close(*screen);
    }

    bool Manager::IsOpen(Id id) const {
        const Screen* screen = Find(id);
        return screen && screen->IsOpen();
    }

    bool Manager::AnyOpen(std::initializer_list<Id> ids) const {
        return std::ranges::any_of(ids, [this](Id id) { return IsOpen(id); });
    }

    bool Manager::AnyCoveringOpen() const {
        return std::ranges::any_of(owned, [](const auto& s) { return s->GetTraits().coversGame && s->IsOpen(); });
    }

    Screen* Manager::Find(Id id) const {
        return byId.at(static_cast<std::size_t>(id));
    }

    void Manager::Update(Frame& frame) {
        owner.reset();
        for (const Id id : kUpdateOrder) {
            Screen* screen = byId[static_cast<std::size_t>(id)];
            if (screen && screen->Update(frame) == Claim::Frame) {
                owner = id;
                break;
            }
        }
    }

}
