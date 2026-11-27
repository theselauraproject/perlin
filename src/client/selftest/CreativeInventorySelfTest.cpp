#include "client/selftest/Check.hpp"
#include "client/screen/screens/CreativeScreen.hpp"
#include "client/screen/screens/InventoryScreen.hpp"
#include "client/systems/gfx/RenderContext.hpp"
#include "client/systems/input/InputManager.hpp"
#include "client/systems/multiplayer/HudItemConvert.hpp"
#include "client/systems/multiplayer/ItemNbtInfo.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/Hud.hpp"

#include <algorithm>
#include <chrono>
#include <set>
#include <string_view>

namespace {
    using namespace perlin;

    // Exercise the real scene layout/hit testing and inventory mouse routing
    // without a window, graphics device or texture-dependent assertions.
    struct UiRenderer final : RenderContext {
        uint32_t next = 1;
        bool Initialize(RenderSurface&, const Options&) override { return true; }
        void Shutdown() override {}
        void BeginFrame(const FrameParams&) override {}
        void EndFrame() override {}
        void RequestScreenshot(const std::string&) override {}
        void UploadCrosshair(const Texture&) override {}
        void DrawHudCursor(float, float, float, float) override {}
        void UploadSkyTextures(const Texture&, const Texture&, const Texture&) override {}
        void UploadAtlas(const uint8_t*, int, int) override {}
        void UpdateAtlasRegion(int, const uint8_t*) override {}
        uint32_t UiCreateTexture(int, int, const uint8_t*, bool, bool = false) override { return next++; }
        void UiDestroyTexture(uint32_t) override {}
        uint32_t UiCreateMesh(const UiVertex*, uint32_t, const uint32_t*, uint32_t) override { return next++; }
        void UiDestroyMesh(uint32_t) override {}
        void UiSubmit(UiLayer, const UiDraw&) override {}
        void FramebufferSize(int& width, int& height) const override { width = 640; height = 480; }
        void UpdateChunkMesh(int, int, const Tessellator&, uint64_t) override {}
        void RemoveChunkMesh(int, int) override {}
        void ClearChunkMeshes() override {}
        void DrawChunkMeshes(const Frustum&, float, float) override {}
    };

    proto::ItemLegacy Item(int32_t id, std::string_view label = {}, uint8_t damage = 0) {
        proto::ItemLegacy item;
        item.networkId = id;
        item.count = id == 0 ? 0 : 1;
        proto::ItemExtraDataWithoutBlockingTick extra;
        if (!label.empty()) {
            extra.hasNbt = proto::ItemExtraDataWithoutBlockingTickHasNbt::True;
            extra.nbt.emplace();
            extra.nbt->version = 1;
            auto& bytes = extra.nbt->nbt.bytes;
            bytes = {10, 0, 0, 3, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', damage, 0, 0, 0,
                     10, 7, 0, 'd', 'i', 's', 'p', 'l', 'a', 'y',
                     8, 4, 0, 'N', 'a', 'm', 'e', static_cast<uint8_t>(label.size()), 0};
            bytes.insert(bytes.end(), label.begin(), label.end());
            bytes.insert(bytes.end(), {0, 0});
            extra.canPlaceOn = {"minecraft:stone"};
            extra.canDestroy = {"minecraft:dirt"};
        }
        item.extra = std::move(extra);
        return item;
    }

    proto::CreativeContentPacket Catalog() {
        proto::CreativeContentPacket packet;
        const auto group = [&](int category, std::string name, proto::ItemLegacy icon = Item(0)) {
            proto::CreativeContentPacketGroupsEntry value;
            value.category = static_cast<decltype(value.category)>(category);
            value.name = std::move(name);
            value.iconItem = std::move(icon);
            packet.groups.push_back(std::move(value));
        };
        group(1, "test.blocks", Item(10, "Header icon", 7));
        group(2, "");
        group(1, "");
        group(1, "test.colors", Item(12));
        group(3, "test.equipment", Item(14));
        const auto entry = [&](uint32_t id, uint32_t groupIndex, proto::ItemLegacy item) {
            packet.items.push_back({id, std::move(item), groupIndex});
        };
        entry(5003, 3, Item(12));
        entry(7001, 0, Item(10, "Variant A", 4));
        entry(91, 2, Item(14)); // Deliberately assigned by the server to Construction.
        entry(990, 1, Item(11));
        entry(41, 0, Item(10, "Variant B", 12));
        entry(703, 3, Item(13));
        entry(8000, 4, Item(14));
        entry(42, 0xffffffffu, Item(11));
        return packet;
    }

