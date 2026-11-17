#include "client/features/audio/AudioDirector.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "util/Log.hpp"

namespace perlin::audio {
    namespace {
        double ClockSeconds() {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    void AudioDirector::OpenOnce() {
        if (!opened) {
            opened = true;
            if (!deps.engine.Open())
                Log("audio", "running without an output device");
        }
        if (hooked)
            return;
        hooked = true;
        deps.sounds.SetBlockNamer([this](int32_t runtimeId) {
            return deps.palette.Name(deps.palette.InternRuntime(runtimeId));
        });
        deps.sounds.SetSubtitleHook([this](const std::string& sound, const glm::dvec3& position, float volume,
                                           bool global) {
            if (deps.options.sound.subtitles)
                subtitles.Add(sound, position, volume, global, ClockSeconds());
        });
    }

    void AudioDirector::UpdateListener(BedrockSession* live) {
        Volumes volumes;
        const SoundSettings& v = deps.options.sound;
        volumes.v = {v.master, v.music, v.record, v.weather, v.block, v.hostile, v.neutral, v.player, v.ambient, v.voice};
        deps.engine.SetVolumes(volumes);
        const bool atCamera = live && live->Camera().PoseActive() && !live->Camera().AudioAtPlayer();
        const CameraPose p = atCamera ? live->Camera().Pose() : CameraPose{};
        if (atCamera)
            deps.engine.SetListener({p.position.x, p.position.y, p.position.z}, p.yaw - 90.0f);
        else
            deps.engine.SetListener({deps.camera.x, deps.camera.y, deps.camera.z}, deps.camera.yaw - 90.0f);
    }

    bool AudioDirector::EchoesLocalPlace(const BedrockSession::SoundRequest& r, const std::string& lowered) const {
        if (lowered != "place" && lowered != "itemuseon")
            return false;
        if (Clock::now() - lastLocalPlace.at >= std::chrono::milliseconds(900))
            return false;
        return std::abs(static_cast<int>(std::floor(r.x)) - lastLocalPlace.x) <= 1 &&
               std::abs(static_cast<int>(std::floor(r.y)) - lastLocalPlace.y) <= 1 &&
               std::abs(static_cast<int>(std::floor(r.z)) - lastLocalPlace.z) <= 1;
    }

    void AudioDirector::DispatchRequests(BedrockSession& live) {
        for (const BedrockSession::SoundRequest& r : live.TakeSounds()) {
            const glm::dvec3 at(r.x, r.y, r.z);
            if (deps.noteSound)
                deps.noteSound(r);
            switch (r.kind) {
            case BedrockSession::SoundRequest::Kind::Named:
                deps.sounds.PlayNamed(r.name, at, r.volume, r.pitch, !r.global);
                break;
            case BedrockSession::SoundRequest::Kind::Level: {
                std::string lowered = r.name;
                std::ranges::transform(lowered, lowered.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (EchoesLocalPlace(r, lowered))
                    break;
                deps.sounds.PlayLevelSound(r.name, at, r.extra, r.entityType, r.baby, r.global);
                break;
            }
            case BedrockSession::SoundRequest::Kind::Stop:
                if (r.stopAll)
                    deps.engine.StopAll();
                else
                    deps.sounds.Stop(r.name);
                break;
            }
        }
    }

    void AudioDirector::UpdateMovement(BedrockSession& live, float dt) {
        Level& level = live.NetLevel();
        MovementSounds::Port port;
        port.blockAt = [&level, this](int x, int y, int z, std::string& name) {
            bool loaded = false;
            const uint16_t id = level.GetBlock(x, y, z, loaded);
            if (!loaded)
                return false;
            name = deps.palette.Name(id);
            return true;
        };
        port.hasBlockSound = [this](const std::string& name) { return !deps.sounds.BlockSoundType(name).empty(); };
        port.playerEvent = [this](const std::string& event, const glm::dvec3& at) {
            deps.sounds.PlayEntityEvent("minecraft:player", event, at);
        };
        port.blockEvent = [this](const std::string& name, const std::string& event, const glm::dvec3& at) {
            deps.sounds.PlayBlockEvent(name, event, at);
        };
        movement.Update(deps.player.State(), deps.player.Sneaking(), deps.hudState.mode == ui::GameMode::Spectator, dt,
                        port);
    }

    void AudioDirector::UpdateSubtitles() {
        deps.hudState.subtitles.clear();
        if (!deps.options.sound.subtitles) {
            subtitles.Clear();
            return;
        }
        const Camera& c = deps.camera;
        for (const Subtitles::Line& l : subtitles.Visible({c.x, c.y, c.z}, c.yaw - 90.0f, ClockSeconds()))
            deps.hudState.subtitles.push_back({l.text, l.direction, l.brightness});
    }

    void AudioDirector::Update(BedrockSession* live, double now, float dt) {
        OpenOnce();
        const std::string root = deps.vanillaRoot();
        if (!root.empty())
            deps.sounds.Configure(root, deps.packDirectories());
        UpdateListener(live);
        const Camera& c = deps.camera;
        music.Update(now, live && deps.options.sound.music > 0.0f, [&] {
            const std::string event = live->GameType() == 1 ? "music.game.creative" : "music.game";
            return deps.sounds.PlayNamed(event, {c.x, c.y, c.z}, 1.0f, 1.0f, false);
        });
        if (live) {
            DispatchRequests(*live);
            UpdateMovement(*live, dt);
        }
        UpdateSubtitles();
    }
}
