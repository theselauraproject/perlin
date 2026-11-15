#pragma once
#include "client/systems/model/HudState.hpp"
#include "util/JsonValue.hpp"
#include "client/ui/JsonUi.hpp"
#include "client/ui/SceneEmit.hpp"
#include "client/ui/UiLayout.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace perlin::ui {

    inline constexpr const char* kJsonButtonPrefix = "jb:";

    struct JsonButton {
        std::string id;
        int index = -1;
    };
    bool ParseJsonButton(const std::string& hoverId, JsonButton& out);

    class ScreenData : public UiData {
    public:
        using Row = std::unordered_map<std::string, json::Value>;

        void Set(const std::string& name, json::Value value) { globals[name] = std::move(value); }
        void Set(const std::string& name, bool value) { globals[name] = json::Value(value); }
        void Set(const std::string& name, const char* value) { globals[name] = json::Value(std::string(value)); }
        void Set(const std::string& name, const std::string& value) { globals[name] = json::Value(value); }
        void Set(const std::string& name, double value) { globals[name] = json::Value(value); }
        std::vector<Row>& Collection(const std::string& name) { return collections[name]; }
        std::vector<FactoryItem>& FactoryItems(const std::string& name) { return factories[name]; }

        std::function<bool(const std::string& name, json::Value& out)> fallback;

        bool Global(const std::string& name, json::Value& out) const override;
        int CollectionSize(const std::string& collection) const override;
        bool CollectionValue(const std::string& collection, int index, const std::string& binding,
                             json::Value& out) const override;
        std::vector<FactoryItem> Factory(const std::string& name) const override;

        std::string Signature() const;

    private:
        std::unordered_map<std::string, json::Value> globals;
        std::unordered_map<std::string, std::vector<Row>> collections;
        std::unordered_map<std::string, std::vector<FactoryItem>> factories;
    };

    class JsonScreens {
    public:
        static JsonScreens& Instance();
        ~JsonScreens();
        void Reset();

        void Warm(const std::vector<std::string>& serverPackDirs);
        bool Ready() const { return state.load() == 2; }
        bool WaitReady(int timeoutMs);

        uint64_t Generation() const;

        bool Build(const std::string& reference, const ScreenData& data, int guiW, int guiH, float scroll,
                   SceneDoc& doc, const Vars* rootVars = nullptr);

        void SetItems(std::unordered_map<std::string, std::vector<HudItem>> table) {
            items = std::move(table);
        }

        bool Overridden(const std::string& ns) const;
        float ContentHeight() const { return emitter.ContentHeight(); }
        float ViewportHeight() const { return emitter.ViewportHeight(); }

        bool Animating() const { return anim.animating; }
        SceneEmitter& Emitter() { return emitter; }

        void SetPaperDoll(std::string key) { paperDoll = std::move(key); }

        bool PaperDollRect(float& x, float& y, float& w, float& h) const {
            x = dollX;
            y = dollY;
            w = dollW;
            h = dollH;
            return dollW > 0;
        }
        UiServices& Services() { return services; }

    private:
        JsonScreens();
        const Node* Tree(const std::string& reference, const Vars* rootVars);

        UiLibrary library;
        std::unordered_map<std::string, std::unique_ptr<Node>> trees;
        std::unordered_set<std::string> failed;
        std::unordered_set<std::string> overriddenNs;
        SceneEmitter emitter;
        UiServices services;
        AnimHost anim;
        std::unordered_map<std::string, json::Value> animCache;
        std::thread worker;
        std::atomic<int> state{0};
        std::atomic<uint64_t> generation{0};
        std::string key;
        std::string paperDoll;
        std::unordered_map<std::string, std::vector<HudItem>> items;
        float dollX = 0, dollY = 0, dollW = 0, dollH = 0;
        std::mutex mutex;
    };
}
