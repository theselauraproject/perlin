#include "client/audio/SoundSystem.hpp"
#include "util/Env.hpp"

#include "client/ui/Json.hpp"
#include "util/FileIo.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include "util/Log.hpp"

namespace perlin::audio {
    namespace fs = std::filesystem;
    using Json = ui::Json;

    std::string EventKey(const std::string& id) {
        std::string s = id;
        const size_t colon = s.find(':');
        if (colon != std::string::npos)
            s = s.substr(colon + 1);
        std::string out;
        for (char c : s)
            if (std::isalnum(static_cast<unsigned char>(c)))
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return out;
    }

    namespace {

        void Range(const Json* j, float& lo, float& hi, float fallback) {
            lo = hi = fallback;
            if (!j)
                return;
            if (j->IsNumber())
                lo = hi = static_cast<float>(j->number);
            else if (j->IsArray() && j->array.size() >= 2) {
                lo = static_cast<float>(j->array[0].NumberOr(fallback));
                hi = static_cast<float>(j->array[1].NumberOr(fallback));
            }
        }

        EventSound ParseEvent(const Json& j) {
            EventSound e;
            if (j.IsString()) {
                e.sound = j.string;
            } else if (j.IsObject()) {
                if (const Json* s = j.Find("sound"); s && s->IsString())
                    e.sound = s->string;
                Range(j.Find("volume"), e.volumeLo, e.volumeHi, 1.0f);
                Range(j.Find("pitch"), e.pitchLo, e.pitchHi, 1.0f);
            }
            return e;
        }

        void ParseEvents(const Json& events, std::unordered_map<std::string, EventSound>& out,
                         float volumeScale, float pitchScale) {
            if (!events.IsObject())
                return;
            for (const auto& [key, value] : events.object) {
                EventSound e = ParseEvent(value);
                if (!e.Valid() || e.sound.starts_with("missing."))
                    continue;
                e.volumeLo *= volumeScale;
                e.volumeHi *= volumeScale;
                e.pitchLo *= pitchScale;
                e.pitchHi *= pitchScale;
                out[EventKey(key)] = e;
            }
        }

        std::string ShortName(const std::string& id) {
            const size_t colon = id.find(':');
            return colon == std::string::npos ? id : id.substr(colon + 1);
        }
    }

    void SoundSystem::LoadDefinitionsText(const std::string& text) {
        Json doc;
        std::string error;
        if (!Json::Parse(text, doc, error))
            return;
        const Json* list = doc.Find("sound_definitions");
        if (!list || !list->IsObject())
            return;
        for (const auto& [name, def] : list->object) {
            if (!def.IsObject())
                continue;
            SoundDefinition d;
            if (const Json* c = def.Find("category"); c && c->IsString())
                d.category = c->string;
            if (const Json* m = def.Find("min_distance"); m && m->IsNumber())
                d.minDistance = static_cast<float>(m->number);
            if (const Json* m = def.Find("max_distance"); m && m->IsNumber())
                d.maxDistance = static_cast<float>(m->number);
            if (const Json* sounds = def.Find("sounds"); sounds && sounds->IsArray())
                for (const Json& s : sounds->array) {
                    SoundDefinition::Entry e;
                    if (s.IsString())
                        e.file = s.string;
                    else if (s.IsObject()) {
                        if (const Json* n = s.Find("name"); n && n->IsString())
                            e.file = n->string;
                        if (const Json* v = s.Find("volume"); v && v->IsNumber())
                            e.volume = static_cast<float>(v->number);
                        Range(s.Find("pitch"), e.pitchLo, e.pitchHi, 1.0f);
                        if (const Json* w = s.Find("weight"); w && w->IsNumber())
                            e.weight = std::max(1.0f, static_cast<float>(w->number));
                    }
                    if (!e.file.empty())
                        d.entries.push_back(std::move(e));
                }
            definitions[name] = std::move(d);
        }
    }

