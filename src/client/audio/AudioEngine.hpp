#pragma once
#include "client/audio/Pcm.hpp"

#include <glm/glm.hpp>

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace perlin::audio {
    enum class Category : int {
        Master,
        Music,
        Record,
        Weather,
        Block,
        Hostile,
        Neutral,
        Player,
        Ambient,
        Voice,
        Count
    };
    constexpr int kCategoryCount = static_cast<int>(Category::Count);
    const char* CategoryName(Category c);
    Category CategoryFor(const std::string& definitionCategory, const std::string& soundName);

    struct Volumes {
        std::array<float, kCategoryCount> v;
        Volumes() { v.fill(1.0f); }
        float Get(Category c) const { return v[static_cast<size_t>(c)]; }
    };

    struct Voice {
        PcmPtr pcm;
        Category category = Category::Master;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool positional = false;
        glm::dvec3 position{0.0};
        float range = 16.0f;
        bool loop = false;
        std::string name;
        uint64_t id = 0;

        uint64_t startedMs = 0;
        double cursor = 0.0;
        float fade = 1.0f;
        bool stopping = false;
    };

    struct StreamParams {
        int rate = 48000;
        int channels = 1;
        Category category = Category::Voice;
        float volume = 1.0f;
        bool positional = false;
        glm::dvec3 position{0.0};
        float range = 16.0f;
    };

    struct StreamStatus {
        float bufferedMs = 0.0f;
        uint32_t underruns = 0;
    };

    class AudioEngine {
    public:
        AudioEngine();
        ~AudioEngine();

        bool Open(int sampleRate = 48000);
        void Close();
        bool DeviceOpen() const { return stream != nullptr; }
        int SampleRate() const { return rate; }

        void SetVolumes(const Volumes& volumes);
        void SetListener(const glm::dvec3& position, float yawDegrees);

        uint64_t Start(Voice voice);
        void Stop(uint64_t id);
        void StopName(const std::string& name);
        void StopAll();
        size_t Active() const;
        bool Playing(const std::string& name) const;

        uint32_t StreamOpen(const StreamParams& params);
        bool StreamWrite(uint32_t id, const int16_t* samples, size_t count, StreamStatus& status);
        bool StreamUpdate(uint32_t id, float volume, bool positional, const glm::dvec3& position, float range);
        void StreamClose(uint32_t id);

        void Mix(float* interleavedStereo, int frames);

        static float Attenuation(float distance, float range);

    private:
        static void Callback(void* user, SDL_AudioStream* stream, int additional, int total);
        struct Stream {
            uint32_t id = 0;
            StreamParams params;
            std::vector<int16_t> buffer;
            size_t head = 0;
            double cursor = 0.0;
            bool primed = false;
            uint32_t underruns = 0;
        };
        mutable std::mutex mutex;
        std::vector<Voice> voices;
        std::vector<Stream> streams;
        uint32_t nextStream = 1;
        Volumes volumes;
        glm::dvec3 listener{0.0};
        float listenerYaw = 0;
        SDL_AudioStream* stream = nullptr;
        int rate = 48000;
        uint64_t nextId = 1;
        bool initialisedSubsystem = false;
    };
}
