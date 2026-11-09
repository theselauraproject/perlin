#include "client/ui/ChestScreen.hpp"

#include "client/resources/TexturePack.hpp"
#include "client/ui/Hud.hpp"
#include "client/ui/HudData.hpp"
#include "client/ui/HudRenderers.hpp"
#include "client/ui/ItemTooltip.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/TenFont.hpp"
#include "client/ui/UiAssets.hpp"
#include "client/ui/UiTemplate.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include "util/Log.hpp"

namespace perlin::ui {
    namespace {
        const char* const kScreens[] = {"small_chest_screen", "large_chest_screen", "ender_chest_screen",
                                        "barrel_screen", "shulker_box_screen"};

        std::string TitleMarker(const std::string& title) {
            if (title.size() >= 4 && title.compare(0, 3, "@@@") == 0 && title[3] >= '1' && title[3] <= '6')
                return title.substr(0, 4);
            return {};
        }

        void StripTitleMarker(Json& json) {
            if (json.IsString()) {
                std::string& t = json.string;
                for (size_t at = t.find("@@@"); at != std::string::npos; at = t.find("@@@", at)) {
                    if (at + 4 < t.size() && t[at + 3] >= '1' && t[at + 3] <= '6' && t[at + 4] == '#')
                        t.erase(at, 4);
                    else
                        at += 3;
                }
            } else if (json.IsObject()) {
                for (auto& member : json.object)
                    StripTitleMarker(member.second);
            } else if (json.IsArray()) {
                for (auto& item : json.array)
                    StripTitleMarker(item);
            }
        }

        void StripTitleMarker(Node& node) {
            StripTitleMarker(node.props);
            for (Json& b : node.bindings)
                StripTitleMarker(b);
            for (auto& child : node.children)
                if (child)
                    StripTitleMarker(*child);
            if (node.gridTemplate)
                StripTitleMarker(*node.gridTemplate);
            for (auto& entry : node.factoryControls)
                if (entry.second)
                    StripTitleMarker(*entry.second);
        }

        const HudItem* SlotItem(const InventoryView& v, const std::string& collection, int index) {
            if (index < 0)
                return nullptr;
            if (collection == "container_items")
                return index < std::min(v.chestRows * 9, 54) ? &v.chestItems[static_cast<size_t>(index)]
                                                             : nullptr;
            if (collection == "inventory_items")
                return index < 27 ? &v.slots[static_cast<size_t>(9 + index)] : nullptr;
            if (collection == "hotbar_items")
                return index < 9 ? &v.slots[static_cast<size_t>(index)] : nullptr;
            return nullptr;
        }

        std::string SlotName(const std::string& collection, int index) {
            if (index < 0)
                return {};
            if (collection == "container_items")
                return "k" + std::to_string(index);
            if (collection == "inventory_items")
                return "s" + std::to_string(9 + index);
            if (collection == "hotbar_items")
                return "s" + std::to_string(index);
            return {};
        }

        class ChestData final : public UiData {
        public:
            explicit ChestData(const InventoryView& view) : v(view) {}

            bool Global(const std::string& name, Json& out) const override {
                if (name == "#container_title") {
                    out = Json(TitleMarker(v.title).empty() ? v.title : v.title.substr(4));
                    return true;
                }
                return false;
            }

            int CollectionSize(const std::string& collection) const override {
                if (collection == "container_items")
                    return std::min(v.chestRows * 9, 54);
                if (collection == "inventory_items")
                    return 27;
                if (collection == "hotbar_items")
                    return 9;
                return 0;
            }

            bool CollectionValue(const std::string& collection, int index, const std::string& binding,
                                 Json& out) const override {
                const HudItem* item = SlotItem(v, collection, index);
                if (!item)
                    return false;
                const auto set = [&](Json value) {
                    out = std::move(value);
                    return true;
                };
                const bool empty = item->Empty();
                if (binding == "#item_id_aux" || binding == "#item_renderer_data")
                    return set(Json(empty ? 0.0 : 1.0));
                if (binding == "#inventory_stack_count")
                    return set(Json(!empty && item->count > 1 ? std::to_string(item->count) : std::string()));
                if (binding == "#stack_count_visible")
                    return set(Json(!empty && item->count > 1));
                if (binding == "#item_durability_visible")
                    return set(Json(!empty && item->maxDamage > 0 && item->aux > 0));
                if (binding == "#item_durability_total_amount")
                    return set(Json(static_cast<double>(item->maxDamage)));
                if (binding == "#item_durability_current_amount")
                    return set(Json(static_cast<double>(std::max(0, item->maxDamage - item->aux))));
                if (binding == "#item_storage_visible" || binding == "#inactive_item" ||
                    binding == "#slot_selected" || binding == "#is_item_locked" ||
                    binding == "#item_lock_cell_image_visible")
                    return set(Json(false));
                if (binding == "#item_storage_total_amount" || binding == "#item_storage_current_amount")
                    return set(Json(0.0));
                return false;
            }