    void SoundSystem::LoadSoundsText(const std::string& text) {
        Json doc;
        std::string error;
        if (!Json::Parse(text, doc, error))
            return;
        const auto blocks = [&](const Json* group) {
            if (!group || !group->IsObject())
                return;
            for (const auto& [type, def] : group->object) {
                if (!def.IsObject())
                    continue;
                float vol = 1.0f, pitch = 1.0f;
                if (const Json* v = def.Find("volume"); v && v->IsNumber())
                    vol = static_cast<float>(v->number);
                if (const Json* p = def.Find("pitch"); p && p->IsNumber())
                    pitch = static_cast<float>(p->number);
                if (const Json* ev = def.Find("events"))
                    ParseEvents(*ev, blockEvents[type], vol, pitch);
            }
        };
        blocks(doc.Find("block_sounds"));
        if (const Json* interactive = doc.Find("interactive_sounds"))
            blocks(interactive->Find("block_sounds"));
        if (const Json* es = doc.Find("entity_sounds")) {
            auto defaults = entityEvents["*"];
            if (const Json* d = es->Find("defaults"); d && d->IsObject())
                if (const Json* ev = d->Find("events"))
                    ParseEvents(*ev, defaults, 1.0f, 1.0f);
            if (const Json* ents = es->Find("entities"); ents && ents->IsObject())
                for (const auto& [type, def] : ents->object) {
                    if (!def.IsObject())
                        continue;
                    float vol = 1.0f, pitch = 1.0f;
                    if (const Json* v = def.Find("volume"); v && v->IsNumber())
                        vol = static_cast<float>(v->number);
                    if (const Json* p = def.Find("pitch"); p && p->IsNumber())
                        pitch = static_cast<float>(p->number);
                    auto& dest = entityEvents[ShortName(type)];
                    if (const Json* variants = def.Find("variants"))
                        if (const Json* map = variants->Find("map"))
                            if (const Json* dv = map->Find("default"))
                                if (const Json* ev = dv->Find("events"))
                                    ParseEvents(*ev, dest, vol, pitch);
                    if (const Json* ev = def.Find("events"))
                        ParseEvents(*ev, dest, vol, pitch);
                    for (const auto& [k, e] : defaults)
                        dest.emplace(k, e);
                }
            entityEvents["*"] = defaults;
        }
        for (const char* section : {"individual_event_sounds", "individual_named_sounds"})
            if (const Json* s = doc.Find(section)) {
                const Json* list = s->Find("events");
                if (!list)
                    list = s->Find("sounds");
                if (list && list->IsObject())
                    for (const auto& [name, value] : list->object) {
                        EventSound e = ParseEvent(value);
                        if (e.Valid())
                            named[name] = e;
                    }
            }
    }

    void SoundSystem::LoadBlocksText(const std::string& text) {
        Json doc;
        std::string error;
        if (!Json::Parse(text, doc, error) || !doc.IsObject())
            return;
        for (const auto& [name, def] : doc.object)
            if (def.IsObject())
                if (const Json* s = def.Find("sound"); s && s->IsString())
                    blockSoundType[name] = s->string;
    }

    void SoundSystem::Configure(const std::string& newRoot, const std::vector<std::string>& packs) {
        if (configured && root == newRoot && packRoots == packs)
            return;
        configured = true;
        root = newRoot;
        packRoots = packs;
        warned.clear();
        definitions.clear();
        blockEvents.clear();
        entityEvents.clear();
        named.clear();
        blockSoundType.clear();
        cache.clear();
        cacheBytes = 0;
        if (root.empty())
            return;
        const auto loadRoot = [&](const std::string& directory) {
            std::string text;
            if (file::Read(fs::path(directory) / "sounds" / "sound_definitions.json", text))
                LoadDefinitionsText(text);
            if (file::Read(fs::path(directory) / "sounds.json", text))
                LoadSoundsText(text);
            if (file::Read(fs::path(directory) / "blocks.json", text))
                LoadBlocksText(text);
        };
        loadRoot(root);
        // Directories are highest priority first, as for textures and JSON UI.
        for (auto it = packRoots.rbegin(); it != packRoots.rend(); ++it)
            loadRoot(*it);
        Log("audio", "{} sound definitions, {} block sound types, {} entities with sounds",
            definitions.size(), blockEvents.size(), entityEvents.size());
    }

    std::string SoundSystem::BlockSoundType(const std::string& blockName) const {
        if (const auto exact = blockSoundType.find(blockName); exact != blockSoundType.end())
            return exact->second;
        const auto it = blockSoundType.find(ShortName(blockName));
        return it == blockSoundType.end() ? std::string() : it->second;
    }

    bool SoundSystem::FindBlockEvent(const std::string& soundType, const std::string& event,
                                     EventSound& out) const {
        const auto t = blockEvents.find(soundType);
        if (t == blockEvents.end())
            return false;
        const auto e = t->second.find(EventKey(event));
        if (e == t->second.end())
            return false;
        out = e->second;
        return true;
    }

