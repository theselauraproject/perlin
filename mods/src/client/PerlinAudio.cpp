#include "client/PerlinInternal.hpp"
#include "util/Log.hpp"

namespace perlin {
    void Perlin::UpdateAudio(BedrockSession* live, double now, float dt) {
        static bool opened = false, hooked = false;
        const std::string& root = ui::UiAssets::Instance().VanillaRoot();
        if (!opened) {
            opened = true;
            if (!audioEngine.Open())
                Log("audio", "running without an output device");
        }
        if (!hooked) {
            hooked = true;
            sounds.SetBlockNamer([](int32_t runtimeId) {
                BlockPalette& palette = BlockPalette::Instance();
                return palette.Name(palette.InternRuntime(runtimeId));
            });
            sounds.SetSubtitleHook([this](const std::string& sound, const glm::dvec3& position, float volume,
                                          bool global) {
                if (options.sound.subtitles)
                    subtitles.Add(sound, position, volume, global,
                                  static_cast<double>(std::chrono::duration<double>(
                                                          std::chrono::steady_clock::now().time_since_epoch())
                                                          .count()));
            });
        }
        if (!root.empty())
            sounds.Configure(root, ServerPacks::Instance().Directories());
        audio::Volumes volumes;
        const SoundSettings& v = options.sound;
        volumes.v = {v.master,  v.music,   v.record, v.weather, v.block,
                     v.hostile, v.neutral, v.player, v.ambient, v.voice};
        audioEngine.SetVolumes(volumes);
        {
            // a camera preset can keep the listener on the camera (default) or on the player
            const bool atCamera = live && live->Camera().PoseActive() && !live->Camera().AudioAtPlayer();
            const CameraPose p = atCamera ? live->Camera().Pose() : CameraPose{};
            if (atCamera)
                audioEngine.SetListener({p.position.x, p.position.y, p.position.z}, p.yaw - 90.0f);
            else
                audioEngine.SetListener({camera.x, camera.y, camera.z}, camera.yaw - 90.0f);
        }

        static double nextMusic = 0;
        if (live && options.sound.music > 0.0f) {
            if (nextMusic == 0)
                nextMusic = now + 10.0 + (std::rand() % 2000) / 100.0;
            if (now >= nextMusic) {
                const std::string event = live->GameType() == 1 ? "music.game.creative" : "music.game";
                const bool started =
                    sounds.PlayNamed(event, {camera.x, camera.y, camera.z}, 1.0f, 1.0f, false);
                nextMusic = now + (started ? 60.0 + (std::rand() % 12000) / 100.0 + 120.0 : 300.0);
            }
        }

        if (live) {
            for (const BedrockSession::SoundRequest& r : live->TakeSounds()) {
                const glm::dvec3 at(r.x, r.y, r.z);
                NoteModSound(r.name, r.x, r.y, r.z, r.volume, r.pitch);
                switch (r.kind) {
                case BedrockSession::SoundRequest::Kind::Named:
                    sounds.PlayNamed(r.name, at, r.volume, r.pitch, !r.global);
                    break;
                case BedrockSession::SoundRequest::Kind::Level:
                    sounds.PlayLevelSound(r.name, at, r.extra, r.entityType, r.baby, r.global);
                    break;
                case BedrockSession::SoundRequest::Kind::Stop:
                    if (r.stopAll)
                        audioEngine.StopAll();
                    else
                        sounds.Stop(r.name);
                    break;
                }
            }

            const physics::State& st = player.State();
            static physics::State last = st;
            static double walked = 0, nextStep = 1;
            static bool haveLast = false;
            if (!haveLast) {
                last = st;
                haveLast = true;
            }
            Level& level = live->NetLevel();
            const auto blockUnder = [&]() -> std::string {
                for (double dy : {0.2, 0.6, 1.1}) {
                    bool loaded = false;
                    const uint16_t id = level.GetBlock(static_cast<int>(std::floor(st.x)),
                                                       static_cast<int>(std::floor(st.y - dy)),
                                                       static_cast<int>(std::floor(st.z)), loaded);
                    if (!loaded)
                        continue;
                    const std::string name = BlockPalette::Instance().Name(id);
                    if (name != "minecraft:air" && !sounds.BlockSoundType(name).empty())
                        return name;
                }
                return {};
            };
            const glm::dvec3 feet(st.x, st.y, st.z);
            if (hudState.mode != ui::GameMode::Spectator) {
                static double swimClock = 0;
                swimClock += dt;
                if (st.inWater && !last.inWater && std::fabs(st.vy) > 0.1)
                    sounds.PlayEntityEvent("minecraft:player", "splash", feet);
                else if (st.inWater && swimClock > 0.45 && std::hypot(st.x - last.x, st.z - last.z) > 0.03) {
                    swimClock = 0;
                    sounds.PlayEntityEvent("minecraft:player", "swim", feet);
                }
                if (st.onGround && !last.onGround && !st.inWater) {
                    const std::string under = blockUnder();
                    if (!under.empty())
                        sounds.PlayBlockEvent(under, last.y - st.y > 3.0 ? "fall" : "land", feet);
                } else if (!st.onGround && last.onGround && st.y > last.y && !st.inWater) {
                    const std::string under = blockUnder();
                    if (!under.empty())
                        sounds.PlayBlockEvent(under, "jump", feet);
                }
                if (st.onGround && !st.inWater && !st.onClimbable && !player.Sneaking()) {
                    walked += std::hypot(st.x - last.x, st.z - last.z) * 0.6;
                    if (walked > nextStep) {
                        nextStep = std::floor(walked) + 1.0;
                        const std::string under = blockUnder();
                        if (!under.empty())
                            sounds.PlayBlockEvent(under, "step", feet);
                    }
                }
            }
            last = st;
        }

        hudState.subtitles.clear();
        if (options.sound.subtitles) {
            const double clockNow =
                std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            for (const audio::Subtitles::Line& l :
                 subtitles.Visible({camera.x, camera.y, camera.z}, camera.yaw - 90.0f, clockNow))
                hudState.subtitles.push_back({l.text, l.direction, l.brightness});
        } else {
            subtitles.Clear();
        }
        if (PERLIN_ENV("PERLIN_PREVIEW_BORDERS"))
            debug.chunkBorders = true;
        if (PERLIN_ENV("PERLIN_PREVIEW_SUBTITLES")) {
            hudState.subtitles = {{"Cow moos", -1, 1.0f},
                                  {"Footsteps", 0, 0.8f},
                                  {"Zombie groans", 1, 0.5f},
                                  {"Explosion", 1, 0.3f}};
        }
        (void)dt;
        (void)now;
    }
}
