#include "client/systems/audio/MovementSounds.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {
    using perlin::audio::MovementSounds;
    using perlin::audio::MusicScheduler;

    struct World {
        std::map<std::tuple<int, int, int>, std::pair<bool, std::string>> blocks;
        std::map<std::string, bool> soundy;
        bool loadedAll = true;
    };

    struct Recorder {
        std::vector<std::string> events;
        void Add(const std::string& kind, const std::string& block, const std::string& ev, const glm::dvec3& at) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "%s|%s|%s|%.6f,%.6f,%.6f", kind.c_str(), block.c_str(), ev.c_str(),
                          at.x, at.y, at.z);
            events.emplace_back(buf);
        }
    };

    namespace orig {
        struct Sounds {
            Recorder& rec;
            const World& world;
            void PlayEntityEvent(const std::string& type, const std::string& ev, const glm::dvec3& at) {
                rec.Add("entity", type, ev, at);
            }
            void PlayBlockEvent(const std::string& name, const std::string& ev, const glm::dvec3& at) {
                rec.Add("block", name, ev, at);
            }
            std::string BlockSoundType(const std::string& name) const {
                auto it = world.soundy.find(name);
                return it != world.soundy.end() && it->second ? "stone" : "";
            }
        };
        struct Level {
            const World& world;
            uint16_t GetBlock(int x, int y, int z, bool& loaded) const {
                auto it = world.blocks.find({x, y, z});
                loaded = it != world.blocks.end() && it->second.first;
                return 0;
            }
            std::string Name(int x, int y, int z) const {
                auto it = world.blocks.find({x, y, z});
                return it == world.blocks.end() ? "minecraft:air" : it->second.second;
            }
        };

        struct Movement {
            perlin::physics::State last;
            double walked = 0, nextStep = 1, swimClock = 0;
            bool haveLast = false;

            void Update(Sounds& sounds, Level& level, const perlin::physics::State& st, bool sneaking,
                        bool spectator, float dt) {
            if (!haveLast) {
                last = st;
                haveLast = true;
            }
            const auto blockUnder = [&]() -> std::string {
                for (double dy : {0.2, 0.6, 1.1}) {
                    bool loaded = false;
                    const int bx = static_cast<int>(std::floor(st.x));
                    const int by = static_cast<int>(std::floor(st.y - dy));
                    const int bz = static_cast<int>(std::floor(st.z));
                    level.GetBlock(bx, by, bz, loaded);
                    if (!loaded)
                        continue;
                    const std::string name = level.Name(bx, by, bz);
                    if (name != "minecraft:air" && !sounds.BlockSoundType(name).empty())
                        return name;
                }
                return {};
            };
            const glm::dvec3 feet(st.x, st.y, st.z);
            if (!spectator) {
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
                if (st.onGround && !st.inWater && !st.onClimbable && !sneaking) {
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
        };

        struct Music {
            std::mt19937 rng;
            double nextMusic = 0;
            int Rand() { return static_cast<int>(rng() & 0x7fffffff); }
            void Update(double now, bool live, float musicVolume, bool creative, std::vector<std::string>& log,
                        bool playResult) {
                if (live && musicVolume > 0.0f) {
                    if (nextMusic == 0)
                        nextMusic = now + 10.0 + (Rand() % 2000) / 100.0;
                    if (now >= nextMusic) {
                        const std::string event = creative ? "music.game.creative" : "music.game";
                        log.push_back(event);
                        const bool started = playResult;
                        nextMusic = now + (started ? 60.0 + (Rand() % 12000) / 100.0 + 120.0 : 300.0);
                    }
                }
            }
        };
    }

    int failures = 0;
    long steps = 0;
}

int main() {
    std::mt19937 rng(1234);
    const char* names[] = {"minecraft:stone", "minecraft:grass", "minecraft:air", "minecraft:glass",
                           "minecraft:wool", "minecraft:mute"};
    long eventsSeen = 0;
    for (int trial = 0; trial < 40; trial++) {
        Recorder orec, nrec;
        World world;
        for (int x = -4; x < 4; x++)
            for (int y = -2; y < 3; y++)
                for (int z = -4; z < 4; z++)
                    world.blocks[{x, y, z}] = {rng() % 5 != 0, names[rng() % 6]};
        for (const char* n : names)
            world.soundy[n] = std::string(n) != "minecraft:mute" && rng() % 4 != 0;
        orig::Sounds osounds{orec, world};
        orig::Level olevel{world};
        orig::Movement original;
        MovementSounds::Port port;
        port.blockAt = [&](int x, int y, int z, std::string& name) {
            auto it = world.blocks.find({x, y, z});
            if (it == world.blocks.end() || !it->second.first)
                return false;
            name = it->second.second;
            return true;
        };
        port.hasBlockSound = [&](const std::string& n) { return osounds.BlockSoundType(n) != ""; };
        port.playerEvent = [&](const std::string& ev, const glm::dvec3& at) {
            nrec.Add("entity", "minecraft:player", ev, at);
        };
        port.blockEvent = [&](const std::string& n, const std::string& ev, const glm::dvec3& at) {
            nrec.Add("block", n, ev, at);
        };
        MovementSounds fresh;
        perlin::physics::State st;
        st.x = 0.5;
        st.z = 0.5;
        st.onGround = true;
        for (int i = 0; i < 4000; i++) {
            st.x += ((rng() % 1000) / 1000.0 - 0.5) * 0.6;
            st.z += ((rng() % 1000) / 1000.0 - 0.5) * 0.6;
            st.y += ((rng() % 1000) / 1000.0 - 0.45) * (rng() % 20 == 0 ? 8.0 : 0.5);
            st.vy = ((rng() % 1000) / 1000.0 - 0.5) * 0.6;
            st.onGround = rng() % 3 != 0;
            st.inWater = rng() % 8 == 0;
            st.onClimbable = rng() % 10 == 0;
            const bool sneaking = rng() % 6 == 0;
            const bool spectator = rng() % 15 == 0;
            const float dt = 0.005f + (rng() % 100) / 1000.0f;
            original.Update(osounds, olevel, st, sneaking, spectator, dt);
            fresh.Update(st, sneaking, spectator, dt, port);
            steps++;
        }
        eventsSeen += static_cast<long>(orec.events.size());
        if (orec.events != nrec.events) {
            failures++;
            std::printf("trial %d: movement events differ (%zu vs %zu)\n", trial, orec.events.size(),
                        nrec.events.size());
        }
    }
    if (eventsSeen < 1000) {
        failures++;
        std::printf("too few movement events generated: %ld\n", eventsSeen);
    }

    for (unsigned seed = 1; seed <= 20; seed++) {
        orig::Music om{std::mt19937(seed)};
        std::mt19937 nr(seed);
        MusicScheduler sched([&] { return static_cast<int>(nr() & 0x7fffffff); });
        std::mt19937 drive(99 + seed);
        std::vector<std::string> olog, nlog;
        double now = 0;
        for (int i = 0; i < 20000; i++) {
            now += (drive() % 4000) / 10.0;
            const bool live = drive() % 5 != 0;
            const float vol = drive() % 7 == 0 ? 0.0f : 1.0f;
            const bool creative = drive() % 2;
            const bool played = drive() % 3 != 0;
            om.Update(now, live, vol, creative, olog, played);
            sched.Update(now, live && vol > 0.0f, [&] {
                nlog.push_back(creative ? "music.game.creative" : "music.game");
                return played;
            });
            steps++;
        }
        if (olog != nlog || olog.empty()) {
            failures++;
            std::printf("seed %u: music sequences differ (%zu vs %zu)\n", seed, olog.size(), nlog.size());
        }
    }
    std::printf("%ld steps, %ld movement events, %d failures\n", steps, eventsSeen, failures);
    return failures ? 1 : 0;
}
