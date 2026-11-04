#include "client/audio/SoundSystem.hpp"
#include "client/audio/Subtitles.hpp"
#include "client/i18n/Lang.hpp"

#include <fstream>
#include <sstream>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace perlin::audio;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << " " #cond << "\n";                        \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static void Put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; i++)
        b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
static void Put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}

static std::vector<uint8_t> MakeWav(const std::vector<int16_t>& samples, int rate, int channels) {
    std::vector<uint8_t> b = {'R', 'I', 'F', 'F'};
    Put32(b, 36 + static_cast<uint32_t>(samples.size() * 2));
    for (char c : std::string("WAVEfmt "))
        b.push_back(static_cast<uint8_t>(c));
    Put32(b, 16);
    Put16(b, 1);
    Put16(b, static_cast<uint16_t>(channels));
    Put32(b, rate);
    Put32(b, rate * channels * 2);
    Put16(b, static_cast<uint16_t>(channels * 2));
    Put16(b, 16);
    for (char c : std::string("data"))
        b.push_back(static_cast<uint8_t>(c));
    Put32(b, static_cast<uint32_t>(samples.size() * 2));
    for (int16_t s : samples)
        Put16(b, static_cast<uint16_t>(s));
    return b;
}

static std::vector<uint8_t> MakeFsb(uint32_t codec, const std::vector<uint8_t>& data, uint64_t samples,
                                    int freqIndex, bool stereo) {
    std::vector<uint8_t> b = {'F', 'S', 'B', '5'};
    Put32(b, 1);
    Put32(b, 1);
    Put32(b, 8);
    Put32(b, 0);
    Put32(b, static_cast<uint32_t>(data.size()));
    Put32(b, codec);
    while (b.size() < 60)
        b.push_back(0);
    const uint64_t mode =
        (static_cast<uint64_t>(freqIndex) << 1) | (stereo ? 1ull << 5 : 0) | (samples << 34);
    Put32(b, static_cast<uint32_t>(mode));
    Put32(b, static_cast<uint32_t>(mode >> 32));
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

static PcmPtr Constant(int16_t value, int frames, int rate = 48000) {
    auto p = std::make_shared<Pcm>();
    p->rate = rate;
    p->channels = 1;
    p->samples.assign(static_cast<size_t>(frames), value);
    return p;
}

static void TestDecoders() {
    Pcm pcm;
    const auto wav = MakeWav({100, -200, 300, -400}, 22050, 2);
    CHECK(DecodeSound(wav, ".wav", pcm));
    CHECK(pcm.rate == 22050 && pcm.channels == 2 && pcm.Frames() == 2 && pcm.samples[1] == -200);

    std::vector<uint8_t> raw;
    for (int16_t s : {1, 2, 3, 4})
        Put16(raw, static_cast<uint16_t>(s));
    CHECK(DecodeSound(MakeFsb(2, raw, 4, 9, false), ".fsb", pcm));
    CHECK(pcm.rate == 48000 && pcm.channels == 1 && pcm.Frames() == 4 && pcm.samples[3] == 4);

    std::vector<uint8_t> frame(0x8C, 0);
    for (int i = 0; i < 8 * 4; i++) {
        frame[12 + i * 4] = 0x11;
        frame[13 + i * 4] = 0x11;
        frame[14 + i * 4] = 0x11;
        frame[15 + i * 4] = 0x11;
    }
    CHECK(DecodeSound(MakeFsb(16, frame, 256, 8, false), ".fsb", pcm));
    CHECK(pcm.Frames() == 256 && pcm.samples[0] == 1 && pcm.samples[255] == 1);
    frame[0] = 0x11;
    frame[1] = 0x11;
    frame[2] = 0x11;
    frame[3] = 0x11;
    frame[8] = 64;
    CHECK(DecodeSound(MakeFsb(16, frame, 256, 8, false), ".fsb", pcm));
    CHECK(pcm.samples[0] == ((64 + 60 * 64) >> 6));
    CHECK(!DecodeSound({1, 2, 3}, ".fsb", pcm));
    CHECK(!DecodeSound(raw, ".mp3", pcm));
    CHECK(MakeFsb(2, raw, 4, 9, false).size() > 60);

    if (const char* dir = std::getenv("PERLIN_TEST_SOUNDS")) {
        const fs::path p = fs::path(dir) / "mob/cow/say1.fsb";
        if (fs::exists(p)) {
            std::ifstream f(p, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            CHECK(DecodeSound(bytes, ".fsb", pcm));
            double e = 0, c = 0;
            for (size_t i = 0; i + 1 < pcm.samples.size(); i++) {
                e += double(pcm.samples[i]) * pcm.samples[i];
                c += double(pcm.samples[i]) * pcm.samples[i + 1];
            }
            CHECK(pcm.Frames() == 22016 && c / e > 0.95);
        }
    }
}

static float RenderPeak(AudioEngine& engine, int frames, float* left = nullptr, float* right = nullptr) {
    std::vector<float> buf(static_cast<size_t>(frames) * 2, 0.0f);
    engine.Mix(buf.data(), frames);
    float peak = 0, l = 0, r = 0;
    for (int i = 0; i < frames; i++) {
        l = std::max(l, std::fabs(buf[i * 2]));
        r = std::max(r, std::fabs(buf[i * 2 + 1]));
    }
    peak = std::max(l, r);
    if (left)
        *left = l;
    if (right)
        *right = r;
    return peak;
}

static void TestMixer() {
    {
        AudioEngine engine;
        Voice v;
        v.pcm = Constant(16384, 48000);
        v.category = Category::Music;
        engine.Start(v);
        CHECK(std::fabs(RenderPeak(engine, 480) - 0.5f) < 0.01f);
    }
    {
        AudioEngine engine;
        Volumes vol;
        vol.v[static_cast<int>(Category::Master)] = 0.5f;
        vol.v[static_cast<int>(Category::Music)] = 0.5f;
        engine.SetVolumes(vol);
        Voice v;
        v.pcm = Constant(16384, 48000);
        v.category = Category::Music;
        engine.Start(v);
        CHECK(std::fabs(RenderPeak(engine, 480) - 0.125f) < 0.005f);
        Voice w;
        w.pcm = Constant(16384, 48000);
        w.category = Category::Master;
        engine.StopAll();
        RenderPeak(engine, 2000);
        engine.Start(w);
        CHECK(std::fabs(RenderPeak(engine, 480) - 0.25f) < 0.005f);
    }
    {
        AudioEngine engine;
        engine.SetListener({0, 0, 0}, 0.0f);
        Voice v;
        v.pcm = Constant(16384, 48000);
        v.positional = true;
        v.position = {0.0, 0.0, 8.5};
        v.range = 16.0f;
        engine.Start(v);
        float l = 0, r = 0;
        RenderPeak(engine, 480, &l, &r);
        CHECK(std::fabs(AudioEngine::Attenuation(8.5f, 16.0f) - 0.5f) < 1e-5f);
        CHECK(std::fabs(l - r) < 0.02f);
        CHECK(l > 0.12f && l < 0.35f);
        AudioEngine far;
        far.SetListener({0, 0, 0}, 0.0f);
        Voice f = v;
        f.position = {0, 0, 40};
        far.Start(f);
        CHECK(RenderPeak(far, 480) < 1e-6f);
    }
    {
        AudioEngine engine;
        engine.SetListener({0, 0, 0}, 0.0f);
        Voice v;
        v.pcm = Constant(16384, 48000);
        v.positional = true;
        v.position = {-3.0, 0.0, 0.0};
        engine.Start(v);
        float l = 0, r = 0;
        RenderPeak(engine, 480, &l, &r);
        CHECK(r > l * 3.0f);
        AudioEngine back;
        back.SetListener({0, 0, 0}, 180.0f);
        back.Start(v);
        RenderPeak(back, 480, &l, &r);
        CHECK(l > r * 3.0f);
    }
    {
        AudioEngine engine;
        Voice v;
        v.pcm = Constant(8000, 4800);
        v.pitch = 2.0f;
        engine.Start(v);
        RenderPeak(engine, 1000);
        CHECK(engine.Active() == 1);
        RenderPeak(engine, 2000);
        CHECK(engine.Active() == 0);
        Voice loop;
        loop.pcm = Constant(8000, 4800);
        loop.loop = true;
        loop.name = "x";
        engine.Start(loop);
        RenderPeak(engine, 100);
        CHECK(engine.Playing("x"));
        engine.StopName("x");
        RenderPeak(engine, 2000);
        CHECK(engine.Active() == 0);
    }
}

static void Write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

static void TestSoundSystem() {
    const fs::path root = fs::temp_directory_path() / "perlin_audio_selftest";
    fs::remove_all(root);
    const auto wav = MakeWav(std::vector<int16_t>(2400, 12000), 48000, 1);
    for (const char* f : {"sounds/step/stone1.wav", "sounds/mob/cow/say1.wav", "sounds/mob/cow/say2.wav"}) {
        fs::create_directories((root / f).parent_path());
        std::ofstream(root / f, std::ios::binary)
            .write(reinterpret_cast<const char*>(wav.data()), static_cast<std::streamsize>(wav.size()));
    }
    Write(root / "sounds/sound_definitions.json", R"({"format_version":"1.14.0","sound_definitions":{
        "step.stone":{"category":"block","sounds":[{"name":"sounds/step/stone1","volume":0.5}]},
        "mob.cow.say":{"category":"neutral","max_distance":null,"sounds":["sounds/mob/cow/say1",{"name":"sounds/mob/cow/say2","weight":2}]}}})");
    Write(
        root / "sounds.json",
        R"({"block_sounds":{"stone":{"events":{"break":{"sound":"break.stone","volume":1.0},"item.use.on":{"sound":"step.stone"}}}},
        "interactive_sounds":{"block_sounds":{"stone":{"events":{"step":{"sound":"step.stone","volume":0.5,"pitch":[0.9,1.1]}}}}},
        "entity_sounds":{"defaults":{"events":{"hurt":"mob.generic.hurt"}},"entities":{"cow":{"pitch":0.6,"events":{"ambient":"mob.cow.say","step":{"sound":"mob.cow.step"}}}}},
        "individual_named_sounds":{"sounds":{"cow.moo":{"sound":"mob.cow.say","volume":0.4}}}})");
    Write(root / "blocks.json", R"({"stone":{"sound":"stone","textures":"stone"}})");

    AudioEngine engine;
    SoundSystem sounds(engine);
    std::string lastSubtitle;
    bool lastGlobal = false;
    sounds.SetSubtitleHook([&](const std::string& s, const glm::dvec3&, float, bool g) {
        lastSubtitle = s;
        lastGlobal = g;
    });
    sounds.SetBlockNamer([](int32_t id) { return id == 7 ? std::string("minecraft:stone") : std::string(); });
    sounds.Configure(root.string());
    CHECK(sounds.HasSounds() && sounds.DefinitionCount() == 2);
    CHECK(EventKey("minecraft:item_use_on") == EventKey("ItemUseOn") &&
          EventKey("item.use.on") == "itemuseon");
    CHECK(sounds.BlockSoundType("minecraft:stone") == "stone");
    sounds.LoadBlocksText(R"({"custom:stone":{"sound":"wood"}})");
    CHECK(sounds.BlockSoundType("custom:stone") == "wood");
    CHECK(sounds.BlockSoundType("minecraft:stone") == "stone");

    CHECK(sounds.PlayLevelSound("Step", {1, 2, 3}, 7, "", false, false));
    CHECK(engine.Active() == 1 && lastSubtitle == "step.stone");
    CHECK(SubtitleText(lastSubtitle) == "Footsteps");
    CHECK(sounds.PlayLevelSound("minecraft:item_use_on", {0, 0, 0}, 7, "", false, false));
    CHECK(!sounds.PlayLevelSound("break", {0, 0, 0}, 7, "", false, false));
    CHECK(sounds.PlayLevelSound("ambient", {0, 0, 0}, 0, "minecraft:cow", false, false));
    CHECK(lastSubtitle == "mob.cow.say" && SubtitleText(lastSubtitle) == "Cow moos");
    CHECK(sounds.PlayNamed("cow.moo", {0, 0, 0}, 1.0f, 1.0f));
    CHECK(sounds.PlayNamed("mob.cow.say", {0, 0, 0}, 1.0f, 1.0f, false) && lastGlobal);
    CHECK(!sounds.PlayNamed("does.not.exist", {0, 0, 0}, 1.0f, 1.0f));
    CHECK(sounds.PlayNamed("step.stone", {0, 0, 0}, 1.0f, 1.0f));
    // Repeated requests are valid events, not an automatic 250 ms mute.
    CHECK(sounds.PlayNamed("step.stone", {1, 0, 0}, 1.0f, 1.0f));
    EventSound cow;
    CHECK(sounds.FindEntityEvent("minecraft:cow", "ambient", cow));
    CHECK(std::fabs(cow.pitchLo - 0.6f) < 1e-6f);
    sounds.LoadDefinitionsText(R"({"sound_definitions":{
        "random.burp":{"sounds":["sounds/step/stone1"]},
        "random.totem":{"sounds":["sounds/step/stone1"]}}})");
    CHECK(sounds.PlayLevelSound("Burp", {}, 0, "", false, false));
    CHECK(lastSubtitle == "random.burp");
    CHECK(sounds.PlayLevelSound("TotemUsed", {}, 0, "", false, false));
    CHECK(lastSubtitle == "random.totem");

    const fs::path low = root / "low", high = root / "high";
    for (const auto& pack : {low, high}) {
        Write(pack / "sounds/sound_definitions.json", R"({"sound_definitions":{
            "pack.test":{"category":"block","sounds":["sounds/custom/tone"]}}})");
        fs::create_directories(pack / "sounds/custom");
        const auto tone = MakeWav(std::vector<int16_t>(2400, pack == high ? 8000 : 4000), 48000, 1);
        std::ofstream(pack / "sounds/custom/tone.wav", std::ios::binary)
            .write(reinterpret_cast<const char*>(tone.data()), static_cast<std::streamsize>(tone.size()));
    }
    AudioEngine layeredEngine;
    SoundSystem layered(layeredEngine);
    layered.Configure(root.string(), {high.string(), low.string()});
    CHECK(layered.DefinitionCount() == 3); // vanilla plus custom definition
    CHECK(layered.PlayNamed("pack.test", {}, 1, 1, false));
    CHECK(std::fabs(RenderPeak(layeredEngine, 2400) - 8000.0f / 32768.0f) < 1e-5f);
    // Changing the active pack list must invalidate decoded samples.
    layered.Configure(root.string(), {low.string()});
    CHECK(layered.PlayNamed("pack.test", {}, 1, 1, false));
    CHECK(std::fabs(RenderPeak(layeredEngine, 2400) - 4000.0f / 32768.0f) < 1e-5f);
    layered.Configure(root.string());
    CHECK(!layered.PlayNamed("pack.test", {}, 1, 1, false));
    fs::remove_all(root);
}