        private:
            const InventoryView& v;
        };

        const LNode* FindNode(const LNode& n, const std::string& id) {
            if (n.id == id)
                return &n;
            for (const auto& kid : n.kids)
                if (const LNode* found = FindNode(*kid, id))
                    return found;
            return nullptr;
        }

        const char* kChestTemplate = R"PERLINRML(<rml>
<head>
<title>chest</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
.cnt { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #ffffff; text-align: right; white-space: nowrap; z-index: 6; }
#cursor { z-index: 30; pointer-events: none; width: 16px; height: 16px; }
#cursor .cnt { z-index: 31; }
{{tipstyle}}{{rcss}}
</style>
</head>
<body>
{{hud}}
<div id="top" style="left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; z-index: 1000; pointer-events: none;"><mclayer layer="top" style="display: block; position: absolute; width: 0px; height: 0px; z-index: -999;"></mclayer>{{cursor}}{{tooltip}}<mclayer layer="gui" style="display: block; position: absolute; width: 0px; height: 0px; z-index: 999;"></mclayer></div>
</body>
</rml>
)PERLINRML";
        const ScreenTemplateDefault kChestDefault("chest_screen.rml", kChestTemplate);
    }

    std::string ChestScreenName(const std::string& titleKey, int rows) {
        if (titleKey == "container.enderchest")
            return "ender_chest_screen";
        if (titleKey == "container.barrel")
            return "barrel_screen";
        if (titleKey == "container.shulkerBox")
            return "shulker_box_screen";
        return rows > 3 ? "large_chest_screen" : "small_chest_screen";
    }

    ChestUi::ChestUi() {
        services.textWidth = [](const std::string& text, float s, bool ten) {
            return ten ? TenFont::Instance().Width(text, s) : McFont::Instance().Width(text, s);
        };
        services.lineHeight = [](float s, bool ten) {
            return ten ? TenFont::Instance().LineHeight(s) : static_cast<float>(McFont::kLineHeight) * s;
        };
        services.textureSize = [](const std::string& tex, int& w, int& h) {
            return UiAssets::Instance().ImageSize(tex, w, h);
        };
        services.localize = [](const std::string& k) { return UiAssets::Instance().Localize(k); };
        emitter.SetTemplates(kChestTemplate, std::string());
        RegisterHudRenderers(emitter, hudState, services);
        emitter.Register("inventory_item_renderer", [this](const LNode& n, RmlWriter& w) {
            const HudItem* item = current ? SlotItem(*current, n.collection, n.index) : nullptr;
            if (item && !item->Empty())
                w.Item(n.ax, n.ay, n.w, n.h, n.layer, item->Key(), n.alpha, item->enchanted);
        });
        emitter.SetSlotNamer([](const LNode& n) {
            return n.type == "input_panel" ? SlotName(n.collection, n.index) : std::string();
        });
    }

    ChestUi::~ChestUi() {
        if (worker.joinable())
            worker.join();
    }

    void ChestUi::Reset() {
        if (worker.joinable())
            worker.join();
        state = 0;
        key.clear();
        problem.clear();
        trees.clear();
        library = UiLibrary{};
        hudState = HudState{};
        current = nullptr;
        dumped.clear();
    }