    struct Rig {
        Options options;
        Input input;
        InputManager inputManager{input, options};
        screen::Manager screens;
        ui::ChatLog chat;
        ui::HudState hudState;
        bool advanced = false;
        screen::Host host{screens, input, inputManager, options, [](bool) {},
                          [] { return std::vector<std::string>{}; }, chat, hudState,
                          [](int, bool, bool) { return false; }, advanced};
        screen::InventoryScreen& inventory =
            screens.Emplace<screen::InventoryScreen>(screen::Id::Inventory, screen::Traits{true}, host);
        UiRenderer renderer;
        ui::Hud hud;
        BedrockSession session;
        std::vector<proto::ItemStackRequest> requests;
        std::vector<proto::TransactionUseItem> uses;
        size_t sends = 0;
        bool encoded = true;
        bool decoded = true;

        explicit Rig(int protocol) {
            options.guiScale = 1;
            hudState.mode = ui::GameMode::Creative;
            session.BeginReplayMode("creative-inventory", 7, protocol);
            session.SetSentTap([this](uint32_t id, const std::vector<uint8_t>& body) {
                ++sends;
                net::ReadStream stream(body.data(), body.size());
                if (id == proto::ItemStackRequestPacket::kId) {
                    proto::ItemStackRequestPacket packet;
                    decoded &= proto::Read(stream, packet) && stream.remaining() == 0;
                    requests.insert(requests.end(), packet.requests.begin(), packet.requests.end());
                } else if (id == proto::InventoryTransactionPacket::kId) {
                    proto::InventoryTransactionPacket packet;
                    decoded &= proto::Read(stream, packet) && stream.remaining() == 0;
                    if (const auto* use = std::get_if<proto::TransactionUseItem>(&packet.transaction.transactionData))
                        uses.push_back(*use);
                }
            });
            proto::ItemRegistryPacket registry;
            int16_t id = 10;
            for (const char* name : {"minecraft:stone", "minecraft:oak_planks", "minecraft:red_wool",
                                     "minecraft:blue_wool", "minecraft:diamond_sword"}) {
                proto::ItemstatesEntry entry;
                entry.name = name;
                entry.runtimeId = id++;
                registry.itemstates.push_back(std::move(entry));
            }
            Inject(registry);
            hud.Overlay().Initialize(renderer);
            screens.Open(inventory);
            inventory.openedAt = screen::InventoryScreen::Clock::now() - std::chrono::seconds(1);
        }

        ~Rig() {
            session.SetSentTap({});
            hud.Overlay().Shutdown();
        }

        template<class Packet> void Inject(const Packet& packet) {
            const net::context::ScopedSettings context({session.ServerProtocol(), -1});
            std::vector<uint8_t> body;
            const bool ok = net::EncodeChecked(packet, body);
            encoded &= ok;
            if (ok)
                session.InjectIncoming(Packet::kId, body);
        }

        void Update() {
            screen::Frame frame{hud, &session, 640, 480};
            inventory.Update(frame);
        }

        void Click(int slot, int button = Mouse::Left) {
            constexpr int px = (640 - ui::kCreativeW) / 2, py = (480 - ui::kCreativeH) / 2;
            input.OnMouseMove(static_cast<float>(px + 8 + (slot % 9) * 18 + 9),
                              static_cast<float>(py + 17 + (slot / 9) * 18 + 9));
            Update();
            input.OnMouseDown(button);
            Update();
            input.OnMouseUp(button);
            Update();
            Update();
        }
    };

    void FindImages(const std::vector<ui::SceneNode>& nodes, std::vector<std::string>& tabs,
                    std::vector<std::string>& images) {
        for (const auto& node : nodes) {
            if (node.kind == ui::SceneNode::Image) {
                images.push_back(node.src);
                if (node.cls == "ticon")
                    tabs.push_back(node.src);
            }
            FindImages(node.children, tabs, images);
        }
    }
}

