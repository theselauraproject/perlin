#include "client/systems/sprites/ItemDisplay.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "util/Json.hpp"
#include "util/ResourcePaths.hpp"
#include "util/Resources.hpp"

namespace perlin::itemdisplay {
    namespace {
        struct Table {
            std::unordered_map<std::string, Entry> rows[4];
            Entry missing;
        };

        Transform ReadTransform(json::View v) {
            Transform t;
            if (!v.isObject())
                return t;
            t.present = true;
            const auto vec = [](json::View a, glm::vec3 fallback) {
                if (!a.isArray() || a.size() < 3)
                    return fallback;
                return glm::vec3(static_cast<float>(a[0].real()), static_cast<float>(a[1].real()),
                                 static_cast<float>(a[2].real()));
            };
            t.rotation = vec(v["rotation"], glm::vec3(0.0f));
            t.translation = vec(v["translation"], glm::vec3(0.0f));
            t.scale = vec(v["scale"], glm::vec3(1.0f));
            return t;
        }

        std::string Bare(std::string s) {
            if (const size_t colon = s.find(':'); colon != std::string::npos)
                s.erase(0, colon + 1);
            return s;
        }

        Entry Build(json::View row, const std::vector<json::View>& styles) {
            Entry e;
            if (!row.isArray() || row.size() < 3)
                return e;
            const std::string kind = row[0].str();
            if (kind == "flat") {
                e.kind = Entry::Kind::Flat;
                e.frontLight = true;
            } else if (kind == "block") {
                e.kind = Entry::Kind::Block;
            } else {
                e.kind = Entry::Kind::Special;
                e.special = kind.starts_with("special:") ? kind.substr(8) : kind;
            }
            e.model = row[2].str();
            const size_t index = static_cast<size_t>(row[1].integer(-1));
            if (index < styles.size()) {
                const json::View s = styles[index];
                e.firstRight = ReadTransform(s["firstperson_righthand"]);
                e.firstLeft = ReadTransform(s["firstperson_lefthand"]);
                e.thirdRight = ReadTransform(s["thirdperson_righthand"]);
                e.thirdLeft = ReadTransform(s["thirdperson_lefthand"]);
                e.ground = ReadTransform(s["ground"]);
                e.head = ReadTransform(s["head"]);
                e.fixed = ReadTransform(s["fixed"]);
                e.gui = ReadTransform(s["gui"]);
                // Tables written before the generator resolved left hands per file.
                if (!e.firstLeft.present)
                    e.firstLeft = e.firstRight;
                if (!e.thirdLeft.present)
                    e.thirdLeft = e.thirdRight;
            }
            if (row.size() > 3 && row[3].isObject()) {
                const json::View extra = row[3];
                const json::View light = extra["light"];
                if (light.isString())
                    e.frontLight = light.str() == "front";
                const json::View local = extra["local"];
                if (local.isArray() && local.size() == 16) {
                    float m[16];
                    for (size_t i = 0; i < 16; i++)
                        m[i] = static_cast<float>(local[i].real());
                    e.local = glm::make_mat4(m);
                    e.hasLocal = true;
                }
                for (const auto [key, value] : extra["args"].members())
                    if (value.isString())
                        e.args[std::string(key)] = Bare(value.str());
            }
            e.found = true;
            return e;
        }

        const Table& Get() {
            static const Table table = [] {
                Table t;
                std::string text;
                if (!resources::Read(resources::path::kItemDisplay, text))
                    return t;
                const std::optional<json::Document> doc = json::Document::Parse(text, true);
                if (!doc)
                    return t;
                const json::View root = doc->root();
                std::vector<json::View> styles;
                for (json::View s : root["styles"].items())
                    styles.push_back(s);
                static const char* const kContexts[4] = {"hand", "ground", "gui", "hand_use"};
                for (const auto [name, row] : root["items"].members()) {
                    const json::View hand = row["hand"];
                    for (int c = 0; c < 4; c++) {
                        const json::View own = row[kContexts[c]];
                        t.rows[c][std::string(name)] = Build(own.isArray() ? own : hand, styles);
                    }
                }
                return t;
            }();
            return table;
        }
    }

    const Entry& Lookup(const std::string& name, Context context) {
        const Table& t = Get();
        const std::string_view bare = std::string_view(name).starts_with("minecraft:")
                                          ? std::string_view(name).substr(10)
                                          : std::string_view(name);
        const auto& map = t.rows[static_cast<int>(context)];
        const auto it = map.find(std::string(bare));
        return it == map.end() ? t.missing : it->second;
    }
}
