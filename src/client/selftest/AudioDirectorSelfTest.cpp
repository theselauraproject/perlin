#include "client/features/audio/AudioDirector.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"
#include "network/Stream.hpp"

#include <thread>

namespace {
    using namespace perlin;

    template <class P> void Inject(BedrockSession& s, const P& packet) {
        s.InjectIncoming(P::kId, net::Encode(packet));
    }

    struct Rig {
        Options options;
        audio::AudioEngine engine;
        audio::SoundSystem sounds{engine};
        Camera camera;
        LocalPlayer player;
        ui::HudState hudState;
        std::vector<BedrockSession::SoundRequest> noted;
        int vanillaCalls = 0;
        std::string root;
        audio::AudioDirector director{audio::AudioDirector::Deps{
            options, sounds, engine, camera, player, hudState, selftest::G().blockPalette,
            [this](const BedrockSession::SoundRequest& r) { noted.push_back(r); },
            [this] {
                vanillaCalls++;
                return root;
            },
            [] { return std::vector<std::string>{}; }, [] { return 0; }}};
    };
}

namespace perlin::selftest {
    int AudioDirectorTest() {
        Checker t("audio_director_test");

        {
            Rig r;
            r.hudState.subtitles.push_back({"stale", 0, 1.0f});
            r.options.sound.subtitles = false;
            r.director.Update(nullptr, 1.0, 0.016f);
            t.Equal(r.vanillaCalls, 1, "update asks for the vanilla root each frame");
            t.Check(r.hudState.subtitles.empty(), "disabled subtitles clear stale lines");
            r.director.Update(nullptr, 1.02, 0.016f);
            t.Equal(r.vanillaCalls, 2, "root is consulted again on the next frame");
            t.Check(r.noted.empty(), "no session means no sound requests");
        }
        {
            Rig r;
            BedrockSession session;
            proto::PlaySoundPacket play;
            play.name = "random.click";
            play.coordinates.x = 80;
            play.coordinates.y = 160;
            play.coordinates.z = 8;
            play.volume = 0.5f;
            play.pitch = 1.5f;
            play.bypassListenerRangeCheck = true;
            Inject(session, play);

            proto::LevelSoundEventPacket level;
            level.soundId = "break";
            level.position = {1.0f, 2.0f, 3.0f};
            level.extraData = 7;
            level.entityType = "minecraft:cow";
            level.isBabyMob = true;
            Inject(session, level);

            proto::StopSoundPacket stop;
            stop.name = "music.game";
            Inject(session, stop);
            proto::StopSoundPacket stopAll;
            stopAll.stopAll = true;
            Inject(session, stopAll);

            r.director.Update(&session, 2.0, 0.016f);
            t.Equal(r.noted.size(), size_t{4}, "every queued request is dispatched exactly once");
            if (r.noted.size() == 4) {
                const auto& named = r.noted[0];
                t.Check(named.kind == BedrockSession::SoundRequest::Kind::Named && named.name == "random.click",
                        "named sound keeps its name");
                t.Check(named.x == 10.0f && named.y == 20.0f && named.z == 1.0f,
                        "named sound coordinates are converted from eighths");
                t.Check(named.volume == 0.5f && named.pitch == 1.5f && named.bypassListenerRangeCheck && !named.global,
                        "range bypass is preserved independently of spatial playback");
                const auto& lvl = r.noted[1];
                t.Check(lvl.kind == BedrockSession::SoundRequest::Kind::Level && lvl.name == "break" &&
                            lvl.extra == 7 && lvl.entityType == "minecraft:cow" && lvl.baby,
                        "level sound keeps event details");
                t.Check(r.noted[2].kind == BedrockSession::SoundRequest::Kind::Stop && !r.noted[2].stopAll &&
                            r.noted[2].name == "music.game",
                        "named stop request preserved");
                t.Check(r.noted[3].kind == BedrockSession::SoundRequest::Kind::Stop && r.noted[3].stopAll,
                        "stop-all request preserved");
            }
            r.director.Update(&session, 2.1, 0.016f);
            t.Equal(r.noted.size(), size_t{4}, "dispatched requests are not replayed");
        }
        {
            Rig r;
            BedrockSession session;
            r.director.NoteLocalPlace(10, 64, 10, audio::AudioDirector::Clock::now());
            proto::LevelSoundEventPacket echo;
            echo.soundId = "place";
            echo.position = {10.5f, 64.5f, 10.5f};
            Inject(session, echo);
            proto::LevelSoundEventPacket far;
            far.soundId = "place";
            far.position = {50.0f, 64.0f, 50.0f};
            Inject(session, far);
            r.director.Update(&session, 3.0, 0.016f);
            t.Equal(r.noted.size(), size_t{2}, "both placements are reported to the sound note hook");
        }
        {
            Rig r;
            r.options.sound.subtitles = true;
            r.director.Update(nullptr, 4.0, 0.016f);
            t.Check(r.hudState.subtitles.empty(), "enabled subtitles with no sounds show nothing");
            r.options.sound.subtitles = false;
            r.director.Update(nullptr, 4.1, 0.016f);
            t.Check(r.hudState.subtitles.empty(), "turning subtitles off keeps the list empty");
        }
        return t.Finish();
    }
}
