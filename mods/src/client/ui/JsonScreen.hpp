#pragma once
#include "client/ui/HudState.hpp"
#include "client/ui/Json.hpp"
#include "client/ui/JsonUi.hpp"
#include "client/ui/RmlEmit.hpp"
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
    // emitter prefix on non-form json ui buttons: "jb:<to_button_id>[:<row>]"
    inline constexpr const char* kJsonButtonPrefix = "jb:";

    // clicked json ui button: bedrock button id + collection row
    struct JsonButton {
        std::string id; // e.g. "button.menu_continue"
        int index = -1;
    };
    bool ParseJsonButton(const std::string& hoverId, JsonButton& out);

    // screen data for json ui: global bindings ("#name"), collections, factory items;
    // unset bindings keep the control default, as in the game
    class ScreenData : public UiData {
    public:
        using Row = std::unordered_map<std::string, Json>;

        void Set(const std::string& name, Json value) { globals[name] = std::move(value); }
        void Set(const std::string& name, bool value) { globals[name] = Json(value); }
        void Set(const std::string& name, const char* value) { globals[name] = Json(std::string(value)); }
        void Set(const std::string& name, const std::string& value) { globals[name] = Json(value); }
        void Set(const std::string& name, double value) { globals[name] = Json(value); }
        std::vector<Row>& Collection(const std::string& name) { return collections[name]; }
        std::vector<FactoryItem>& FactoryItems(const std::string& name) { return factories[name]; }
        // fallback for unset bindings (families like "#*_visible")
        std::function<bool(const std::string& name, Json& out)> fallback;

        bool Global(const std::string& name, Json& out) const override;
        int CollectionSize(const std::string& collection) const override;
        bool CollectionValue(const std::string& collection, int index, const std::string& binding,
                             Json& out) const override;
        std::vector<FactoryItem> Factory(const std::string& name) const override;

        // stable digest: re-emit only on change
        std::string Signature() const;

    private:
        std::unordered_map<std::string, Json> globals;
        std::unordered_map<std::string, std::vector<Row>> collections;
        std::unordered_map<std::string, std::vector<FactoryItem>> factories;
    };

    // builds full screens ("pause.pause_screen", ...) from the json ui stack: server packs, resource packs, vanilla;
    // one instance for every screen: server overrides of any vanilla screen draw as designed
    class JsonScreens {
    public:
        static JsonScreens& Instance();
        ~JsonScreens();
        void Reset();

        // rescan on server/resource pack change; cheap otherwise
        void Warm(const std::vector<std::string>& serverPackDirs);
        bool Ready() const { return state.load() == 2; }
        bool WaitReady(int timeoutMs);
        // bumps on rescan or language change: cached documents must be rebuilt
        uint64_t Generation() const;

        // false (rml untouched) if screen missing or failed: caller falls back to built-in layout;
        // `rootVars`: engine vars (e.g. $container_title); trees cached per set
        bool Build(const std::string& reference, const ScreenData& data, int guiW, int guiH, float scroll,
                   std::string& rml, const Vars* rootVars = nullptr);
        // inventory_item_renderer items per collection ("container_items", "inventory_items", "hotbar_items")
        void SetItems(std::unordered_map<std::string, std::vector<HudItem>> table) {
            items = std::move(table);
        }
        // some non-vanilla pack defines this screen's namespace
        bool Overridden(const std::string& ns) const;
        float ContentHeight() const { return emitter.ContentHeight(); }
        float ViewportHeight() const { return emitter.ViewportHeight(); }
        // last screen still animating: re-evaluate each frame
        bool Animating() const { return anim.animating; }
        RmlEmitter& Emitter() { return emitter; }
        // paper_doll_renderer texture key (see UiAssets::SetHead): player 3d skin
        void SetPaperDoll(std::string key) { paperDoll = std::move(key); }
        // doll rect in last document (gui units); false if none
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
        RmlEmitter emitter;
        UiServices services;
        AnimHost anim;
        std::unordered_map<std::string, Json> animCache;
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