    bool SoundSystem::FindEntityEvent(const std::string& entityType, const std::string& event,
                                      EventSound& out) const {
        const std::string key = EventKey(event);
        const auto t = entityEvents.find(ShortName(entityType));
        if (t != entityEvents.end()) {
            if (const auto e = t->second.find(key); e != t->second.end()) {
                out = e->second;
                return true;
            }
            return false;
        }
        const auto d = entityEvents.find("*");
        if (d == entityEvents.end())
            return false;
        const auto e = d->second.find(key);
        if (e == d->second.end())
            return false;
        out = e->second;
        return true;
    }

    float SoundSystem::Roll(float lo, float hi) {
        if (hi <= lo)
            return lo;
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    }

    PcmPtr SoundSystem::Load(const std::string& file) {
        if (const auto it = cache.find(file); it != cache.end())
            return it->second;
        PcmPtr result;
        std::vector<std::string> roots = packRoots;
        roots.push_back(root);
        bool found = false;
        for (const auto& directory : roots) {
            for (const char* ext : {".fsb", ".ogg", ".wav"}) {
                const fs::path p = fs::path(directory) / (file + ext);
                std::error_code ec;
                if (!fs::exists(p, ec))
                    continue;
                found = true;
                std::ifstream f(p, std::ios::binary);
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                           std::istreambuf_iterator<char>());
                auto pcm = std::make_shared<Pcm>();
                std::string error;
                if (DecodeSound(bytes, ext, *pcm, &error)) {
                    result = pcm;
                    cacheBytes += pcm->samples.size() * sizeof(int16_t);
                } else if (!warned[file]) {
                    warned[file] = true;
                    Log("audio", "cannot decode {}{}: {}", file, ext, error);
                }
                break;
            }
            if (found)
                break;
        }
        if (!result && !warned[file]) {
            warned[file] = true;
            Log("audio", "sound file {} not found", file);
        }
        if (cacheBytes > 192u * 1024u * 1024u) {
            cache.clear();
            cacheBytes = 0;
        }
        cache[file] = result;
        return result;
    }

    bool SoundSystem::Play(const std::string& definition, float volume, float pitch,
                           const glm::dvec3& position, bool positional, const EventSound* event) {
        const auto it = definitions.find(definition);
        if (it == definitions.end() || it->second.entries.empty()) {
            if (!warned["def:" + definition]) {
                warned["def:" + definition] = true;
                if (PERLIN_ENV("PERLIN_AUDIO_DEBUG"))
                    Log("audio", "no sound definition {}", definition);
            }
            return false;
        }
        const SoundDefinition& def = it->second;
        float total = 0;
        for (const auto& e : def.entries)
            total += e.weight;
        float r = std::uniform_real_distribution<float>(0.0f, total)(rng);
        const SoundDefinition::Entry* chosen = &def.entries.back();
        for (const auto& e : def.entries) {
            if (r < e.weight) {
                chosen = &e;
                break;
            }
            r -= e.weight;
        }
        Voice voice;
        voice.pcm = Load(chosen->file);
        if (!voice.pcm)
            return false;
        float eventVolume = 1.0f, eventPitch = 1.0f;
        if (event) {
            eventVolume = Roll(event->volumeLo, event->volumeHi);
            eventPitch = Roll(event->pitchLo, event->pitchHi);
        }
        voice.volume = std::max(0.0f, volume * eventVolume * chosen->volume);
        voice.pitch = std::clamp(pitch * eventPitch * Roll(chosen->pitchLo, chosen->pitchHi), 0.1f, 4.0f);
        voice.category = CategoryFor(def.category, definition);
        voice.positional =
            positional && voice.category != Category::Music && voice.category != Category::Record;
        voice.position = position;
        voice.range = std::max(16.0f * std::max(volume * eventVolume * chosen->volume, 1.0f),
                               def.maxDistance > 0 ? def.maxDistance : 0.0f);
        voice.loop = false;
        voice.name = definition;
        if (voice.volume <= 0.0f)
            return false;
        if (engine.Start(std::move(voice)) == 0)
            return false;
        if (subtitles)
            subtitles(definition, position, volume * eventVolume, !positional);
        return true;
    }

    bool SoundSystem::PlayNamed(const std::string& name, const glm::dvec3& position, float volume,
                                float pitch, bool positional) {
        if (const auto it = named.find(name); it != named.end() && definitions.contains(it->second.sound))
            return Play(it->second.sound, volume, pitch, position, positional, &it->second);
        return Play(name, volume, pitch, position, positional);
    }

    bool SoundSystem::PlayBlockEvent(const std::string& blockName, const std::string& event,
                                     const glm::dvec3& position, float volumeScale) {
        const std::string type = BlockSoundType(blockName);
        if (type.empty())
            return false;
        EventSound e;
        if (!FindBlockEvent(type, event, e))
            return false;
        return Play(e.sound, volumeScale, 1.0f, position, true, &e);
    }

    bool SoundSystem::PlayEntityEvent(const std::string& entityType, const std::string& event,
                                      const glm::dvec3& position, bool baby) {
        EventSound e;
        if (!FindEntityEvent(entityType, event, e))
            return false;
        if (baby && EventKey(event) != "step") {
            e.pitchLo *= 1.4f;
            e.pitchHi *= 1.4f;
        }
        return Play(e.sound, 1.0f, 1.0f, position, true, &e);
    }

    bool SoundSystem::PlayLevelSound(const std::string& eventId, const glm::dvec3& position, int32_t extra,
                                     const std::string& entityType, bool baby, bool global) {
        const std::string key = EventKey(eventId);
        if (key.empty() || key == "undefined")
            return false;
        const bool positional = !global;
        static const std::unordered_map<std::string, const char*> kDirect = {
            {"explode", "random.explode"},
            {"thunder", "ambient.weather.thunder"},
            {"lightningimpact", "ambient.weather.lightning.impact"},
            {"bow", "random.bow"},
            {"bowhit", "random.bowhit"},
            {"levelup", "random.levelup"},
            {"pop", "random.pop"},
            {"burp", "random.burp"},
            {"splash", "random.splash"},
            {"swim", "random.swim"},
            {"fizz", "random.fizz"},
            {"attacknodamage", "game.player.attack.nodamage"},
            {"attackstrong", "game.player.attack.strong"},
            {"drink", "random.drink"},
            {"eat", "random.eat"},
            {"throw", "random.bow"},
            {"glass", "random.glass"},
            {"click", "random.click"},
            {"orb", "random.orb"},
            {"totemused", "random.totem"},
            {"fire", "fire.fire"},
            {"ignite", "fire.ignite"},
            {"teleport", "mob.endermen.portal"},
            {"portal", "portal.portal"},
            {"pop2", "random.pop2"},
            {"anvilbreak", "random.anvil_break"},
            {"anvilland", "random.anvil_land"},
            {"anvilused", "random.anvil_use"},
            {"chestopen", "random.chestopen"},
            {"chestclosed", "random.chestclosed"},
            {"dooropen", "random.door_open"},
            {"doorclose", "random.door_close"},
            {"shulkerboxopen", "random.shulkerbox.open"},
            {"shulkerboxclose", "random.shulkerbox.close"},
            {"enderchestopen", "random.enderchestopen"},
            {"enderchestclosed", "random.enderchestclosed"},
            {"hitnodamage", "game.player.hurt"},
            {"hurt", "game.player.hurt"}};
        if (!entityType.empty() && entityType != "minecraft:") {
            if (key == "step" || key == "land" || key == "jump" || key == "fall" || key == "heavystep") {
                EventSound own;
                if (FindEntityEvent(entityType, "step", own) && key == "step" &&
                    entityType != "minecraft:player")
                    return Play(own.sound, 1.0f, baby ? 1.3f : 1.0f, position, positional, &own);
                if (extra != 0 && blockNamer)
                    return PlayBlockEvent(blockNamer(extra), key == "heavystep" ? "step" : key, position);
            }
            if (PlayEntityEvent(entityType, key, position, baby))
                return true;
        }
        const bool blockEvent = key == "break" || key == "place" || key == "hit" || key == "itemuseon" ||
                                key == "step" || key == "land" || key == "jump" || key == "fall" ||
                                key == "heavystep" || key == "dooropen" || key == "doorclose" ||
                                key == "poweron" || key == "poweroff";
        if (blockEvent && extra != 0 && blockNamer) {
            if (PlayBlockEvent(blockNamer(extra), key == "heavystep" ? "step" : key, position))
                return true;
        }
        if (const auto it = kDirect.find(key); it != kDirect.end())
            return Play(it->second, 1.0f, 1.0f, position, positional);
        return Play(eventId, 1.0f, 1.0f, position, positional);
    }
}
