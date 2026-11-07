#include "client/renderer/FogDefs.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "client/ui/Json.hpp"

namespace perlin {
    namespace fs = std::filesystem;
    using ui::Json;

    namespace {
        bool ParseColor(const Json* j, glm::vec3& out) {
            if (!j)
                return false;
            if (j->IsArray() && j->array.size() >= 3) {
                out = {static_cast<float>(j->array[0].NumberOr(0)),
                       static_cast<float>(j->array[1].NumberOr(0)),
                       static_cast<float>(j->array[2].NumberOr(0))};
                return true;
            }
            if (j->IsString() && j->string.size() >= 7 && j->string[0] == '#') {
                try {
                    const auto byte = [&](size_t i) {
                        return static_cast<float>(std::stoi(j->string.substr(i, 2), nullptr, 16)) / 255.0f;
                    };
                    out = {byte(1), byte(3), byte(5)};
                    return true;
                } catch (...) {}
            }
            return false;
        }

        FogMedium ParseMedium(const Json* j) {
            FogMedium m;
            if (!j || !j->IsObject())
                return m;
            if (const Json* s = j->Find("fog_start"); s && s->IsNumber())
                m.start = static_cast<float>(s->number);
            if (const Json* e = j->Find("fog_end"); e && e->IsNumber())
                m.end = static_cast<float>(e->number);
            glm::vec3 c;
            if (ParseColor(j->Find("fog_color"), c))
                m.color = c;
            if (const Json* t = j->Find("render_distance_type"))
                m.relative = t->StringOr("fixed") == "render";
            return m;
        }
    }

    void FogDefs::Configure(const std::vector<std::string>& dirs) {
        std::string k;
        for (const std::string& d : dirs) {
            std::error_code ec;
            size_t count = 0;
            for (const auto& e : fs::directory_iterator(fs::path(d) / "fogs", ec))
                if (e.is_regular_file())
                    count++;
            k += d + "|" + std::to_string(count) + ";";
        }
        if (k == key)
            return;
        key = k;
        defs.clear();
        for (const std::string& d : dirs) {
            std::error_code ec;
            for (const auto& e : fs::recursive_directory_iterator(
                     fs::path(d) / "fogs", fs::directory_options::skip_permission_denied, ec)) {
                if (!e.is_regular_file() || e.path().extension() != ".json")
                    continue;
                std::ifstream in(e.path(), std::ios::binary);
                std::stringstream ss;
                ss << in.rdbuf();
                Json doc;
                std::string error;
                if (!Json::Parse(ss.str(), doc, error))
                    continue;
                const Json* settings = doc.Find("minecraft:fog_settings");
                if (!settings)
                    continue;
                const Json* desc = settings->Find("description");
                const Json* id = desc ? desc->Find("identifier") : nullptr;
                if (!id || !id->IsString())
                    continue;
                FogSettings f;
                if (const Json* dist = settings->Find("distance")) {
                    f.air = ParseMedium(dist->Find("air"));
                    f.water = ParseMedium(dist->Find("water"));
                    f.lava = ParseMedium(dist->Find("lava"));
                    f.lavaResistance = ParseMedium(dist->Find("lava_resistance"));
                    f.weather = ParseMedium(dist->Find("weather"));
                }
                defs[id->string] = f;
            }
        }
    }

    FogMedium FogDefs::Resolve(const std::vector<std::string>& stack, int medium) const {
        FogMedium out;
        for (const std::string& id : stack) {
            const auto it = defs.find(id);
            if (it == defs.end())
                continue;
            const FogSettings& f = it->second;
            const FogMedium& m = medium == 1 ? f.water : medium == 2 ? f.lava : f.air;
            if (m.start)
                out.start = m.start;
            if (m.end)
                out.end = m.end;
            if (m.color)
                out.color = m.color;
            if (m.start || m.end)
                out.relative = m.relative;
        }
        return out;
    }
}
