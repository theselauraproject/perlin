#include "client/features/worldview/GameTick.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/systems/renderer/gpu/ModGpuHub.hpp"
#include "network/Stream.hpp"

#include <cstdlib>

#include <algorithm>

namespace {
    using namespace perlin;

    template <class P> void Inject(BedrockSession& s, const P& packet) {
        s.InjectIncoming(P::kId, net::Encode(packet));
    }

    class FakeWindow : public ModHost::Window {
    public:
        bool Available() const override { return false; }
        void Quit() override {}
        void SetClipboard(const std::string&) override {}
        std::string GetClipboard() override { return {}; }
        void OpenUrl(const std::string&) override {}
        void SetTitle(const std::string&) override {}
        void ReleaseCursor() override {}
        bool Focused() override { return true; }
        bool Minimized() override { return false; }
    };

    struct Rig {
        Options options;
        FakeWindow window;
        mods::UiRequest uiRequest;
        Camera camera;
        ui::HudState hudState;
        std::unique_ptr<LevelRenderer> levelRenderer;
        HandRenderer handRenderer;
        Input input;
        InputManager inputManager{input, options};
        entity::EntityTracker previewEntities;
        LocalPlayer player;
        ui::ChatLog chat;
        entity::EntityRenderer entityRenderer;
        audio::AudioEngine audioEngine;
        audio::SoundSystem sounds{audioEngine};
        particle::ParticleSystem particles;
        std::unique_ptr<RenderContext> renderer;
        std::unique_ptr<BedrockSession> session = std::make_unique<BedrockSession>();
        TaskGroup tasks;
        float rainLevel = 0.0f, thunderLevel = 0.0f;
        float previousRain = 0.0f, previousThunder = 0.0f;
        double tickDebt = 0.0;
        BlockLightFlicker blockLight;
        std::unique_ptr<FrameLoop> loop = std::make_unique<FrameLoop>(options);
        int hudTicks = 0;
        ModHost modHost{ModHost::Deps{
            selftest::G().modManager, window, uiRequest, selftest::G().modGpuHub, camera, options, hudState,
            levelRenderer, handRenderer, input, inputManager, previewEntities, player, chat, entityRenderer, sounds,
            audioEngine, particles, renderer, session, tasks, rainLevel, thunderLevel,
            [] { return ModHost::ScreenFlags{}; }, [] { return ModHost::FrameStats{}; }, [] { return uint64_t{0}; },
            [] { return false; }, [] { return false; }, [] {}, [] {}, [] {}, [] {}, [](const std::string&) {},
            [] { return LocalBreakState{}; }}};
        worldview::GameTick tick{worldview::GameTick::Deps{
            modHost, input, inputManager, hudState, handRenderer, sounds, camera, player, blockLight, tickDebt,
            rainLevel, previousRain, thunderLevel, previousThunder, loop, [this] { hudTicks++; }}};

        FrameContext Frame(float dt, bool live) {
            FrameContext f;
            f.dt = dt;
            f.live = live ? session.get() : nullptr;
            return f;
        }
    };
}

namespace perlin::selftest {
    int Worldview() {
        const bool preview = std::getenv("PERLIN_PREVIEW") != nullptr;
        Checker c(preview ? "worldview_preview_test" : "worldview_test");
        if (preview) {
            Rig r;
            FrameContext f = r.Frame(0.05f, false);
            r.tick.TickGame(f);
            c.Equal(r.hudTicks, 1, "preview mode ticks the hud without a session");
            return c.Finish();
        }

        {
            Rig r;
            FrameContext f = r.Frame(0.025f, false);
            r.tick.TickGame(f);
            c.Equal(r.hudTicks, 0, "no hud tick below one tick of debt without a session");
            c.Check(r.tickDebt > 0.49 && r.tickDebt < 0.51, "half a tick of debt is carried");
            r.tick.TickGame(f);
            c.Equal(r.hudTicks, 0, "hud does not tick without a live session or preview");
            c.Check(r.tickDebt < 0.01, "a full tick of debt is consumed");
        }

        {
            Rig r;
            FrameContext f = r.Frame(0.1f, true);
            r.tick.TickGame(f);
            c.Equal(r.hudTicks, 2, "live session ticks the hud once per elapsed game tick");
        }


        {
            Rig r;
            r.loop->waterVisionTime = 5;
            FrameContext f = r.Frame(0.05f, false);
            f.underwater = false;
            r.tick.TickGame(f);
            c.Equal(r.loop->waterVisionTime, 0, "water vision decays by ten and clamps at zero");
            f.underwater = true;
            for (int i = 0; i < 700; i++)
                r.tick.TickGame(f);
            c.Equal(r.loop->waterVisionTime, 600, "water vision climbs to the 600 tick ceiling");
        }

        {
            Rig r;
            proto::LevelEventPacket rain;
            rain.event = proto::LevelEventPacketEvent::StartRain;
            Inject(*r.session, rain);
            c.Check(r.session->RainTarget() == 1.0f, "rain target follows the start rain event");
            FrameContext f = r.Frame(0.05f, true);
            r.tick.TickGame(f);
            c.Check(r.rainLevel > 0.0099f && r.rainLevel < 0.0101f, "rain rises by one hundredth per tick");
            c.Check(r.previousRain == 0.0f, "previous rain keeps the level before the step");
            r.tick.TickGame(f);
            c.Check(r.previousRain > 0.0099f && r.previousRain < 0.0101f, "previous rain trails by one tick");
            for (int i = 0; i < 150; i++)
                r.tick.TickGame(f);
            c.Check(r.rainLevel == 1.0f, "rain stops at the target");

            proto::LevelEventPacket stop;
            stop.event = proto::LevelEventPacketEvent::StopRain;
            Inject(*r.session, stop);
            r.tick.TickGame(f);
            c.Check(r.rainLevel > 0.98f && r.rainLevel < 0.9901f, "rain falls toward a zero target");

            proto::LevelEventPacket thunder;
            thunder.event = proto::LevelEventPacketEvent::StartThunder;
            Inject(*r.session, thunder);
            r.tick.TickGame(f);
            c.Check(r.thunderLevel > 0.0099f && r.thunderLevel < 0.0101f, "thunder steps toward its own target");
        }

        {
            Rig r;
            proto::LevelEventPacket rain;
            rain.event = proto::LevelEventPacketEvent::StartRain;
            Inject(*r.session, rain);
            FrameContext f = r.Frame(0.05f, false);
            r.tick.TickGame(f);
            c.Check(r.rainLevel == 0.0f, "weather is frozen without a live session");
        }

        return c.Finish();
    }
}
