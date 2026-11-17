#include "client/ui/SceneKit.hpp"
#include "client/screen/screens/ChestScreen.hpp"
#include "util/Resources.hpp"
#include "client/ui/AssetServices.hpp"

#include "client/systems/resources/TexturePack.hpp"
#include "client/ui/Hud.hpp"
#include "client/ui/HudData.hpp"
#include "client/ui/HudRenderers.hpp"
#include "client/ui/ItemTooltip.hpp"
#include "client/systems/text/McFont.hpp"
#include "client/ui/TenFont.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <iterator>
#include "util/Log.hpp"
#include "util/FileIo.hpp"

namespace perlin::ui {
    namespace {
        const char* const kScreens[] = {"small_chest_screen", "large_chest_screen", "ender_chest_screen",
                                        "barrel_screen", "shulker_box_screen"};

        std::string TitleMarker(const std::string& title) {
            if (title.size() >= 4 && title.starts_with("@@@") && title[3] >= '1' && title[3] <= '6')
                return title.substr(0, 4);
            return {};
        }

        void StripTitleMarker(json::Value& json) {
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
            for (json::Value& b : node.bindings)
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

            bool Global(const std::string& name, json::Value& out) const override {
                if (name == "#container_title") {
                    out = json::Value(TitleMarker(v.title).empty() ? v.title : v.title.substr(4));
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
                                 json::Value& out) const override {
                const HudItem* item = SlotItem(v, collection, index);
                if (!item)
                    return false;
                const auto set = [&](json::Value value) {
                    out = std::move(value);
                    return true;
                };
                const bool empty = item->Empty();
                if (binding == "#item_id_aux" || binding == "#item_renderer_data")
                    return set(json::Value(empty ? 0.0 : 1.0));
                if (binding == "#inventory_stack_count")
                    return set(json::Value(!empty && item->count > 1 ? std::to_string(item->count) : std::string()));
                if (binding == "#stack_count_visible")
                    return set(json::Value(!empty && item->count > 1));
                if (binding == "#item_durability_visible")
                    return set(json::Value(!empty && item->maxDamage > 0 && item->aux > 0));
                if (binding == "#item_durability_total_amount")
                    return set(json::Value(static_cast<double>(item->maxDamage)));
                if (binding == "#item_durability_current_amount")
                    return set(json::Value(static_cast<double>(std::max(0, item->maxDamage - item->aux))));
                if (binding == "#item_storage_visible" || binding == "#inactive_item" ||
                    binding == "#slot_selected" || binding == "#is_item_locked" ||
                    binding == "#item_lock_cell_image_visible")
                    return set(json::Value(false));
                if (binding == "#item_storage_total_amount" || binding == "#item_storage_current_amount")
                    return set(json::Value(0.0));
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
        BindAssetServices(services);
        RegisterHudRenderers(emitter, hudState, services);
        emitter.Register("inventory_item_renderer", [this](const LNode& n, SceneWriter& w) {
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
        const std::string wanted = WarmKey(packDirs);
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
            vars["$container_title"] = json::Value(std::string("#container_title"));
            vars["$localize_title"] = json::Value(false);
            for (const char* screen : kScreens) {
                BuildReport report;
                if (auto tree = BuildTree(library, std::string("chest.") + screen, vars, report))
                    trees[screen] = std::move(tree);
            }

            bool marked = false;
            for (const std::string& root : roots) {
                std::ifstream in(root + "/chest_screen.json", std::ios::binary);
                std::string text;
                file::ReadRest(in, text);

                if (text.contains("%.4s") && text.contains("@@@")) {
                    marked = true;
                    break;
                }
            }
            if (marked)
                for (char digit = '1'; digit <= '6'; digit++) {
                    Vars marker = vars;
                    marker["$container_title"] = json::Value(std::string("@@@") + digit + "#container_title");
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
                        SceneDoc& doc, float (&panel)[4]) {
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
        doc = emitter.Emit(*root, static_cast<float>(guiW), static_cast<float>(guiH), EmitLayout::Chest);
        current = nullptr;
        if (!SceneHasId(doc, "k0") || !SceneHasId(doc, "s0")) {
            problem = "the ui files produced no container slots";
            return false;
        }
        SceneBuilder cursor(doc.scene.lists["cursor"]);
        AddInventoryCursor(cursor, view);
        SceneBuilder tooltip(doc.scene.lists["tooltip"]);
        AddInventoryTooltip(tooltip, view, guiW, guiH);
        ui::kit::TopLayer(doc.scene.nodes, static_cast<float>(guiW), static_cast<float>(guiH), "1000",
                      doc.scene.lists["cursor"], doc.scene.lists["tooltip"]);

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
                std::filesystem::path(paths::DataDir()) / "ui_generated" / (it->first + ".txt");
            std::error_code ec;
            std::filesystem::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            f << DumpScene(doc);
            for (const std::string& u : emitter.UnknownRenderers())
                Log("chest", "custom renderer not implemented: {}", u);
        }
        return true;
    }
}
