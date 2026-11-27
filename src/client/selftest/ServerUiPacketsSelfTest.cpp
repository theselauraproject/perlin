#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include <limits>

namespace perlin::selftest {
    int ServerUiPackets() {
        Checker t("server_ui_packets_test");
        BedrockSession session;
        session.BeginReplayMode("server-ui", 7, kBedrockProtocol);
        session.SelectHotbarSlot(3);
        size_t replies = 0;
        session.SetSentTap([&](uint32_t, const std::vector<uint8_t>&) { ++replies; });
        const auto inject = [&]<class P>(const P& packet) {
            session.InjectIncoming(P::kId, net::Encode(packet));
        };

        proto::GuiDataPickItemPacket pick;
        pick.itemName = "first item";
        pick.itemEffects = "first effect";
        pick.hotbarSlot = 8;
        inject(pick);
        pick.itemName = "replacement item";
        pick.itemEffects = "replacement effect";
        pick.hotbarSlot = 1;
        inject(pick);
        GuiDataState::Notice notice;
        t.Check(session.TakePickItemNotice(notice) && notice.name == pick.itemName &&
                    notice.effects == pick.itemEffects,
                "pick-item updates the latest localized popup and its effect text");
        t.Check(!session.TakePickItemNotice(notice), "pick-item popup is not queued or replayed");
        t.Equal(session.PickItemFlash().slot, 1, "pick-item records the requested flash slot");
        t.Equal(session.GetInventory().selected, 3, "pick-item leaves hotbar selection alone");
        auto bytes = net::Encode(pick);
        bytes.pop_back();
        session.InjectIncoming(pick.kId, bytes);
        t.Check(!session.TakePickItemNotice(notice), "truncated pick-item cannot partially update the UI");

        proto::SetPlayerInventoryOptionsPacket options;
        options.leftTab = static_cast<decltype(options.leftTab)>(5);
        options.rightTab = static_cast<decltype(options.rightTab)>(3);
        options.filtering = true;
        options.layout = static_cast<decltype(options.layout)>(2);
        options.craftingLayout = static_cast<decltype(options.craftingLayout)>(1);
        inject(options);
        auto inventory = session.GetInventoryUiOptions();
        t.Check(inventory.serial == 1 && inventory.leftTab == 5 && inventory.rightTab == 3 &&
                    inventory.filtering && inventory.layout == 2 && inventory.craftingLayout == 1,
                "inventory options replace all five fields");
        options.leftTab = static_cast<decltype(options.leftTab)>(-7);
        options.filtering = false;
        inject(options);
        inventory = session.GetInventoryUiOptions();
        t.Check(inventory.serial == 2 && inventory.leftTab == -7 && !inventory.filtering,
                "option storage preserves enum values just as the reference copy does");
        bytes = net::Encode(options);
        bytes.pop_back();
        session.InjectIncoming(options.kId, bytes);
        t.Equal(session.GetInventoryUiOptions().serial, uint64_t{2},
                "truncated option packet leaves the previous snapshot intact");

        proto::SetPlayerFurnaceOptionsPacket furnace;
        for (int type = 1; type <= 3; ++type) {
            furnace.furnaceType = static_cast<decltype(furnace.furnaceType)>(type);
            furnace.furnaceOptions = {type + 10, type == 2, type + 20};
            inject(furnace);
        }
        for (int type = 1; type <= 3; ++type) {
            const auto stored = session.GetFurnaceUiOptions(type);
            t.Check(stored.serial == 1 && stored.leftTab == type + 10 &&
                        stored.filtering == (type == 2) && stored.layout == type + 20,
                    "each furnace kind has independent preferences");
        }
        for (const int type : {0, 4, 255}) {
            furnace.furnaceType = static_cast<decltype(furnace.furnaceType)>(type);
            inject(furnace);
        }
        t.Equal(session.GetFurnaceUiOptions(1).serial, uint64_t{1},
                "out-of-range furnace kinds cannot overwrite a valid kind");
        t.Equal(session.GetFurnaceUiOptions(0).serial, uint64_t{0}, "invalid lookup is empty");
        proto::PlayerLocationPacket location;
        location.entityUniqueId = 99;
        location.type = proto::PlayerLocationPacketType::Coordinates;
        location.position = proto::Vec3F{1.0e20f, -1.0e20f, 1.0e20f};
        inject(location);
        auto locations = session.GetTrackedPlayerLocations();
        t.Check(locations.contains(99) && locations.at(99) && locations.at(99)->x == 1.0e20f,
                "location accepts any finite coordinates even for an unloaded actor");
        location.position = proto::Vec3F{2, 3, 4};
        inject(location);
        t.Check(session.GetTrackedPlayerLocations().at(99)->y == 3,
                "location update replaces the previous position");
        location.type = proto::PlayerLocationPacketType::TypeHide;
        location.position.reset();
        location.entityUniqueId = 100;
        inject(location);
        locations = session.GetTrackedPlayerLocations();
        t.Check(locations.contains(100) && !locations.at(100),
                "hide creates a hidden entry even for an unknown unique ID");
        location.entityUniqueId = 99;
        inject(location);
        t.Check(!session.GetTrackedPlayerLocations().at(99), "hide replaces an existing coordinate");
        entity::Entity actor;
        t.Check(!session.Entities().FindUnique(99, actor), "locations do not spawn or transform an actor");
        t.Equal(replies, size_t{0}, "these clientbound UI packets generate no protocol reply");
        session.SetSentTap({});
        session.ResetServerGui();
        t.Check(!session.TakePickItemNotice(notice) && session.PickItemFlash().slot == -1 &&
                    session.GetInventoryUiOptions().serial == 0 &&
                    session.GetFurnaceUiOptions(2).serial == 0,
                "reconnection reset removes previous server UI state");

        BedrockSession beforePlayer;
        beforePlayer.InjectIncoming(options.kId, net::Encode(options));
        furnace.furnaceType = proto::SetPlayerFurnaceOptionsPacketFurnaceType::Furnace;
        beforePlayer.InjectIncoming(furnace.kId, net::Encode(furnace));
        t.Equal(beforePlayer.GetInventoryUiOptions().serial, uint64_t{0},
                "inventory preferences are ignored before a local player exists");
        t.Equal(beforePlayer.GetFurnaceUiOptions(1).serial, uint64_t{0},
                "valid furnace preferences are ignored before a local player exists");
        for (float value : {std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
            BedrockSession invalid;
            invalid.BeginReplayMode("invalid-location", 7, kBedrockProtocol);
            location.type = proto::PlayerLocationPacketType::Coordinates;
            location.position = proto::Vec3F{0, value, 0};
            invalid.InjectIncoming(location.kId, net::Encode(location));
            t.Check(invalid.GetDisconnectInfo().happened && invalid.GetDisconnectInfo().reason == 90 &&
                        invalid.GetTrackedPlayerLocations().empty(),
                    "invalid location triggers BadPacket without contaminating locator state");
        }
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            const net::context::ScopedSettings context({protocol, -1});
            BedrockSession containers;
            containers.BeginReplayMode("container-lifecycle", 7, protocol);
            std::vector<proto::PacketViolationWarningPacket> warnings;
            std::vector<proto::ContainerClosePacket> closes;
            std::vector<std::vector<uint8_t>> warningBytes;
            bool decoded = true;
            containers.SetSentTap([&](uint32_t id, const std::vector<uint8_t>& body) {
                net::ReadStream stream(body.data(), body.size());
                if (id == proto::PacketViolationWarningPacket::kId) {
                    proto::PacketViolationWarningPacket warning;
                    decoded &= proto::Read(stream, warning) && stream.remaining() == 0;
                    warnings.push_back(std::move(warning));
                    warningBytes.push_back(body);
                } else if (id == proto::ContainerClosePacket::kId) {
                    proto::ContainerClosePacket close;
                    decoded &= proto::Read(stream, close) && stream.remaining() == 0;
                    closes.push_back(close);
                }
            });
            const auto deliver = [&]<class P>(const P& packet) {
                containers.InjectIncoming(P::kId, net::Encode(packet));
            };
            proto::ContainerOpenPacket open;
            open.windowId = static_cast<proto::WindowID>(4);
            open.runtimeEntityId = -1;
            proto::ContainerClosePacket close;
            close.windowId = static_cast<proto::WindowID>(99);
            close.windowType = proto::WindowType::None;
            close.server = false;
            deliver(close);
            close.server = true;
            deliver(close);
            t.Check(warnings.empty() && closes.empty() && !containers.TakeServerClosedInventory(),
                    "container close without an active manager neither warns nor acknowledges");

            for (auto type : {proto::WindowType::Container, proto::WindowType::Workbench,
                              proto::WindowType::Furnace, proto::WindowType::Horse,
                              proto::WindowType::Inventory}) {
                warnings.clear();
                warningBytes.clear();
                closes.clear();
                open.windowType = type;
                deliver(open);
                t.Check(containers.InventoryIsOpen(), "container open establishes the current manager");
                close.server = false;
                close.windowType = proto::WindowType::None;
                deliver(close);
                t.Check(containers.InventoryIsOpen() && !containers.TakeServerClosedInventory() &&
                            warnings.size() == 1 && closes.empty(),
                        "unsolicited close response preserves the manager and sends only a warning");
                if (!warnings.empty()) {
                    const auto& warning = warnings.back();
                    t.Check(static_cast<int32_t>(warning.violationType) == -1 &&
                                static_cast<int32_t>(warning.severity) == 0 && warning.packetId == 156 &&
                                warning.reason == "Server sent a ContainerClosePacket response packet when it should have sent the server initiated packet",
                            "container warning uses the recovered type, severity, cause and exact context");
                    const auto& raw = warningBytes.back();
                    t.Check(raw.size() > 5 && raw[0] == 1 && raw[1] == 0 &&
                                raw[2] == 0xb8 && raw[3] == 2,
                            "warning serializes literal signed -1, zero and packet cause 156");
                }
                close.server = true;
                close.windowType = type == proto::WindowType::Container
                                       ? proto::WindowType::Workbench : proto::WindowType::Container;
                deliver(close);
                t.Check(containers.InventoryIsOpen() && !containers.TakeServerClosedInventory() &&
                            closes.empty(),
                        "server close waits for a matching screen type and sends no early response");
                close.windowType = type;
                deliver(close);
                t.Check(containers.InventoryIsOpen() && containers.TakeServerClosedInventory() &&
                            !containers.TakeServerClosedInventory() && closes.empty(),
                        "matching server close is consumed once by the screen without an early response");
                containers.CloseInventory();
                t.Check(!containers.InventoryIsOpen() && !containers.ChestOpen() &&
                            !containers.HorseOpen() && !containers.WorkbenchOpen() && closes.size() == 1,
                        "screen closure removes the manager and sends one close");
                if (!closes.empty()) {
                    t.Check(closes[0].windowId == open.windowId &&
                                closes[0].windowType == proto::WindowType::None && !closes[0].server,
                            "close uses the current manager ID, type None and response flag");
                }
                close.server = false;
                deliver(close);
                containers.CloseInventory();
                t.Check(warnings.size() == 1 && closes.size() == 1 && !containers.InventoryIsOpen(),
                        "later response and repeated UI close do not send a second close or warning");
            }

            warnings.clear();
            closes.clear();
            open.windowType = proto::WindowType::Workbench;
            deliver(open);
            containers.CloseInventory();
            t.Check(closes.size() == 1 && closes[0].windowId == open.windowId &&
                        closes[0].windowType == proto::WindowType::None,
                    "ordinary player-initiated close uses the same manager deletion packet");
            open.windowId = static_cast<proto::WindowID>(5);
            deliver(open);
            close.server = false;
            deliver(close);
            t.Check(warnings.size() == 1 && containers.InventoryIsOpen(),
                    "a late response while a new manager exists warns without comparing window IDs");
            close.server = true;
            close.windowType = open.windowType;
            deliver(close);
            auto truncated = net::Encode(close);
            truncated.pop_back();
            containers.InjectIncoming(close.kId, truncated);
            t.Check(containers.TakeServerClosedInventory(),
                    "a truncated close cannot overwrite a pending valid close");
            deliver(close);
            containers.BeginReplayMode("new-container-session", 7, protocol);
            t.Check(!containers.InventoryIsOpen() && !containers.TakeServerClosedInventory() &&
                        !containers.TakeWorkbenchOpened() && !containers.ChestOpen(),
                    "session reset clears container lifecycle and pending UI-open/close signals");
            t.Check(decoded, "all container responses decode completely in the negotiated profile");
            containers.SetSentTap({});

            BedrockSession absent;
            size_t absentReplies = 0;
            absent.SetSentTap([&](uint32_t, const std::vector<uint8_t>&) { ++absentReplies; });
            absent.InjectIncoming(open.kId, net::Encode(open));
            absent.InjectIncoming(close.kId, net::Encode(close));
            t.Check(!absent.InventoryIsOpen() && absentReplies == 0,
                    "container packets before a local player exists do not establish state or reply");
            absent.SetSentTap({});
        }
        {
            const auto previousProtocol = net::context::Protocol();
            BedrockSession camera;
            camera.BeginReplayMode("camera-2193", 7, 2193);
            size_t cameraReplies = 0;
            camera.SetSentTap([&](uint32_t, const std::vector<uint8_t>&) { ++cameraReplies; });
            // Fade in immediately, hold for ten seconds, then fade out immediately.
            // No sleep or time-sensitive interpolation sample is needed.
            camera.InjectIncoming(proto::CameraInstructionPacket::kId,
                                  {0, 0, 1, 1,
                                   0, 0, 0, 0, 0, 0, 0x20, 0x41, 0, 0, 0, 0,
                                   1, 0, 0, 0x80, 0x3e, 0, 0, 0, 0x3f, 0, 0, 0x40, 0x3f,
                                   0, 0, 0, 0, 0, 0});
            camera.Camera().Tick({}, {});
            const auto tint = camera.Camera().Tint();
            t.Check(tint.r == 0.25f && tint.g == 0.5f && tint.b == 0.75f && tint.a == 1.0f,
                    "literal 2193 fade timing and color reach the live camera consumer");

            camera.InjectIncoming(proto::CameraInstructionPacket::kId,
                                  {0, 0, 0, 0, 0, 1, 0, 0, 0xb4, 0x42, 0, 0, 0, 0,
                                   7, 'I', 'N', '_', 'Q', 'U', 'A', 'D', 0, 0, 0, 0});
            t.Equal(camera.Camera().Fov(70.0f), 90.0f,
                    "literal 2193 FOV with a registered uppercase name reaches camera state");
            t.Equal(cameraReplies, size_t{0}, "camera instructions do not invent a protocol reply");
            camera.SetSentTap({});
            net::context::SetProtocol(previousProtocol);
        }
        return t.Finish();
    }
}
