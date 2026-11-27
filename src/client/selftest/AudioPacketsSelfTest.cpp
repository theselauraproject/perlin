#include "client/features/audio/AudioDirector.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>

namespace perlin::selftest {
    namespace {
    void RunAudioPackets(Checker& t, int protocol) {
        const net::context::ScopedSettings codec({protocol, -1});
        namespace fs = std::filesystem;
        const auto root = fs::temp_directory_path() /
                          ("perlin_audio_packets_" + std::to_string(std::chrono::steady_clock::now()
                                                                         .time_since_epoch().count()));
        struct Cleanup {
            fs::path path;
            ~Cleanup() { std::error_code error; fs::remove_all(path, error); }
        } cleanup{root};
        fs::create_directories(root / "sounds");
        std::ofstream(root / "sounds/sound_definitions.json")
            << R"({"sound_definitions":{"compat_tone":{"category":"block","sounds":["sounds/tone"]}}})";
        std::vector<uint8_t> wav;
        const auto tag = [&](const char* text) { wav.insert(wav.end(), text, text + 4); };
        const auto u16 = [&](uint16_t value) { wav.push_back(value & 255); wav.push_back(value >> 8); };
        const auto u32 = [&](uint32_t value) {
            for (int i = 0; i < 4; ++i)
                wav.push_back(static_cast<uint8_t>(value >> (i * 8)));
        };
        tag("RIFF"); u32(36 + 96000); tag("WAVE"); tag("fmt "); u32(16);
        u16(1); u16(1); u32(48000); u32(96000); u16(2); u16(16); tag("data"); u32(96000);
        for (int frame = 0; frame < 48000; ++frame)
            u16(frame < 24000 ? 16384 : 8192);
        std::ofstream(root / "sounds/tone.wav", std::ios::binary)
            .write(reinterpret_cast<const char*>(wav.data()), static_cast<std::streamsize>(wav.size()));

        Options options;
        options.sound.music = 0;
        audio::AudioEngine engine;
        audio::SoundSystem sounds(engine);
        Camera camera;
        camera.x = camera.y = camera.z = 0;
        LocalPlayer player;
        ui::HudState hud;
        audio::AudioDirector director({options, sounds, engine, camera, player, hud, G().blockPalette,
                                       {}, [&] { return root.string(); }, [] { return std::vector<std::string>{}; },
                                       [] { return 0; }});
        director.Update(nullptr, 0, 0);
        // Keep the real decode/mixer path while removing asynchronous device
        // consumption from this deterministic protocol regression.
        engine.Close();
        BedrockSession session;
        session.BeginReplayMode("sound", 7, protocol);
        size_t replies = 0;
        session.SetSentTap([&](uint32_t, const std::vector<uint8_t>&) { ++replies; });
        const auto inject = [&]<class P>(const P& packet) {
            session.InjectIncoming(P::kId, net::Encode(packet));
            director.Update(&session, 1, 0);
        };
        const auto sample = [&] {
            float output[2]{};
            engine.Mix(output, 1);
            return output[0];
        };
        proto::PlaySoundPacket play;
        play.name = "compat_tone";
        play.volume = play.pitch = 1;
        play.handle = 0; // Zero is a valid server handle, not an absent handle.
        play.loopCount = UINT32_MAX;
        inject(play);
        t.Equal(engine.Active(), size_t{1}, "PlaySound creates a handle-addressable voice");
        t.Check(std::fabs(sample() - 0.5f) < 1e-5f, "playback reaches decoded PCM");
        proto::ClientboundUpdateSoundDataPacket update;
        update.serverSoundHandle = 0;
        update.update.type = proto::SoundDataUpdateType::SetVolume;
        update.update.volume = 0.25f;
        inject(update);
        t.Check(std::fabs(sample() - 0.125f) < 1e-5f, "server volume update reaches the existing voice");
        update.update.type = proto::SoundDataUpdateType::Pause;
        inject(update);
        t.Equal(sample(), 0.0f, "script pause silences the selected server handle");
        update.update.type = proto::SoundDataUpdateType::SeekTo;
        update.update.seconds = 0.75f;
        inject(update);
        update.update.type = proto::SoundDataUpdateType::Resume;
        inject(update);
        t.Check(std::fabs(sample() - 0.0625f) < 1e-5f, "seek during pause is retained on resume");
        update.update.type = proto::SoundDataUpdateType::SetPitch;
        update.update.pitch = 0;
        inject(update);
        t.Check(std::fabs(sample() - 0.0625f) < 1e-5f, "pitch update keeps the same handle");
        update.update.type = proto::SoundDataUpdateType::Fade;
        update.update.targetVolume = 0;
        update.update.duration = 0;
        inject(update);
        t.Equal(sample(), 0.0f, "zero-duration fade updates the selected voice");
        update.update.type = proto::SoundDataUpdateType::SetVolume;
        update.update.volume = 1;
        update.serverSoundHandle = 999;
        inject(update);
        t.Equal(sample(), 0.0f, "an unknown server handle cannot address a local voice");
        update.serverSoundHandle = 0;
        inject(update);
        t.Check(std::fabs(sample() - 0.25f) < 1e-5f, "a silent server voice can be made audible again");
        update.update.type = proto::SoundDataUpdateType::Stop;
        inject(update);
        float tail[2048]{};
        engine.Mix(tail, 1024);
        t.Equal(engine.Active(), size_t{0}, "stop removes the mapped voice");
        t.Equal(replies, size_t{0}, "sound control dispatch produces no protocol reply");
        session.SetSentTap({});
        play.handle = 11;
        play.playbackPositionSeconds = 0.75f;
        inject(play);
        t.Check(std::fabs(sample() - 0.25f) < 1e-5f, "initial playback position survives PlaySound dispatch");
        director.Update(nullptr, 2, 0);
        float disconnectTail[2048]{};
        engine.Mix(disconnectTail, 1024);
        t.Equal(engine.Active(), size_t{0}, "server handles are stopped when leaving their session");
        play.playbackPositionSeconds.reset();
        play.handle = 12;
        play.coordinates.x = 128;
        inject(play);
        t.Equal(engine.Active(), size_t{0}, "default server range check rejects the exact 16-block boundary");
        play.bypassListenerRangeCheck = true;
        inject(play);
        t.Equal(engine.Active(), size_t{1}, "range bypass permits starting at that position");
        t.Equal(sample(), 0.0f, "range bypass preserves spatial attenuation");
        camera.x = 16;
        director.Update(&session, 3, 0);
        t.Check(sample() > 0.49f, "a bypassed positional voice becomes audible when the listener approaches");
        director.Update(nullptr, 4, 0);
        float cleanupSamples[2048]{};
        engine.Mix(cleanupSamples, 1024);
        play.handle.reset();
        inject(play);
        t.Equal(engine.Active(), size_t{1}, "a server loop need not have an externally controllable handle");
        director.Update(nullptr, 5, 0);
        std::fill(std::begin(cleanupSamples), std::end(cleanupSamples), 0.0f);
        engine.Mix(cleanupSamples, 1024);
        t.Equal(engine.Active(), size_t{0}, "unaddressable server loops also end with their session");
        play.handle = 77;
        inject(play);
        inject(play);
        t.Equal(engine.Active(), size_t{2}, "server plays with handles are not merged by proximity");
        director.Update(nullptr, 6, 0);
        std::fill(std::begin(cleanupSamples), std::end(cleanupSamples), 0.0f);
        engine.Mix(cleanupSamples, 1024);
        t.Equal(engine.Active(), size_t{0}, "session cleanup retains ownership after a handle is reused");
    }
    }

    int AudioPackets() {
        Checker t("audio_packets_test");
        RunAudioPackets(t, kBedrockProtocol);
        RunAudioPackets(t, 2193);
        return t.Finish();
    }
}
