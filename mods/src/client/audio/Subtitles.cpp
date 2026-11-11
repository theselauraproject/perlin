#include "client/audio/Subtitles.hpp"

#include "client/i18n/Lang.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace perlin::audio {
    namespace {
        std::string Sentence(std::string s) {
            for (char& c : s)
                if (c == '_')
                    c = ' ';
            if (!s.empty())
                s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
            return s;
        }

        bool StartsWith(const std::string& s, const char* prefix) {
            return s.starts_with(prefix);
        }
        bool Contains(const std::string& s, const char* part) {
            return s.contains(part);
        }

        std::vector<std::string> Split(const std::string& s) {
            std::vector<std::string> parts;
            size_t start = 0;
            while (true) {
                const size_t dot = s.find('.', start);
                parts.push_back(s.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
                if (dot == std::string::npos)
                    break;
                start = dot + 1;
            }
            return parts;
        }

        std::string T(const std::string& key) {
            return i18n::Tr(key);
        }
        std::string T(const std::string& key, const std::vector<std::string>& args) {
            return i18n::Tr(key, args);
        }

        std::string MobName(const std::string& mob) {
            std::string out;
            if (i18n::Lang::Get().Find("entity." + mob + ".name", out))
                return out;
            if (mob == "endermen" && i18n::Lang::Get().Find("entity.enderman.name", out))
                return out;
            return Sentence(mob);
        }
    }

    std::string SubtitleText(const std::string& name) {
        i18n::Lang& lang = i18n::Lang::Get();
        const auto first = [&](std::initializer_list<std::string> keys) -> std::string {
            for (const std::string& k : keys)
                if (lang.Has(k))
                    return T(k);
            return {};
        };
        const std::vector<std::string> parts = Split(name);
        if (parts.empty())
            return {};
        const std::string& p0 = parts[0];
        if (p0 == "step" || p0 == "fall" || p0 == "land" || p0 == "jump")
            return first({"subtitles.block.generic.footsteps"});
        if (p0 == "dig" || p0 == "break")
            return first({"subtitles.block.generic.break"});
        if (p0 == "place")
            return first({"subtitles.block.generic.place"});
        if (p0 == "hit")
            return first({"subtitles.block.generic.hit"});
        if (p0 == "use")
            return first({"subtitles.block.generic.use", "subtitles.block.generic.place"});

        static const std::map<std::string, std::string> kMap = {
            {"random.explode", "entity.generic.explode"},
            {"random.pop", "entity.item.pickup"},
            {"random.pop2", "entity.item.pickup"},
            {"random.orb", "entity.experience_orb.pickup"},
            {"random.levelup", "entity.player.levelup"},
            {"random.burp", "entity.player.burp"},
            {"random.eat", "entity.generic.eat"},
            {"random.drink", "entity.generic.drink"},
            {"random.splash", "entity.generic.splash"},
            {"random.swim", "entity.generic.swim"},
            {"random.fizz", "entity.generic.extinguish_fire"},
            {"random.bow", "entity.arrow.shoot"},
            {"random.bowhit", "entity.arrow.hit"},
            {"random.break", "entity.item.break"},
            {"random.chestopen", "block.chest.open"},
            {"random.chestclosed", "block.chest.close"},
            {"random.enderchestopen", "block.ender_chest.open"},
            {"random.enderchestclosed", "block.ender_chest.close"},
            {"random.shulkerbox.open", "block.shulker_box.open"},
            {"random.shulkerbox.close", "block.shulker_box.close"},
            {"random.door_open", "block.wooden_door.open"},
            {"random.door_close", "block.wooden_door.close"},
            {"random.glass", "block.glass.break"},
            {"random.anvil_use", "block.anvil.use"},
            {"random.anvil_land", "block.anvil.land"},
            {"random.anvil_break", "block.anvil.destroy"},
            {"random.toast", "ui.toast.challenge_complete"},
            {"random.totem", "item.totem.use"},
            {"random.lever_click", "block.lever.click"},
            {"random.click", "block.wooden_button.click_on"},
            {"game.player.hurt", "entity.player.hurt"},
            {"game.player.die", "entity.player.death"},
            {"game.player.attack.strong", "entity.player.attack.strong"},
            {"game.player.attack.nodamage", "entity.player.attack.weak"},
            {"damage.hit", "entity.player.hurt"},
            {"damage.fallbig", "entity.generic.big_fall"},
            {"damage.fallsmall", "entity.generic.small_fall"},
            {"fire.fire", "block.fire.ambient"},
            {"fire.ignite", "item.flintandsteel.use"},
            {"liquid.water", "block.water.ambient"},
            {"liquid.lava", "block.lava.ambient"},
            {"liquid.lavapop", "block.lava.pop"},
            {"ambient.weather.thunder", "entity.lightning_bolt.thunder"},
            {"ambient.weather.lightning.impact", "entity.lightning_bolt.impact"},
            {"ambient.weather.rain", "weather.rain"},
            {"ambient.cave", "ambient.cave"},
            {"portal.portal", "block.portal.ambient"},
            {"portal.travel", "block.portal.travel"},
            {"portal.trigger", "block.portal.trigger"},
            {"mob.enderdragon.growl", "entity.ender_dragon.growl"},
            {"mob.enderdragon.flap", "entity.ender_dragon.flap"},
            {"mob.enderdragon.death", "entity.ender_dragon.death"},
            {"mob.enderdragon.hit", "entity.ender_dragon.hurt"},
            {"mob.endermen.portal", "entity.enderman.teleport"},
            {"mob.endermen.scream", "entity.enderman.scream"},
            {"mob.endermen.stare", "entity.enderman.stare"},
            {"mob.creeper.say", "entity.creeper.hurt"},
            {"mob.tnt.fuse", "entity.tnt.primed"},
            {"item.trident.hit", "item.trident.hit"},
            {"item.trident.throw", "item.trident.throw"},
            {"item.trident.return", "item.trident.return"},
            {"item.shield.block", "item.shield.block"},
            {"bucket.fill_water", "item.bucket.fill"},
            {"bucket.fill_lava", "item.bucket.fill"},
            {"bucket.empty_water", "item.bucket.empty"},
            {"bucket.empty_lava", "item.bucket.empty"},
            {"elytra.loop", "item.elytra.flying"},
            {"firework.launch", "entity.firework_rocket.launch"},
            {"firework.blast", "entity.firework_rocket.blast"},
            {"firework.large_blast", "entity.firework_rocket.blast"},
            {"firework.twinkle", "entity.firework_rocket.twinkle"},
        };
        if (const auto it = kMap.find(name); it != kMap.end())
            if (const std::string t = first({"subtitles." + it->second}); !t.empty())
                return t;
        if (StartsWith(name, "armor.equip"))
            return first({"subtitles.item.armor." + parts[1], "subtitles.item.armor.equip"});
        if (p0 == "note")
            return first({"subtitles.block.note_block.note"});
        if (name == "mob.wither.spawn" || name == "mob.wither.shoot" || name == "mob.wither.death" ||
            name == "mob.wither.hurt" || name == "mob.wither.ambient")
            if (const std::string t = first(
                    {"subtitles.entity.wither." + std::string(parts[2] == "ambient" ? "ambient" : parts[2])});
                !t.empty())
                return t;

        if (p0 == "mob" && parts.size() >= 3) {
            std::string mob = parts[1];
            static const std::map<std::string, std::string> kMob = {
                {"endermen", "enderman"},
                {"zombiepigman", "zombified_piglin"},
                {"zombie_pigman", "zombified_piglin"},
                {"snowgolem", "snow_golem"},
                {"irongolem", "iron_golem"},
                {"villager", "villager"},
                {"magmacube", "magma_cube"},
                {"evocation_illager", "evoker"},
                {"horse", "horse"},
            };
            if (const auto it = kMob.find(mob); it != kMob.end())
                mob = it->second;
            std::string event = parts[2];
            const bool baby = event.size() > 5 && event.compare(event.size() - 5, 5, "_baby") == 0;
            if (baby)
                event.resize(event.size() - 5);
            if (event == "say" || event == "idle")
                event = "ambient";
            else if (event == "hit")
                event = "hurt";
            else if (event == "walk" || event == "stomp")
                event = "step";
            else if (event == "eating")
                event = "eat";
            else if (event == "fireball" || event == "bite")
                event = event == "bite" ? "attack" : "shoot";
            else if (event == "primed")
                event = "primed";
            const std::string who = MobName(mob);
            const std::string t = first({baby ? "subtitles.entity.baby_" + mob + "." + event : std::string(),
                                         "subtitles.entity." + mob + "." + event});
            if (!t.empty())
                return t;
            if (event == "step")
                return first({"subtitles.block.generic.footsteps"});
            if (event == "hurt")
                return first({"subtitles.entity.generic.hurt"});
            if (event == "death")
                return first({"subtitles.entity.generic.death"});
            static const std::map<std::string, std::string> kEnglish = {{"ambient", "makes a noise"},
                                                                        {"angry", "growls"},
                                                                        {"shoot", "shoots"},
                                                                        {"attack", "attacks"},
                                                                        {"eat", "eats"},
                                                                        {"spawn", "appears"},
                                                                        {"pop", "pops"},
                                                                        {"plop", "pops"},
                                                                        {"fuse", "fuses"},
                                                                        {"primed", "fuses"}};
            const auto it = kEnglish.find(event);
            return who + " " + (it != kEnglish.end() ? it->second : std::string("makes a noise"));
        }
        return {};
    }

    void Subtitles::Add(const std::string& sound, const glm::dvec3& position, float volume, bool global,
                        double now) {
        const std::string text = SubtitleText(sound);
        if (text.empty() || volume <= 0.0f)
            return;
        for (Entry& e : entries)
            if (e.text == text) {
                e.position = position;
                e.time = now;
                e.volume = std::max(e.volume, volume);
                e.global = global;
                return;
            }
        if (entries.size() >= 12)
            entries.erase(entries.begin());
        entries.push_back({text, position, now, volume, global});
    }

    std::vector<Subtitles::Line> Subtitles::Visible(const glm::dvec3& listener, float yawDegrees,
                                                    double now) {
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const Entry& e) { return now - e.time > kLifetime; }),
                      entries.end());
        const double yaw = static_cast<double>(yawDegrees) * 3.14159265358979323846 / 180.0;
        const glm::dvec2 forward(-std::sin(yaw), std::cos(yaw));
        const glm::dvec2 right(-std::cos(yaw), -std::sin(yaw));
        std::vector<Line> lines;
        for (const Entry& e : entries) {
            Line line;
            line.text = e.text;
            line.age = static_cast<float>(now - e.time);
            line.brightness = static_cast<float>(
                (255.0 - (255.0 - 75.0) * std::clamp((now - e.time) / kLifetime, 0.0, 1.0)) / 255.0);
            if (!e.global) {
                const glm::dvec3 d = e.position - listener;
                const double dist = glm::length(d);
                if (dist > 16.0 * std::max(1.0f, e.volume))
                    continue;
                if (dist > 1e-3) {
                    const double front = (d.x * forward.x + d.z * forward.y) / dist;
                    const double side = (d.x * right.x + d.z * right.y) / dist;
                    if (front <= 0.5)
                        line.direction = side > 0.0 ? 1 : -1;
                }
            }
            lines.push_back(std::move(line));
        }
        return lines;
    }
}