static void TestSubtitles() {
    CHECK(SubtitleText("mob.zombie.say") == "Zombie groans");
    CHECK(SubtitleText("mob.polar_bear.hurt") == "Polar Bear hurts");
    CHECK(SubtitleText("mob.pig.death") == "Pig dies");
    CHECK(SubtitleText("dig.stone") == "Block broken");
    CHECK(SubtitleText("random.explode") == "Explosion");
    CHECK(SubtitleText("music.game").empty());

    Subtitles s;
    s.Add("mob.cow.say", {10, 0, 0}, 1.0f, false, 0.0);
    s.Add("random.explode", {0, 0, 5}, 1.0f, false, 0.5);
    s.Add("mob.cow.say", {10, 0, 0}, 1.0f, false, 1.0);
    CHECK(s.Count() == 2);
    auto lines = s.Visible({0, 0, 0}, 0.0f, 1.2);
    CHECK(lines.size() == 2);
    for (const auto& l : lines) {
        if (l.text == "Cow moos")
            CHECK(l.direction == -1);
        if (l.text == "Explosion")
            CHECK(l.direction == 0);
        CHECK(l.brightness <= 1.0f && l.brightness >= 75.0f / 255.0f);
    }
    s.Add("mob.zombie.say", {100, 0, 0}, 1.0f, false, 1.2);
    CHECK(s.Visible({0, 0, 0}, 0.0f, 1.3).size() == 2);
    CHECK(s.Visible({0, 0, 0}, 0.0f, 4.6).empty());
}

static void LoadLanguage() {
    std::ifstream f(PERLIN_LANG_FILE, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    ss << "\nsubtitles.block.generic.footsteps=Footsteps\nsubtitles.block.generic.break=Block broken\n"
          "subtitles.entity.cow.ambient=Cow moos\nsubtitles.entity.zombie.ambient=Zombie groans\n"
          "subtitles.entity.polar_bear.hurt=Polar Bear hurts\nsubtitles.entity.pig.death=Pig dies\n"
          "subtitles.entity.generic.explode=Explosion\nentity.polar_bear.name=Polar Bear\n";
    perlin::i18n::Lang::Get().LoadFromText(ss.str());
}

int main() {
    LoadLanguage();
    TestDecoders();
    TestMixer();
    TestSoundSystem();
    TestSubtitles();
    if (failures == 0)
        std::cout << "audio_selftest ok\n";
    return failures == 0 ? 0 : 1;
}
