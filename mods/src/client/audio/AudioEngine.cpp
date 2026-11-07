#include "client/audio/AudioEngine.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include "util/Log.hpp"

namespace perlin::audio {
    namespace {
        constexpr size_t kMaxVoices = 96;
    }

    const char* CategoryName(Category c) {
        static const char* kNames[] = {"master",  "music",   "record", "weather", "block",
                                       "hostile", "neutral", "player", "ambient", "voice"};
        return kNames[static_cast<int>(c)];
    }

    Category CategoryFor(const std::string& category, const std::string& name) {
        if (category == "music")
            return Category::Music;
        if (category == "record")
            return Category::Record;
        if (category == "weather")
            return Category::Weather;
        if (category == "block")
            return Category::Block;
        if (category == "hostile")
            return Category::Hostile;
        if (category == "neutral")
            return Category::Neutral;
        if (category == "player")
            return Category::Player;
        if (category == "ambient")
            return Category::Ambient;
        if (category == "ui")
            return Category::Master;
        if (name.starts_with("music."))
            return Category::Music;
        if (name.starts_with("record."))
            return Category::Record;
        if (name.starts_with("ambient.weather") || name.starts_with("weather."))
            return Category::Weather;
        if (name.starts_with("ambient."))
            return Category::Ambient;
        if (name.starts_with("mob."))
            return Category::Neutral;
        if (name.starts_with("step.") || name.starts_with("dig.") || name.starts_with("break.") ||
            name.starts_with("place.") || name.starts_with("hit.") || name.starts_with("fall.") ||
            name.starts_with("block."))
            return Category::Block;
        return Category::Player;
    }

    AudioEngine::AudioEngine() = default;
    AudioEngine::~AudioEngine() {
        Close();
    }

