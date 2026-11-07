#pragma once
#include "client/audio/AudioEngine.hpp"

#include <functional>
#include <map>
#include <random>
#include <string>
#include <unordered_map>

namespace perlin::audio {
    struct SoundDefinition {
        struct Entry {
            std::string file;
            float volume = 1.0f;
            float pitchLo = 1.0f, pitchHi = 1.0f;
            float weight = 1.0f;
        };
        std::string category;
        float minDistance = -1, maxDistance = -1;
        std::vector<Entry> entries;
    };

    struct EventSound {
        std::string sound;
        float volumeLo = 1, volumeHi = 1, pitchLo = 1, pitchHi = 1;
        bool Valid() const { return !sound.empty(); }
    };

    class SoundSystem {
    public:
        explicit SoundSystem(AudioEngine& engine) : engine(engine) {}

        void Configure(const std::string& root, const std::vector<std::string>& packs = {});
        void Reload() {
            const std::string r = root;
            configured = false;
            Configure(r, packRoots);
        }
        bool HasSounds() const { return !definitions.empty(); }
        size_t DefinitionCount() const { return definitions.size(); }

        using SubtitleHook = std::function<void(const std::string& sound, const glm::dvec3& position,
                                                float volume, bool global)>;
        void SetSubtitleHook(SubtitleHook hook) { subtitles = std::move(hook); }
        void SetBlockNamer(std::function<std::string(int32_t)> namer) { blockNamer = std::move(namer); }

        bool PlayNamed(const std::string& name, const glm::dvec3& position, float volume, float pitch,
                       bool positional = true);
        bool PlayLevelSound(const std::string& eventId, const glm::dvec3& position, int32_t extra,
                            const std::string& entityType, bool baby, bool global);
        bool PlayBlockEvent(const std::string& blockName, const std::string& event,
                            const glm::dvec3& position, float volumeScale = 1.0f);
        bool PlayEntityEvent(const std::string& entityType, const std::string& event,
                             const glm::dvec3& position, bool baby = false);
        void Stop(const std::string& name) { engine.StopName(name); }

        std::string BlockSoundType(const std::string& blockName) const;
        bool FindEntityEvent(const std::string& entityType, const std::string& event, EventSound& out) const;
        bool FindBlockEvent(const std::string& soundType, const std::string& event, EventSound& out) const;
        void LoadDefinitionsText(const std::string& text);
        void LoadSoundsText(const std::string& text);
        void LoadBlocksText(const std::string& text);

    private:
        struct Played {
            std::string name;
            float volume;
        };
        bool Play(const std::string& definition, float volume, float pitch, const glm::dvec3& position,
                  bool positional, const EventSound* event = nullptr);
        PcmPtr Load(const std::string& file);
        float Roll(float lo, float hi);

        AudioEngine& engine;
        std::string root;
        std::vector<std::string> packRoots;
        bool configured = false;
        std::unordered_map<std::string, SoundDefinition> definitions;
        std::unordered_map<std::string, std::unordered_map<std::string, EventSound>> blockEvents;
        std::unordered_map<std::string, std::unordered_map<std::string, EventSound>> entityEvents;
        std::unordered_map<std::string, EventSound> named;
        std::unordered_map<std::string, std::string> blockSoundType;
        std::unordered_map<std::string, PcmPtr> cache;
        size_t cacheBytes = 0;
        std::map<std::string, bool> warned;
        std::mt19937 rng{std::random_device{}()};
        SubtitleHook subtitles;
        std::function<std::string(int32_t)> blockNamer;
    };

    std::string EventKey(const std::string& id);
}
