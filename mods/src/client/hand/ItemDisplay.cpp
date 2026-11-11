#include "client/hand/ItemDisplay.hpp"

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "util/Json.hpp"
#include "util/Resources.hpp"

namespace perlin::itemdisplay {
    namespace {
        struct Table {
            std::vector<std::unordered_map<std::string, Transform>> styles;
            std::unordered_map<std::string, Entry> hand, ground;
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
                return glm::vec3(a[0].real(), a[1].real(), a[2].real());
            };
            t.rotation = vec(v["rotation"], glm::vec3(0.0f));
            t.translation = vec(v["translation"], glm::vec3(0.0f));
            t.scale = vec(v["scale"], glm::vec3(1.0f));
            return t;
        }

        Entry Build(json::View row, const std::vector<json::View>& styles) {
            Entry e;
            if (!row.isArray() || row.size() < 3)
                return e;
            const std::string kind = row[0].str();
            if (kind == "flat")
                e.kind = Entry::Kind::Flat;
            else if (kind == "block")
                e.kind = Entry::Kind::Block;
            else {
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
                if (!e.firstLeft.present)
                    e.firstLeft = e.firstRight;
                if (!e.thirdLeft.present)
                    e.thirdLeft = e.thirdRight;
            }
            e.found = true;
            return e;
        }

        const Table& Get() {
            static Table table = [] {
                Table t;
                static std::string text;
                static std::optional<json::Document> doc;
                if (!resources::Read("item_display.json", text))
                    return t;
                doc = json::Document::Parse(text, true);
                if (!doc)
                    return t;
                const json::View root = doc->root();
                std::vector<json::View> styles;
                for (json::View s : root["styles"].items())
                    styles.push_back(s);
                for (const auto [name, row] : root["items"].members()) {
                    t.hand[std::string(name)] = Build(row["hand"], styles);
                    t.ground[std::string(name)] =
                        Build(row["ground"].isArray() ? row["ground"] : row["hand"], styles);
                }
                return t;
            }();
            return table;
        }
    }

    const Entry& Lookup(const std::string& name, bool ground) {
        const Table& t = Get();
        const std::string bare = name.starts_with("minecraft:") ? name.substr(10) : name;
        const auto& map = ground ? t.ground : t.hand;
        const auto it = map.find(bare);
        return it == map.end() ? t.missing : it->second;
    }

    glm::mat4 Apply(const Transform& t, bool leftHand) {
        if (!t.present)
            return glm::mat4(1.0f);
        const float k = 3.14159265358979323846f / 180.0f;
        float ry = t.rotation.y, rz = t.rotation.z;
        if (leftHand) {
            ry = -ry;
            rz = -rz;
        }
        const float side = leftHand ? -1.0f : 1.0f;
        glm::mat4 m = glm::translate(
            glm::mat4(1.0f), glm::vec3(side * t.translation.x, t.translation.y, t.translation.z) / 16.0f);
        m = glm::rotate(m, t.rotation.x * k, glm::vec3(1, 0, 0));
        m = glm::rotate(m, ry * k, glm::vec3(0, 1, 0));
        m = glm::rotate(m, rz * k, glm::vec3(0, 0, 1));
        return glm::scale(m, t.scale);
    }
}