    bool AudioEngine::Open(int sampleRate) {
        Close();
        rate = sampleRate;
        if (!SDL_WasInit(SDL_INIT_AUDIO)) {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                Log("audio", "no audio subsystem: {}", SDL_GetError());
                return false;
            }
            initialisedSubsystem = true;
        }
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = 2;
        spec.freq = sampleRate;
        stream =
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioEngine::Callback, this);
        if (!stream) {
            Log("audio", "cannot open the playback device: {}", SDL_GetError());
            return false;
        }
        SDL_ResumeAudioStreamDevice(stream);
        Log("audio", "playing at {} Hz", sampleRate);
        return true;
    }

    void AudioEngine::Close() {
        if (stream) {
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
        }
        if (initialisedSubsystem) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            initialisedSubsystem = false;
        }
        std::lock_guard lock(mutex);
        voices.clear();
    }

    void AudioEngine::Callback(void* user, SDL_AudioStream* s, int additional, int total) {
        (void)total;
        auto* self = static_cast<AudioEngine*>(user);
        constexpr int kChunk = 1024;
        std::vector<float> buffer(static_cast<size_t>(kChunk) * 2);
        int bytes = additional;
        while (bytes > 0) {
            const int frames = std::min(kChunk, (bytes + 7) / 8);
            std::fill(buffer.begin(), buffer.begin() + static_cast<size_t>(frames) * 2, 0.0f);
            self->Mix(buffer.data(), frames);
            SDL_PutAudioStreamData(s, buffer.data(), frames * 8);
            bytes -= frames * 8;
        }
    }

    void AudioEngine::SetVolumes(const Volumes& v) {
        std::lock_guard lock(mutex);
        volumes = v;
    }

    void AudioEngine::SetListener(const glm::dvec3& position, float yawDegrees) {
        std::lock_guard lock(mutex);
        listener = position;
        listenerYaw = yawDegrees;
    }

    uint64_t AudioEngine::Start(Voice voice) {
        if (!voice.pcm || voice.pcm->Frames() == 0)
            return 0;
        std::lock_guard lock(mutex);
        if (voice.positional && glm::length(voice.position - listener) > static_cast<double>(voice.range))
            return 0;
        const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                         std::chrono::steady_clock::now().time_since_epoch())
                                                         .count());
        if (voice.positional && !voice.loop && !voice.name.empty()) {
            // predict+echo dedup: server echo of place/break/hit ~1 frame later; same sound+spot in window = echo;
            // legit repeats (steps 250ms+, bridging 166ms+ in other cells) fall outside
            for (const Voice& v : voices) {
                if (v.stopping || v.name != voice.name || nowMs - v.startedMs > 90u)
                    continue;
                const glm::dvec3 d = v.position - voice.position;
                if (glm::dot(d, d) < 1.0e-6)
                    return v.id;
            }
        }
        if (voices.size() >= kMaxVoices) {
            auto it = std::find_if(voices.begin(), voices.end(), [](const Voice& v) { return !v.loop; });
            if (it == voices.end())
                return 0;
            voices.erase(it);
        }
        voice.id = nextId++;
        voice.startedMs = nowMs;
        voices.push_back(std::move(voice));
        return voices.back().id;
    }

    void AudioEngine::Stop(uint64_t id) {
        std::lock_guard lock(mutex);
        for (Voice& v : voices)
            if (v.id == id)
                v.stopping = true;
    }

    void AudioEngine::StopName(const std::string& name) {
        std::lock_guard lock(mutex);
        for (Voice& v : voices)
            if (v.name == name)
                v.stopping = true;
    }

    void AudioEngine::StopAll() {
        std::lock_guard lock(mutex);
        for (Voice& v : voices)
            v.stopping = true;
    }

    size_t AudioEngine::Active() const {
        std::lock_guard lock(mutex);
        return voices.size();
    }

    bool AudioEngine::Playing(const std::string& name) const {
        std::lock_guard lock(mutex);
        return std::any_of(voices.begin(), voices.end(),
                           [&](const Voice& v) { return v.name == name && !v.stopping; });
    }

    float AudioEngine::Attenuation(float distance, float range) {
        if (range <= 1.0f)
            return distance <= 1.0f ? 1.0f : 0.0f;
        return std::clamp(1.0f - (distance - 1.0f) / (range - 1.0f), 0.0f, 1.0f);
    }

    void AudioEngine::Mix(float* out, int frames) {
        std::lock_guard lock(mutex);
        const float master = volumes.Get(Category::Master);
        const double yaw = static_cast<double>(listenerYaw) * 3.14159265358979323846 / 180.0;
        const glm::dvec2 right(-std::cos(yaw), -std::sin(yaw));
        for (Voice& v : voices) {
            const Pcm& pcm = *v.pcm;
            const size_t total = pcm.Frames();
            float left = 1.0f, rightGain = 1.0f;
            float gain = v.volume;
            if (v.category != Category::Master)
                gain *= volumes.Get(v.category);
            gain = std::clamp(gain * master, 0.0f, 1.0f);
            if (v.positional) {
                const glm::dvec3 d = v.position - listener;
                const double dist = glm::length(d);
                gain *= Attenuation(static_cast<float>(dist), v.range);
                double pan = 0.0;
                if (dist > 1e-3) {
                    pan = (d.x * right.x + d.z * right.y) / dist;
                    pan *= std::min(1.0, dist / 1.5);
                }
                const double angle = (pan + 1.0) * 0.25 * 3.14159265358979323846;
                left = static_cast<float>(std::cos(angle)) * 1.41421356f;
                rightGain = static_cast<float>(std::sin(angle)) * 1.41421356f;
                left = std::min(left, 1.0f);
                rightGain = std::min(rightGain, 1.0f);
            }
            const double step =
                static_cast<double>(pcm.rate) * static_cast<double>(v.pitch) / static_cast<double>(rate);
            const float fadeStep = 1.0f / (0.02f * static_cast<float>(rate));
            const int channels = pcm.channels;
            for (int i = 0; i < frames; i++) {
                size_t i0 = static_cast<size_t>(v.cursor);
                if (i0 >= total) {
                    if (!v.loop) {
                        v.stopping = true;
                        v.fade = 0.0f;
                        break;
                    }
                    v.cursor = std::fmod(v.cursor, static_cast<double>(total));
                    i0 = static_cast<size_t>(v.cursor);
                }
                size_t i1 = i0 + 1;
                if (i1 >= total)
                    i1 = v.loop ? 0 : i0;
                const float t = static_cast<float>(v.cursor - static_cast<double>(i0));
                float l, r;
                if (channels == 1) {
                    const float a = pcm.samples[i0], b = pcm.samples[i1];
                    l = r = (a + (b - a) * t) * (1.0f / 32768.0f);
                } else {
                    const float al = pcm.samples[i0 * 2], bl = pcm.samples[i1 * 2];
                    const float ar = pcm.samples[i0 * 2 + 1], br = pcm.samples[i1 * 2 + 1];
                    l = (al + (bl - al) * t) * (1.0f / 32768.0f);
                    r = (ar + (br - ar) * t) * (1.0f / 32768.0f);
                    if (v.positional)
                        l = r = (l + r) * 0.5f;
                }
                if (v.stopping) {
                    v.fade -= fadeStep;
                    if (v.fade <= 0.0f) {
                        v.fade = 0.0f;
                        break;
                    }
                }
                const float g = gain * v.fade;
                out[i * 2] += l * g * left;
                out[i * 2 + 1] += r * g * rightGain;
                v.cursor += step;
            }
        }
        voices.erase(
            std::remove_if(voices.begin(), voices.end(),
                           [](const Voice& v) { return v.fade <= 0.0f || (v.stopping && v.fade <= 0.0f); }),
            voices.end());
        for (int i = 0; i < frames * 2; i++) {
            const float a = std::fabs(out[i]);
            if (a > 0.8f)
                out[i] = std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f), out[i]);
        }
    }
}
