#include "client/screen/Frame.hpp"
#include "client/screen/ScreenManager.hpp"

#include <array>
#include <cstdio>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace perlin::ui {
    class Hud {};
}

using namespace perlin::screen;

namespace {
    int failures = 0;
    void Check(bool ok, const char* what, int line) {
        if (!ok) {
            std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
            failures++;
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    class TestScreen : public Screen {
    public:
        TestScreen(Id id, Traits traits, Claim claim, std::vector<int>& journal)
            : Screen(id, traits), claim(claim), journal(journal) {}

        Claim Update(Frame& frame) override {
            journal.push_back(static_cast<int>(GetId()));
            if (onUpdate)
                onUpdate(frame);
            return claim;
        }

        Claim claim;
        std::function<void(Frame&)> onUpdate;

    private:
        std::vector<int>& journal;
    };

    class ExternalOpen final : public Screen {
    public:
        explicit ExternalOpen(Id id) : Screen(id, Traits{false}) {}
        Claim Update(Frame&) override { return Claim::None; }
        bool IsOpen() const override { return external; }
        bool external = false;
    };
}

int main() {
    {
        const std::array<Id, kScreenCount> expected = {Id::Title,   Id::Disconnect, Id::Loading, Id::Replay,
                                                       Id::Credits, Id::Death,      Id::Form,    Id::Inventory,
                                                       Id::Pause,   Id::Bed,        Id::Chat,    Id::Asset};
        CHECK(kUpdateOrder == expected);
        for (std::size_t i = 0; i < kUpdateOrder.size(); i++)
            CHECK(static_cast<std::size_t>(kUpdateOrder[i]) == i);
    }
    std::vector<int> journal;
    perlin::ui::Hud hud;
    Frame frame{hud, nullptr, 640, 480};

    {
        Manager m;
        auto& title = m.Emplace<TestScreen>(Id::Title, Traits{true}, Claim::Frame, journal);
        auto& pause = m.Emplace<TestScreen>(Id::Pause, Traits{true}, Claim::None, journal);
        auto& chat = m.Emplace<TestScreen>(Id::Chat, Traits{false}, Claim::None, journal);

        CHECK(!m.IsOpen(Id::Pause));
        CHECK(!m.AnyCoveringOpen());
        CHECK(m.Open(pause));
        CHECK(!m.Open(pause));
        CHECK(m.IsOpen(Id::Pause));
        CHECK(m.AnyCoveringOpen());
        CHECK(m.Open(chat));
        CHECK(m.AnyOpen({Id::Chat, Id::Death}));
        CHECK(!m.AnyOpen({Id::Death, Id::Form}));
        CHECK(m.Close(pause));
        CHECK(!m.AnyCoveringOpen());

        m.Update(frame);
        CHECK((journal == std::vector<int>{static_cast<int>(Id::Title)}));
        CHECK(m.TookFrame(Id::Title));
        CHECK(m.FrameOwner() == Id::Title);
        journal.clear();

        title.claim = Claim::None;
        m.Update(frame);
        CHECK((journal == std::vector<int>{static_cast<int>(Id::Title), static_cast<int>(Id::Pause), static_cast<int>(Id::Chat)}));
        CHECK(!m.FrameOwner().has_value());
        journal.clear();

        m.CloseAll({&title, &pause, &chat});
        CHECK(!m.Close(chat));
        CHECK(m.Find(Id::Form) == nullptr);
        CHECK(m.Find(Id::Chat) == &chat);
    }

    {
        Manager m;
        auto& a = m.Emplace<TestScreen>(Id::Death, Traits{}, Claim::None, journal);
        auto& b = m.Emplace<TestScreen>(Id::Form, Traits{}, Claim::None, journal);
        auto& c = m.Emplace<TestScreen>(Id::Bed, Traits{}, Claim::None, journal);
        m.Open(a);
        a.onUpdate = [&](Frame&) {
            m.Open(c);
            m.Close(a);
            m.Open(b);
        };
        journal.clear();
        m.Update(frame);
        CHECK((journal == std::vector<int>{static_cast<int>(Id::Death), static_cast<int>(Id::Form), static_cast<int>(Id::Bed)}));
        CHECK(b.IsOpen() && c.IsOpen() && !a.IsOpen());
    }

    {
        Manager m;
        auto& first = m.Emplace<TestScreen>(Id::Death, Traits{}, Claim::Frame, journal);
        m.Emplace<TestScreen>(Id::Form, Traits{}, Claim::None, journal);
        journal.clear();
        m.Update(frame);
        CHECK((journal == std::vector<int>{static_cast<int>(Id::Death)}));
        CHECK(m.TookFrame(Id::Death));
        first.claim = Claim::None;
        m.Update(frame);
        CHECK((journal == std::vector<int>{static_cast<int>(Id::Death), static_cast<int>(Id::Death), static_cast<int>(Id::Form)}));
        CHECK(!m.TookFrame(Id::Death));
    }

    {
        const Id order[] = {Id::Title, Id::Disconnect, Id::Loading, Id::Replay, Id::Credits, Id::Death,
                            Id::Form,  Id::Inventory,  Id::Pause,   Id::Bed,    Id::Chat};
        for (std::size_t i = 0; i + 1 < std::size(order); ++i)
            CHECK(static_cast<int>(order[i]) + 1 == static_cast<int>(order[i + 1]));
        CHECK(static_cast<int>(Id::Title) == 0);
        CHECK(static_cast<std::size_t>(Id::Asset) + 1 == kScreenCount);
    }

    {
        Manager m;
        m.Emplace<TestScreen>(Id::Chat, Traits{false}, Claim::None, journal);
        m.Emplace<TestScreen>(Id::Pause, Traits{true}, Claim::None, journal);
        m.Emplace<TestScreen>(Id::Title, Traits{true}, Claim::None, journal);
        journal.clear();
        m.Update(frame);
        CHECK((journal == std::vector<int>{static_cast<int>(Id::Title), static_cast<int>(Id::Pause),
                                           static_cast<int>(Id::Chat)}));
    }

    {
        Manager m;
        auto& external = m.Emplace<ExternalOpen>(Id::Replay);
        CHECK(!m.IsOpen(Id::Replay));
        external.external = true;
        CHECK(m.IsOpen(Id::Replay));
        CHECK(!m.AnyCoveringOpen());
        bool threw = false;
        try {
            m.Emplace<ExternalOpen>(Id::Replay);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(threw);
        threw = false;
        try {
            m.Adopt(nullptr);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(threw);
    }

    {
        std::mt19937 rng(5);
        for (int round = 0; round < 300; ++round) {
            Manager m;
            std::vector<TestScreen*> screens;
            std::vector<bool> model;
            for (int i = 0; i < 6; ++i) {
                screens.push_back(&m.Emplace<TestScreen>(static_cast<Id>(i), Traits{(i & 1) != 0}, Claim::None, journal));
                model.push_back(false);
            }
            for (int step = 0; step < 40; ++step) {
                const int i = static_cast<int>(rng() % 6);
                if (rng() & 1) {
                    CHECK(m.Open(*screens[i]) == !model[i]);
                    model[i] = true;
                } else {
                    CHECK(m.Close(*screens[i]) == model[i]);
                    model[i] = false;
                }
                bool covering = false;
                for (int k = 0; k < 6; ++k)
                    covering = covering || (model[k] && (k & 1));
                CHECK(m.AnyCoveringOpen() == covering);
                for (int k = 0; k < 6; ++k)
                    CHECK(m.IsOpen(static_cast<Id>(k)) == model[k]);
            }
        }
    }

    if (failures) {
        std::fprintf(stderr, "screen_manager_selftest FAILED (%d)\n", failures);
        return 1;
    }
    std::puts("screen_manager_selftest passed");
    return 0;
}