    void ChestUi::Warm(const std::vector<std::string>& packDirs) {
        std::string wanted;
        for (const std::string& d : packDirs)
            wanted += d + "|";
        wanted += paths::DataDir() + "|" + UiAssets::Instance().VanillaRoot() + "|" +
                  std::to_string(TexturePack::Instance().Generation());
        if (state.load() == 1)
            return;
        if (state.load() != 0 && wanted == key)
            return;
        if (worker.joinable())
            worker.join();
        key = wanted;
        state = 1;
        dumped.clear();
        std::vector<std::string> roots;
        const std::string data = paths::DataDir();
        // first root wins: the player's own texture pack, picked on purpose, ahead of the server's packs
        if (const std::string packUi = ExtractPackUi(data, "pack_ui_chest"); !packUi.empty())
            roots.push_back(packUi + "/ui");
        for (size_t i = packDirs.size(); i-- > 0;)
            roots.push_back(packDirs[i] + "/ui");
        roots.push_back(data + "/ui");
        roots.push_back(data + "/ui_defaults");
        roots.push_back(UiAssets::Instance().VanillaRoot() + "/ui");
        worker = std::thread([this, roots]() {
            library.SetRoots(roots);
            trees.clear();
            problem.clear();
            if (!library.Scan()) {
                problem = "no ui folder";
                state = 3;
                return;
            }
            Vars vars = EngineRootVariables();
            vars["$container_title"] = Json(std::string("#container_title"));
            vars["$localize_title"] = Json(false);
            for (const char* screen : kScreens) {
                BuildReport report;
                if (auto tree = BuildTree(library, std::string("chest.") + screen, vars, report))
                    trees[screen] = std::move(tree);
            }

            bool marked = false;
            for (const std::string& root : roots) {
                std::ifstream in(root + "/chest_screen.json", std::ios::binary);
                std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

                if (text.contains("%.4s") && text.contains("@@@")) {
                    marked = true;
                    break;
                }
            }
            if (marked)
                for (char digit = '1'; digit <= '6'; digit++) {
                    Vars marker = vars;
                    marker["$container_title"] = Json(std::string("@@@") + digit + "#container_title");
                    BuildReport report;
                    if (auto tree = BuildTree(library, "chest.small_chest_screen", marker, report)) {
                        StripTitleMarker(*tree);
                        trees[std::string("small_chest_screen@@@") + digit] = std::move(tree);
                    }
                }
            if (!trees.contains("small_chest_screen") || !trees.contains("large_chest_screen")) {
                problem = "chest.small_chest_screen / chest.large_chest_screen could not be built";
                state = 3;
                return;
            }
            state = 2;
        });
    }

    bool ChestUi::WaitReady(int timeoutMs) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (state.load() == 1 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return Ready();
    }

    bool ChestUi::Build(const InventoryView& view, const std::string& screen, int guiW, int guiH,
                        std::string& rml, float (&panel)[4]) {
        if (state.load() != 2)
            return false;
        auto it = trees.end();
        if (const std::string marker = TitleMarker(view.title); !marker.empty())
            it = trees.find("small_chest_screen" + marker);

        if (it == trees.end() && view.chestRows >= 1 && view.chestRows <= 6 &&
            (screen == "small_chest_screen" || screen == "large_chest_screen"))
            it = trees.find("small_chest_screen@@@" + std::to_string(view.chestRows));
        if (it == trees.end())
            it = trees.find(screen);
        if (it == trees.end())
            it = trees.find(view.chestRows > 3 ? "large_chest_screen" : "small_chest_screen");
        if (it == trees.end() || !it->second)
            return false;
        ChestData data(view);
        const auto root =
            EvaluateTree(*it->second, data, services, static_cast<float>(guiW), static_cast<float>(guiH));
        if (!root)
            return false;
        current = &view;
        emitter.SetTemplates(ScreenTemplate("chest_screen.rml", kChestTemplate), std::string());
        rml = emitter.Emit(*root, static_cast<float>(guiW), static_cast<float>(guiH), std::string(), false);
        current = nullptr;
        if (!rml.contains("id=\"k0\"") || !rml.contains("id=\"s0\"")) {
            problem = "the ui files produced no container slots";
            return false;
        }
        ReplaceAll(rml, "cursor", InventoryCursorRml(view));
        ReplaceAll(rml, "tooltip", InventoryTooltipRml(view, guiW, guiH));
        ReplaceAll(rml, "tipstyle", TooltipStyles());

        const float fallbackH = static_cast<float>(InventoryPanelH(view));
        panel[0] = static_cast<float>(guiW - kInvPanelW) * 0.5f;
        panel[1] = (static_cast<float>(guiH) - fallbackH) * 0.5f;
        panel[2] = static_cast<float>(kInvPanelW);
        panel[3] = fallbackH;
        if (const LNode* p = FindNode(*root, "root_panel"); p && p->w > 0 && p->h > 0) {
            panel[0] = p->ax;
            panel[1] = p->ay;
            panel[2] = p->w;
            panel[3] = p->h;
        }

        if (dumped.insert(it->first).second) {
            const std::filesystem::path out =
                std::filesystem::path(paths::DataDir()) / "ui_generated" / (it->first + ".rml");
            std::error_code ec;
            std::filesystem::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            f << rml;
            for (const std::string& u : emitter.UnknownRenderers())
                Log("chest", "custom renderer not implemented: {}", u);
        }
        return true;
    }
}
