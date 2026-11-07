#pragma once
#include "client/ui/Json.hpp"
#include "client/ui/JsonUi.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace perlin::ui {
    struct FactoryItem {
        std::string id;
        Vars vars;
        std::string collection;
        int index = -1;
    };

    class UiData {
    public:
        virtual ~UiData() = default;
        virtual bool Global(const std::string& name, Json& out) const = 0;
        virtual int CollectionSize(const std::string& collection) const { return 0; }
        virtual bool CollectionValue(const std::string& collection, int index, const std::string& binding,
                                     Json& out) const {
            return false;
        }
        virtual std::vector<FactoryItem> Factory(const std::string& name) const { return {}; }
    };

    struct AnimHost {
        struct Rec {
            double start = 0;
            uint64_t seen = 0;
            bool finished = false;
            std::string destroyName;
        };
        double now = 0;
        uint64_t epoch = 0;
        bool animating = false;
        std::unordered_map<std::string, Rec> recs;
        std::unordered_map<std::string, double> events;
        std::unordered_set<std::string> destroyed;
        void Fire(const std::string& name) { events[name] = now; }
    };

    struct UiServices {
        std::function<Json(const std::string& ref, const std::string& ns)> animDef;
        AnimHost* anim = nullptr;
        std::function<float(const std::string& text, float scale, bool ten)> textWidth;
        std::function<float(float scale, bool ten)> lineHeight;
        std::function<bool(const std::string& texture, int& width, int& height)> textureSize;
        std::function<std::string(const std::string& key)> localize;
    };

    struct LNode {
        const Node* src = nullptr;
        std::string type;
        std::string id;
        bool visible = true;
        Json bag = Json::MakeObject();
        Json p = Json::MakeObject();
        float x = 0, y = 0, w = 0, h = 0;
        float ax = 0, ay = 0;
        int layer = 0;
        float alpha = 1.0f;
        std::string collection;
        int index = -1;
        std::string text;
        std::vector<std::unique_ptr<LNode>> kids;

        bool dependsW = false, dependsH = false;
        bool fromFactory = false;
        bool needSibW = false, needSibH = false;
        int textLines = 1;
        float textWidth = 0;
        std::vector<std::string> lines;

        std::string Str(const char* key, const std::string& fallback = {}) const {
            const Json* v = p.Find(key);
            return v && v->IsString() ? v->string : fallback;
        }
        double Num(const char* key, double fallback) const {
            const Json* v = p.Find(key);
            return v && v->IsNumber() ? v->number : fallback;
        }
        bool Bool(const char* key, bool fallback) const {
            const Json* v = p.Find(key);
            if (!v)
                return fallback;
            if (v->IsBool())
                return v->boolean;
            if (v->IsNumber())
                return v->number != 0;
            return fallback;
        }
        const Json* Get(const char* key) const { return p.Find(key); }
    };

    std::unique_ptr<LNode> EvaluateTree(const Node& root, const UiData& data, const UiServices& services,
                                        float guiW, float guiH);

    std::string StripFormatting(const std::string& text);
}
