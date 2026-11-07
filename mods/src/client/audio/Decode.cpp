#include "client/audio/Pcm.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_VORBIS_NO_PUSHDATA_API
#include <stb_vorbis.c>

namespace perlin::audio {
    namespace {
        uint32_t U32(const uint8_t* p) {
            return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }
        uint16_t U16(const uint8_t* p) {
            return static_cast<uint16_t>(p[0] | (p[1] << 8));
        }
        int16_t S16(const uint8_t* p) {
            return static_cast<int16_t>(U16(p));
        }

        void Fail(std::string* error, const char* message) {
            if (error)
                *error = message;
        }

        constexpr int8_t kFadpcmCoefs[8][2] = {{0, 0},   {60, 0}, {122, 60}, {115, 52},
                                               {98, 55}, {0, 0},  {0, 0},    {0, 0}};

        bool DecodeFsb5(const std::vector<uint8_t>& b, Pcm& out, std::string* error) {
            if (b.size() < 0x40 || std::memcmp(b.data(), "FSB5", 4) != 0) {
                Fail(error, "not an FSB5 file");
                return false;
            }
            const uint32_t version = U32(&b[4]), count = U32(&b[8]), headerBytes = U32(&b[12]),
                           nameBytes = U32(&b[16]), dataBytes = U32(&b[20]), codec = U32(&b[24]);
            const size_t headerSize = version == 0 ? 0x40 : 0x3C;
            if (count < 1 || headerSize + headerBytes + nameBytes + dataBytes > b.size()) {
                Fail(error, "truncated FSB5 file");
                return false;
            }
            static const int kRates[] = {4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000};
            const uint8_t* sh = &b[headerSize];
            const uint64_t mode = static_cast<uint64_t>(U32(sh)) | (static_cast<uint64_t>(U32(sh + 4)) << 32);
            const bool extra = (mode & 1) != 0;
            const int freq = static_cast<int>((mode >> 1) & 0xF);
            int channels = static_cast<int>((mode >> 5) & 3) + 1;
            if (channels > 2) {
                Fail(error, "unsupported channel layout");
                return false;
            }
            const uint64_t dataOffset = ((mode >> 6) & 0x0FFFFFFF) * 16;
            const uint64_t samples = mode >> 34;
            int rate = freq >= 1 && freq <= 9 ? kRates[freq] : 44100;
            if (freq == 0)
                rate = 44100;
            size_t at = 8;
            while (extra && at + 4 <= headerBytes) {
                const uint32_t chunk = U32(sh + at);
                const uint32_t size = (chunk >> 1) & 0xFFFFFF, type = chunk >> 25;
                if (at + 4 + size > headerBytes)
                    break;
                if (type == 1 && size >= 1)
                    channels = std::clamp<int>(sh[at + 4], 1, 2);
                else if (type == 2 && size >= 4)
                    rate = static_cast<int>(U32(sh + at + 4));
                at += 4 + size;
                if (!(chunk & 1))
                    break;
            }
            const uint8_t* data = &b[headerSize + headerBytes + nameBytes + dataOffset];
            const size_t available = dataBytes > dataOffset ? dataBytes - dataOffset : 0;
            out.rate = rate;
            out.channels = channels;
            if (codec == 2) {
                const size_t n = std::min<size_t>(available / 2, samples * static_cast<size_t>(channels));
                out.samples.resize(n);
                for (size_t i = 0; i < n; i++)
                    out.samples[i] = S16(data + i * 2);
                return n > 0;
            }
            if (codec == 1) {
                const size_t n = std::min<size_t>(available, samples * static_cast<size_t>(channels));
                out.samples.resize(n);
                for (size_t i = 0; i < n; i++)
                    out.samples[i] = static_cast<int16_t>((static_cast<int>(data[i]) - 128) * 256);
                return n > 0;
            }
            if (codec == 16) {
                constexpr size_t kFrameBytes = 0x8C, kFrameSamples = 256;
                const size_t frames = (samples + kFrameSamples - 1) / kFrameSamples;
                if (frames * kFrameBytes * static_cast<size_t>(channels) > available) {
                    Fail(error, "truncated FADPCM data");
                    return false;
                }
                out.samples.assign(samples * static_cast<size_t>(channels), 0);
                for (int c = 0; c < channels; c++)
                    for (size_t f = 0; f < frames; f++) {
                        const uint8_t* frame =
                            data + (f * static_cast<size_t>(channels) + static_cast<size_t>(c)) * kFrameBytes;
                        const uint32_t coefs = U32(frame), shifts = U32(frame + 4);
                        int32_t hist1 = S16(frame + 8), hist2 = S16(frame + 10);
                        size_t written = f * kFrameSamples;
                        for (int i = 0; i < 8; i++) {
                            const int index = static_cast<int>((coefs >> (i * 4)) & 0xF) & 7;
                            const int shift = 22 - static_cast<int>((shifts >> (i * 4)) & 0xF);
                            const int32_t c1 = kFadpcmCoefs[index][0], c2 = kFadpcmCoefs[index][1];
                            for (int j = 0; j < 4; j++) {
                                uint32_t nibbles = U32(frame + 0x0C + i * 0x10 + j * 4);
                                for (int k = 0; k < 8; k++) {
                                    int32_t s = static_cast<int32_t>((nibbles & 0xF) << 28) >> shift;
                                    nibbles >>= 4;
                                    s = (s + hist1 * c1 - hist2 * c2) >> 6;
                                    s = std::clamp(s, -32768, 32767);
                                    hist2 = hist1;
                                    hist1 = s;
                                    if (written < samples)
                                        out.samples[written * static_cast<size_t>(channels) +
                                                    static_cast<size_t>(c)] = static_cast<int16_t>(s);
                                    written++;
                                }
                            }
                        }
                    }
                return true;
            }
            Fail(error, "unsupported FSB codec (only PCM and FADPCM)");
            return false;
        }

