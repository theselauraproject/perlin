#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace perlin::screen {

    enum class Id : uint8_t {
        Title,
        Disconnect,
        Loading,
        Replay,
        Credits,
        Death,
        Form,
        Inventory,
        Pause,
        Bed,
        Chat,
        Asset,
    };

    inline constexpr std::size_t kScreenCount = static_cast<std::size_t>(Id::Asset) + 1;

    inline constexpr std::array<Id, kScreenCount> kUpdateOrder = {
        Id::Title,  Id::Disconnect, Id::Loading, Id::Replay, Id::Credits, Id::Death,
        Id::Form,   Id::Inventory,  Id::Pause,   Id::Bed,    Id::Chat,    Id::Asset,
    };

    constexpr bool UpdateOrderIsPermutation() {
        std::array<bool, kScreenCount> seen{};
        for (Id id : kUpdateOrder) {
            if (seen[static_cast<std::size_t>(id)])
                return false;
            seen[static_cast<std::size_t>(id)] = true;
        }
        for (bool b : seen)
            if (!b)
                return false;
        return true;
    }
    static_assert(UpdateOrderIsPermutation(), "kUpdateOrder must list every screen::Id exactly once");

    struct Frame;

    enum class Claim : uint8_t { None, Frame };

    struct Traits {
        bool coversGame = true;
    };

    class Manager;

    class Screen {
    public:
        explicit Screen(Id id, Traits traits = {}) : id(id), traits(traits) {}
        virtual ~Screen() = default;
        Screen(const Screen&) = delete;
        Screen& operator=(const Screen&) = delete;

        Id GetId() const { return id; }
        const Traits& GetTraits() const { return traits; }

        virtual bool IsOpen() const { return open; }
        virtual Claim Update(Frame& frame) = 0;

    private:
        friend class Manager;
        Id id;
        Traits traits;
        bool open = false;
    };

}