namespace perlin::selftest {
    int CreativeInventory() {
        Checker t("creative_inventory_test");
        const int previousProtocol = net::context::ProtocolSlot().load(std::memory_order_relaxed);
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            Rig rig(protocol);
            const auto packet = Catalog();
            rig.Inject(packet);
            t.Check(rig.encoded, "the creative registry/catalog fixtures fully encode for the selected profile");
            const auto catalog = rig.session.Creative();
            t.Check(catalog.items.size() == 8 && catalog.groups.size() == 5 &&
                        catalog.items[0].entryId == 5003 && catalog.items[4].entryId == 41,
                    "received creative catalog retains group order, entry order and original network IDs");
            // Do not turn a decoder regression into an unchecked fixture
            // access. Later profiles must still run and report their result.
            if (catalog.items.size() != 8 || catalog.groups.size() != 5)
                continue;
            t.Check(catalog.groups[0].icon.customName == "Header icon" && catalog.groups[0].icon.damage == 7 &&
                        catalog.items[4].info.customName == "Variant B" && catalog.items[4].info.damage == 12,
                    "group icons and selectable entries retain their own NBT metadata");
            t.Check(catalog.items.back().group == -1,
                    "the ungrouped wire sentinel does not alias a category or a group index");
            const std::set<size_t> collapsed;
            const auto cells = creative::Category(catalog, 1, collapsed);
            t.Equal(cells, std::vector<creative::Cell>{{0, creative::CellKind::CollapsedGroup},
                                                      {2, creative::CellKind::Item},
                                                      {3, creative::CellKind::CollapsedGroup}},
                    "the live catalog projects named and anonymous groups in server order");
            rig.Update();
            const size_t before = rig.sends;
            rig.Click(0);
            t.Check(rig.inventory.creative.expandedGroups.contains(0) && rig.sends == before &&
                        rig.session.GetInventory().cursor.Empty(),
                    "clicking a collapsed group expands the real inventory UI without an inventory request");
            rig.Click(0);
            t.Check(!rig.inventory.creative.expandedGroups.contains(0) && rig.sends == before,
                    "clicking an expanded group collapses it without crafting its icon");
            rig.Click(0);
            rig.Click(2, Mouse::Right);
            t.Check(rig.decoded && rig.requests.size() == 1 &&
                        !rig.requests[0].actions.empty() &&
                        rig.requests[0].actions[0].typeId == proto::ItemStackRequestActionsEntryTypeId::CraftCreative &&
                        rig.requests[0].actions[0].itemId == 41,
                    "clicking a projected child sends its original noncontiguous creative entry ID");
            t.Check(rig.session.GetInventory().cursor.customName == "Variant B" &&
                        rig.session.GetInventory().cursor.count == 1,
                    "creative selection installs the chosen metadata variant on the cursor");
            rig.session.InvClick(0, false);
            const float eye[]{0, 65, 0};
            rig.session.SendUseItemInAir(eye, false);
            t.Check(rig.decoded && rig.uses.size() == 1, "the selected creative stack reaches a real use transaction");
            if (!rig.uses.empty()) {
                const auto* raw = std::get_if<proto::ItemExtraDataWithoutBlockingTick>(&rig.uses.back().heldItem.extra);
                const auto* expected = std::get_if<proto::ItemExtraDataWithoutBlockingTick>(&packet.items[4].item.extra);
                t.Check(raw && expected && raw->nbt && expected->nbt &&
                            raw->nbt->nbt.bytes == expected->nbt->nbt.bytes &&
                            raw->canPlaceOn == expected->canPlaceOn && raw->canDestroy == expected->canDestroy,
                        "creative selection preserves raw NBT and placement/destruction metadata into outgoing wire data");
            }
            auto malformed = net::Encode(packet);
            malformed.pop_back();
            const uint32_t version = rig.session.CreativeVersion();
            rig.session.InjectIncoming(packet.kId, malformed);
            t.Equal(rig.session.CreativeVersion(), version,
                    "a truncated creative packet cannot replace the catalog snapshot");
            proto::CreativeContentPacket empty;
            rig.Inject(empty);
            rig.Update();
            t.Check(rig.session.Creative().items.empty() && rig.inventory.creative.expandedGroups.empty(),
                    "catalog replacement drops obsolete entries and expansion indexes");
        }
        {
            ui::CreativeView view;
            view.tabPresent.fill(true);
            view.grid[0].name = "minecraft:stone";
            view.grid[0].count = 1;
            view.gridKinds[0] = creative::CellKind::CollapsedGroup;
            view.grid[1] = view.grid[0];
            view.gridKinds[1] = creative::CellKind::ExpandedGroup;
            view.grid[2] = view.grid[0];
            view.gridKinds[2] = creative::CellKind::GroupItem;
            const auto doc = ui::BuildCreativeScreen(view, 640, 480);
            std::vector<std::string> tabs, images;
            FindImages(doc.scene.nodes, tabs, images);
            t.Equal(tabs, std::vector<std::string>{"textures/ui/icon_recipe_construction",
                                                  "textures/ui/icon_recipe_equipment",
                                                  "textures/ui/icon_recipe_item",
                                                  "textures/ui/icon_recipe_nature",
                                                  "textures/ui/magnifyingGlass",
                                                  "textures/ui/inventory_icon"},
                    "actual tab scene uses fixed Bedrock composite textures in UI order");
            for (const char* path : {"textures/ui/recipe_book_light_button",
                                     "textures/ui/recipe_book_dark_button_pressed",
                                     "textures/ui/recipe_book_dark_button",
                                     "textures/ui/recipe_book_expand_icon",
                                     "textures/ui/recipe_book_collapse_icon"})
                t.Check(std::ranges::find(images, path) != images.end(),
                        "actual group scene exposes the corresponding native header/child texture");
        }
        net::context::SetProtocol(previousProtocol);
        return t.Finish();
    }
}