        bool DecodeWav(const std::vector<uint8_t>& b, Pcm& out, std::string* error) {
            if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0 ||
                std::memcmp(&b[8], "WAVE", 4) != 0) {
                Fail(error, "not a WAV file");
                return false;
            }
            int format = 0, channels = 0, rate = 0, bits = 0;
            size_t pos = 12;
            while (pos + 8 <= b.size()) {
                const uint32_t size = U32(&b[pos + 4]);
                const uint8_t* body = &b[pos + 8];
                if (std::memcmp(&b[pos], "fmt ", 4) == 0 && pos + 8 + 16 <= b.size()) {
                    format = U16(body);
                    channels = U16(body + 2);
                    rate = static_cast<int>(U32(body + 4));
                    bits = U16(body + 14);
                    if (format == 0xFFFE && size >= 26)
                        format = U16(body + 24);
                } else if (std::memcmp(&b[pos], "data", 4) == 0) {
                    const size_t n = std::min<size_t>(size, b.size() - pos - 8);
                    if (channels < 1 || channels > 2 || rate <= 0) {
                        Fail(error, "unsupported WAV layout");
                        return false;
                    }
                    out.rate = rate;
                    out.channels = channels;
                    if (format == 1 && bits == 16) {
                        out.samples.resize(n / 2);
                        for (size_t i = 0; i < n / 2; i++)
                            out.samples[i] = S16(body + i * 2);
                    } else if (format == 1 && bits == 8) {
                        out.samples.resize(n);
                        for (size_t i = 0; i < n; i++)
                            out.samples[i] = static_cast<int16_t>((static_cast<int>(body[i]) - 128) * 256);
                    } else if (format == 3 && bits == 32) {
                        out.samples.resize(n / 4);
                        for (size_t i = 0; i < n / 4; i++) {
                            float f;
                            std::memcpy(&f, body + i * 4, 4);
                            out.samples[i] = static_cast<int16_t>(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
                        }
                    } else {
                        Fail(error, "unsupported WAV sample format");
                        return false;
                    }
                    return !out.samples.empty();
                }
                pos += 8 + size + (size & 1);
            }
            Fail(error, "WAV has no data");
            return false;
        }

        bool DecodeOgg(const std::vector<uint8_t>& b, Pcm& out, std::string* error) {
            int channels = 0, rate = 0;
            short* decoded = nullptr;
            const int frames =
                stb_vorbis_decode_memory(b.data(), static_cast<int>(b.size()), &channels, &rate, &decoded);
            if (frames <= 0 || !decoded || channels < 1) {
                Fail(error, "invalid Ogg Vorbis data");
                free(decoded);
                return false;
            }
            if (channels > 2) {
                out.samples.resize(static_cast<size_t>(frames) * 2);
                for (int i = 0; i < frames; i++) {
                    out.samples[static_cast<size_t>(i) * 2] = decoded[static_cast<size_t>(i) * channels];
                    out.samples[static_cast<size_t>(i) * 2 + 1] =
                        decoded[static_cast<size_t>(i) * channels + 1];
                }
                channels = 2;
            } else {
                out.samples.assign(decoded, decoded + static_cast<size_t>(frames) * channels);
            }
            free(decoded);
            out.rate = rate;
            out.channels = channels;
            return true;
        }
    }

    bool DecodeSound(const std::vector<uint8_t>& bytes, const std::string& extension, Pcm& out,
                     std::string* error) {
        out = Pcm();
        if (extension == ".fsb")
            return DecodeFsb5(bytes, out, error);
        if (extension == ".ogg")
            return DecodeOgg(bytes, out, error);
        if (extension == ".wav")
            return DecodeWav(bytes, out, error);
        Fail(error, "unsupported sound format");
        return false;
    }
}
