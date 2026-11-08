#include "client/audio/Capture.hpp"

#include <algorithm>

#include <SDL3/SDL.h>

namespace perlin::audio {
    Capture::~Capture() { Stop(); }

    std::vector<std::string> Capture::Devices() {
        std::vector<std::string> names;
        if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
            return names;
        int count = 0;
        SDL_AudioDeviceID* ids = SDL_GetAudioRecordingDevices(&count);
        if (!ids)
            return names;
        for (int i = 0; i < count; i++)
            if (const char* name = SDL_GetAudioDeviceName(ids[i]))
                names.emplace_back(name);
        SDL_free(ids);
        return names;
    }

    bool Capture::Start(const std::string& wanted, int sampleRate, std::string* error) {
        Stop();
        if (!SDL_WasInit(SDL_INIT_AUDIO)) {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                if (error)
                    *error = SDL_GetError();
                return false;
            }
            initialisedSubsystem = true;
        }
        SDL_AudioDeviceID id = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
        if (!wanted.empty()) {
            int count = 0;
            SDL_AudioDeviceID* ids = SDL_GetAudioRecordingDevices(&count);
            bool found = false;
            if (ids) {
                for (int i = 0; i < count && !found; i++) {
                    const char* name = SDL_GetAudioDeviceName(ids[i]);
                    if (name && wanted == name) {
                        id = ids[i];
                        found = true;
                    }
                }
                SDL_free(ids);
            }
            if (!found) {
                if (error)
                    *error = "no such microphone";
                return false;
            }
        }
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16;
        spec.channels = 1;
        spec.freq = sampleRate;
        stream = SDL_OpenAudioDeviceStream(id, &spec, nullptr, nullptr);
        if (!stream) {
            if (error)
                *error = SDL_GetError();
            return false;
        }
        SDL_ResumeAudioStreamDevice(stream);
        device = wanted;
        rate = sampleRate;
        return true;
    }

    void Capture::Stop() {
        if (stream) {
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
        }
        device.clear();
        rate = 0;
    }

    size_t Capture::Read(std::vector<int16_t>& out) {
        if (!stream)
            return 0;
        const int available = SDL_GetAudioStreamAvailable(stream);
        if (available <= 0)
            return 0;
        const size_t before = out.size();
        out.resize(before + static_cast<size_t>(available) / sizeof(int16_t));
        const int got = SDL_GetAudioStreamData(stream, out.data() + before, available & ~1);
        out.resize(before + static_cast<size_t>(std::max(got, 0)) / sizeof(int16_t));
        return out.size() - before;
    }
}
