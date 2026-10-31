#include "client/multiplayer/BedrockSession.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <vector>
#include "world/level/block/BlockPalette.hpp"
#include "client/player/BlockUse.hpp"
#include "util/Log.hpp"

namespace perlin {
    using namespace proto;

    namespace {
        Vec3F V(const float p[3]) { return {p[0], p[1], p[2]}; }
    }

    bool BedrockSession::SendUseItemOnBlock(int bx, int by, int bz, int face, const float click[3], const float eye[3], int use, const int dest[3], bool repeat) {
        if (face < 0 || face > 5) return false;
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemUse;
        TransactionUseItem u;
        int selected = 0;
        {
            std::lock_guard lock(statusMutex);
            selected = std::clamp(inventory.selected, 0, 8);
            u.heldItem = inventoryRaw[selected];
        }
        if (use == 2 && (u.heldItem.count == 0 || u.heldItem.blockRuntimeId == 0)) return false;
        if (use == 2) {
            static constexpr int offsets[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
            const bool same = dest[0] == bx && dest[1] == by && dest[2] == bz;
            if (!same && (int64_t(dest[0]) != int64_t(bx) + offsets[face][0] ||
                          int64_t(dest[1]) != int64_t(by) + offsets[face][1] ||
                          int64_t(dest[2]) != int64_t(bz) + offsets[face][2])) return false;
            bool loaded = false;
            const auto id = netLevel.GetBlock(dest[0], dest[1], dest[2], loaded);
            if (!blockuse::IsReplaceableCell(loaded, id, BlockPalette::Instance().Name(id))) return false;
        }
        u.actionType = TransactionUseItemActionType::ClickBlock;
        // GameMode::buildBlock 0x14376B290 writes trigger = simulated + 1: startBuildBlock passes false, continueBuildBlock true
        u.triggerType = repeat ? TransactionUseItemTriggerType::SimulationTick : TransactionUseItemTriggerType::PlayerInput;
        u.blockPosition = {bx, by, bz};
        u.face = static_cast<uint8_t>(face);
        u.hotbarSlot = selected;
        u.hand = TransactionUseItemHand::MainHand;
        // the actor's tick position (eye level, the same one PlayerAuthInput carries), not the interpolated camera
        u.playerPos = haveLastAuthPos ? Vec3F{lastAuthPos[0], lastAuthPos[1] + kPlayerEyeHeight, lastAuthPos[2]} : V(eye);
        u.clickPos = V(click);
        {
            bool loaded = false;
            const uint16_t clicked = netLevel.GetBlock(bx, by, bz, loaded);
            if (use == 2 && !loaded) return false;
            u.blockRuntimeId = loaded ? static_cast<uint32_t>(BlockPalette::Instance().RuntimeId(clicked)) : 0;
        }
        haveLoggedInteraction = true;
        Log("place", "use item on ({},{},{}) face {} clicked id {} held {} x{} block {} use {} dest ({},{},{})", bx, by, bz, face, u.blockRuntimeId, u.heldItem.networkId, static_cast<int>(u.heldItem.count), u.heldItem.blockRuntimeId, (use == 2 ? "place" : use == 1 ? "interact" : "nothing"), dest[0], dest[1], dest[2]);
        if (use != 0 && !itemUseOnActive.exchange(true)) {
            PlayerActionPacket start;
            start.runtimeEntityId = runtimeId;
            start.action = Action::StartItemUseOn;
            start.position = {bx, by, bz};
            start.resultPosition = {dest[0], dest[1], dest[2]};
            start.face = face;
            if (!Send(start)) {
                itemUseOnActive = false;
                return false;
            }
            std::copy(dest, dest + 3, itemUseDest);
        }
        u.clientPrediction = use != 0 ? TransactionUseItemClientPrediction::Success : TransactionUseItemClientPrediction::Failure;
        p.transaction.transactionData = u;
        if (!Send(p)) return false;
        if (use != 0) {
            itemUseDest[0] = dest[0];
            itemUseDest[1] = dest[1];
            itemUseDest[2] = dest[2];
        }
        if (use == 2) {
            // the game spends one block up front; the server corrects us if it disagrees
            const bool creative = World().worldGameMode == 1;
            if (!creative) {
                std::lock_guard lock(statusMutex);
                const int held = std::clamp(inventory.selected, 0, 8);
                if (InvInfoLocked(held).count > 0) {
                    InvInfoLocked(held).count--;
                    if (InvRawLocked(held).count > 0)
                        InvRawLocked(held).count = static_cast<uint16_t>(InvRawLocked(held).count - 1);
                    if (InvInfoLocked(held).count <= 0) {
                        InvInfoLocked(held) = ItemStackInfo{};
                        InvRawLocked(held) = ItemV4{};
                    }
                }
            }
            std::lock_guard lock(placeMutex);
            pendingPlace = {dest[0], dest[1], dest[2], std::chrono::steady_clock::now(), true, static_cast<int32_t>(u.heldItem.blockRuntimeId)};
        }
        return true;
    }

    bool BedrockSession::SendUseItemInAir(const float eye[3], bool held) {
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemUse;
        TransactionUseItem u;
        int selected = 0;
        {
            std::lock_guard lock(statusMutex);
            selected = std::clamp(inventory.selected, 0, 8);
            u.heldItem = inventoryRaw[selected];
        }
        if (held)
            QueueInputFlag(InputFlag::StartUsingItem);
        // the game sends click-air with no trigger and a success prediction; player input reads as a scaffold to anticheats
        u.actionType = TransactionUseItemActionType::ClickAir;
        u.triggerType = TransactionUseItemTriggerType::Unknown;
        u.clientPrediction = TransactionUseItemClientPrediction::Success;
        u.blockPosition = {0, 0, 0};
        u.face = 255;
        u.hotbarSlot = selected;
        u.hand = TransactionUseItemHand::MainHand;
        u.playerPos = V(eye);
        u.clickPos = {0, 0, 0};
        u.blockRuntimeId = 0;
        p.transaction.transactionData = u;
        return Send(p);
    }

    bool BedrockSession::SendReleaseItem(const float eye[3]) {
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemRelease;
        TransactionTransactionDataCase2 r;
        {
            std::lock_guard lock(statusMutex);
            r.hotbarSlot = std::clamp(inventory.selected, 0, 8);
            r.heldItem = inventoryRaw[r.hotbarSlot];
        }
        r.actionType = TransactionTransactionDataCase2ActionType::Release;
        r.headPos = V(eye);
        p.transaction.transactionData = r;
        return Send(p);
    }

    bool BedrockSession::SendAttackEntity(uint64_t targetRuntimeId, const float eye[3], const float click[3]) {
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemUseOnEntity;
        TransactionTransactionDataCase1 a;
        {
            std::lock_guard lock(statusMutex);
            a.hotbarSlot = std::clamp(inventory.selected, 0, 8);
            a.heldItem = inventoryRaw[a.hotbarSlot];
        }
        a.entityRuntimeId = targetRuntimeId;
        a.actionType = TransactionTransactionDataCase1ActionType::Attack;
        a.playerPos = V(eye);
        a.clickPos = V(click);
        p.transaction.transactionData = a;
        return Send(p);
    }

    bool BedrockSession::SendInteractEntity(uint64_t targetRuntimeId, const float eye[3], const float click[3]) {
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemUseOnEntity;
        TransactionTransactionDataCase1 a;
        {
            std::lock_guard lock(statusMutex);
            a.hotbarSlot = std::clamp(inventory.selected, 0, 8);
            a.heldItem = inventoryRaw[a.hotbarSlot];
        }
        a.entityRuntimeId = targetRuntimeId;
        a.actionType = TransactionTransactionDataCase1ActionType::Interact;
        a.playerPos = V(eye);
        a.clickPos = V(click);
        p.transaction.transactionData = a;
        Log("interact", "entity {}", targetRuntimeId);
        return Send(p);
    }

    bool BedrockSession::SendLeaveVehicle() {
        const int64_t uid = ridingUid.load();
        if (uid == 0)
            return false;
        entity::Entity vehicle;
        if (!entities.FindUnique(uid, vehicle))
            return false;
        InteractPacket p;
        p.actionId = InteractPacketActionId::LeaveVehicle;
        p.targetEntityId = vehicle.runtimeId;
        p.hasPosition = false;
        Log("interact", "leave vehicle {}", vehicle.runtimeId);
        return Send(p);
    }

    bool BedrockSession::SendPickBlock(int bx, int by, int bz) {
        BlockPickRequestPacket p;
        p.x = bx;
        p.y = by;
        p.z = bz;
        p.addUserData = false;
        {
            std::lock_guard lock(statusMutex);
            p.selectedSlot = static_cast<uint8_t>(std::clamp(inventory.selected, 0, 8));
        }
        return Send(p);
    }

    bool BedrockSession::SendCreditsFinished() {
        ShowCreditsPacket p;
        p.runtimeEntityId = runtimeId;
        p.status = 1;
        return Send(p);
    }

    bool BedrockSession::SendPickEntity(uint64_t runtimeId) {
        EntityPickRequestPacket p;
        p.runtimeEntityId = runtimeId;
        p.withData = false;
        {
            std::lock_guard lock(statusMutex);
            p.selectedSlot = static_cast<uint8_t>(std::clamp(inventory.selected, 0, 8));
        }
        return Send(p);
    }

    bool BedrockSession::SendDropRequest(int slot, int count, int attempt) {
        ItemStackRequestPacket p;
        ItemStackRequest r;
        ItemStackRequestActionsEntry a;
        {
            std::lock_guard lock(statusMutex);
            const ItemV4& raw = inventoryRaw[slot];
            if (raw.networkId == 0 || raw.count == 0)
                return false;
            if (attempt < 0)
                attempt = dropForm;
            if (attempt == 1) {
                a.source.slotType.containerId = slot < 9 ? ContainerSlotType::Hotbar : ContainerSlotType::Inventory;
                a.source.slot = static_cast<uint8_t>(slot < 9 ? slot : slot - 9);
            } else {
                a.source.slotType.containerId = ContainerSlotType::HotbarAndInventory;
                a.source.slot = static_cast<uint8_t>(slot);
            }
            a.source.stackId = attempt >= 2 ? 0 : raw.stackId.value_or(0);
            a.count = static_cast<uint8_t>(count <= 0 ? std::min<int>(raw.count, 255) : std::min<int>(count, raw.count));
            r.requestId = nextStackRequestId;
            nextStackRequestId -= 2;
            {
                InvSnapshot snapshot;
                snapshot.view = inventory;
                std::copy(std::begin(inventoryRaw), std::end(inventoryRaw), std::begin(snapshot.raw));
                std::copy(std::begin(extraRaw), std::end(extraRaw), std::begin(snapshot.extra));
                std::copy(std::begin(chestRaw), std::end(chestRaw), std::begin(snapshot.chest));
                invSnapshots[r.requestId] = std::move(snapshot);
                if (invSnapshots.size() > 16)
                    invSnapshots.erase(std::prev(invSnapshots.end()));
                ItemStackInfo& shown = slot < 9 ? inventory.hotbar[slot] : inventory.main[slot - 9];
                ItemV4& rawSlot = inventoryRaw[slot];
                const int left = static_cast<int>(rawSlot.count) - static_cast<int>(a.count);
                if (left <= 0) {
                    shown = ItemStackInfo{};
                    rawSlot = ItemV4{};
                } else {
                    shown.count = left;
                    rawSlot.count = static_cast<decltype(rawSlot.count)>(left);
                }
            }
            pendingDrops[r.requestId] = {slot, a.count, attempt};
            if (pendingDrops.size() > 64)
                pendingDrops.erase(std::prev(pendingDrops.end()));
        }
        a.typeId = ItemStackRequestActionsEntryTypeId::Drop;
        a.legacyTypeId = static_cast<uint8_t>(ItemStackRequestActionsEntryTypeId::Drop);
        a.randomly = false;
        r.actions.push_back(a);
        r.cause = static_cast<ItemStackRequestCause>(-1);
        Log("inventory", "drop request {} slot {} stack {} count {} form {}", r.requestId, slot, a.source.stackId, static_cast<int>(a.count), attempt);
        p.requests.push_back(std::move(r));
        return Send(p);
    }

    bool BedrockSession::SendDropSelected(int count) {
        int slot;
        {
            std::lock_guard lock(statusMutex);
            slot = std::clamp(inventory.selected, 0, 8);
        }
        return SendDropRequest(slot, count, -1);
    }
}

namespace perlin {
    bool BedrockSession::PredictPlace(int x, int y, int z, int32_t heldRuntimeId) {
        return PredictPlaceState(x, y, z, heldRuntimeId, false);
    }

    bool BedrockSession::PredictBridgePlace(int x, int y, int z, int32_t heldRuntimeId, int face, const float click[3]) {
        auto& palette = BlockPalette::Instance();
        const uint16_t held = palette.InternRuntime(heldRuntimeId);
        const std::string name = palette.Name(held);
        auto states = palette.States(held);
        const auto bareKey = [](const std::string& key) { return key.starts_with("minecraft:") ? key.substr(10) : key; };
        const bool slab = name.ends_with("_slab") && !name.contains("double");
        const bool stairs = name.ends_with("_stairs");
        const bool pillar = !slab && !stairs && states.size() == 1 && bareKey(states[0].first) == "pillar_axis";
        if (!name.starts_with("minecraft:") || !(slab || stairs || pillar))
            return PredictPlace(x, y, z, heldRuntimeId);
        if (face < 0 || face > 5 || !std::isfinite(click[1])) return false;
        const bool upper = blockuse::IsUpperSlabPlacement(face, click[1]);
        // pillar_axis values by index: y, x, z
        static const char* const kAxis[3] = {"y", "x", "z"};
        int resolved = 0;
        for (auto& [key, value] : states) {
            const auto bare = bareKey(key);
            const bool text = value.kind == BlockPalette::StateValue::String;
            if (slab && bare == "top_slot_bit" && !text) {
                value.i = upper ? 1 : 0;
                resolved++;
            } else if (slab && bare == "vertical_half" && text) {
                value.s = upper ? "top" : "bottom";
                resolved++;
            } else if (stairs && bare == "upside_down_bit" && !text) {
                value.i = blockuse::IsUpsideDownStairPlacement(face, click[1], value.i != 0) ? 1 : 0;
                resolved++;
            } else if (stairs && bare == "weirdo_direction" && !text) {
                // the actor yaw of the last tick, as PlayerAuthInput sent it
                value.i = blockuse::StairDirection(pose.yaw);
                resolved++;
            } else if (pillar && text) {
                value.s = kAxis[blockuse::PillarAxis(face)];
                resolved++;
            }
        }
        // every state set by the rules above: an unknown extra state would be guessed
        if (resolved == 0 || resolved != static_cast<int>(states.size())) return false;
        std::sort(states.begin(), states.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        const uint16_t placed = palette.Intern(BlockPalette::HashState(name, states));
        const int32_t runtime = palette.RuntimeId(placed);
        if (palette.Name(placed) != name || palette.InternRuntime(runtime) != placed) return false;
        return PredictPlaceState(x, y, z, runtime, true);
    }

    bool BedrockSession::PredictPlaceState(int x, int y, int z, int32_t heldRuntimeId, bool resolved) {
        if (heldRuntimeId == 0)
            return false;
        BlockPalette& pal = BlockPalette::Instance();
        const uint16_t local = pal.InternRuntime(heldRuntimeId);
        const std::string name = pal.Name(local);
        if (local == 0 || name.empty() || name == "minecraft:air")
            return false;
        static const char* const kSkipNames[] = {"slab", "door", "bed", "chest", "sign", "banner", "skull", "head", "piston", "observer", "hopper",
                                                 "dropper", "dispenser", "water", "lava", "fire", "portal", "frame", "lectern", "cauldron",
                                                 "campfire", "candle", "lantern", "stairs", "trapdoor", "button", "lever", "rail", "torch",
                                                 "ladder", "vine", "anvil", "grindstone", "barrel", "furnace", "smoker", "blast_furnace",
                                                 "shulker", "command_block", "structure", "jigsaw", "end_rod", "lightning_rod", "bell",
                                                 "glazed_terracotta", "log", "wood", "stem", "hyphae", "pillar", "sapling", "flower", "tallgrass",
                                                 "double_plant", "leaves", "cake", "comparator", "repeater", "tripwire", "pressure_plate", "sea_pickle",
                                                 "turtle_egg", "snow_layer", "pointed_dripstone", "amethyst_cluster", "bud", "coral", "kelp", "seagrass"};
        static const char* const kSkipStates[] = {"facing_direction", "direction", "cardinal_direction", "vertical_half", "upside_down_bit",
                                                  "top_slot_bit", "pillar_axis", "attached_bit", "hanging", "open_bit", "powered_bit"};
        // a resolved state already went through the game's getPlacementBlock rules (PredictBridgePlace)
        if (!resolved) {
            for (const char* skip : kSkipNames)
                if (name.contains(skip))
                    return false;
            for (const auto& [key, value] : pal.States(local)) {
                (void)value;
                for (const char* skip : kSkipStates)
                    if (key.contains(skip))
                        return false;
            }
        }
        bool loaded = false;
        const uint16_t previous = netLevel.GetBlock(x, y, z, loaded);
        const std::string& previousName = pal.Name(previous);
        if (!blockuse::IsReplaceableCell(loaded, previous, previousName))
            return false;
        netLevel.SetBlock(x, y, z, local);
        std::lock_guard lock(predictMutex);
        if (predictedPlaces.size() > 64)
            predictedPlaces.erase(predictedPlaces.begin());
        predictedPlaces.push_back({x, y, z, local, previous, std::chrono::steady_clock::now()});
        return true;
    }

    void BedrockSession::ExpirePredictions() {
        std::vector<PredictedPlace> expired;
        {
            std::lock_guard lock(predictMutex);
            if (predictedPlaces.empty())
                return;
            const auto now = std::chrono::steady_clock::now();
            for (auto it = predictedPlaces.begin(); it != predictedPlaces.end();) {
                if (now - it->at > std::chrono::milliseconds(1500)) {
                    expired.push_back(*it);
                    it = predictedPlaces.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (const PredictedPlace& p : expired) {
            bool loaded = false;
            if (netLevel.GetBlock(p.x, p.y, p.z, loaded) == p.id && loaded) {
                netLevel.SetBlock(p.x, p.y, p.z, p.previous);
                Log("place", "the server never confirmed the block at ({},{},{}); reverted", p.x, p.y, p.z);
            }
        }
    }

    void BedrockSession::ConfirmPlace(int x, int y, int z, int32_t runtimeId) {
        {
            std::lock_guard lock(predictMutex);
            for (auto it = predictedPlaces.begin(); it != predictedPlaces.end(); ++it)
                if (it->x == x && it->y == y && it->z == z) {
                    predictedPlaces.erase(it);
                    break;
                }
        }
        int32_t held = 0;
        {
            std::lock_guard lock(placeMutex);
            if (!pendingPlace.active)
                return;
            if (std::chrono::steady_clock::now() - pendingPlace.at > std::chrono::seconds(2)) {
                pendingPlace.active = false;
                return;
            }
            if (pendingPlace.x != x || pendingPlace.y != y || pendingPlace.z != z)
                return;
            pendingPlace.active = false;
            held = pendingPlace.held;
        }
        {
            auto& pal = BlockPalette::Instance();
            Log("place", "server put block {} ({}) at ({},{},{}), we placed with block id {} ({}){}", runtimeId, pal.Name(pal.InternRuntime(runtimeId)), x, y, z, held, pal.Name(pal.InternRuntime(held)), (runtimeId != held ? "  <-- different" : ""));
        }
    }
}

namespace perlin {
    void BedrockSession::NoteUnknownBlock(int32_t networkId) {
        static std::mutex m;
        static std::vector<int32_t> seen;
        auto& pal = BlockPalette::Instance();
        if (!pal.Name(pal.InternRuntime(networkId)).empty())
            return;
        std::lock_guard lock(m);
        if (std::find(seen.begin(), seen.end(), networkId) != seen.end() || seen.size() > 200)
            return;
        seen.push_back(networkId);
        Log("block", "server sent block id {} that matches no known block state", networkId);
    }
}

namespace perlin {
    using namespace proto;
    void BedrockSession::EndItemUseOn() {
        if (!itemUseOnActive.exchange(false))
            return;
        PlayerActionPacket stop;
        stop.runtimeEntityId = runtimeId;
        stop.action = Action::StopItemUseOn;
        stop.position = {itemUseDest[0], itemUseDest[1], itemUseDest[2]};
        stop.resultPosition = {0, 0u, 0};
        stop.face = 0;
        Send(stop);
    }

    bool BedrockSession::OpenInventory() {
        InteractPacket p;
        p.actionId = InteractPacketActionId::OpenInventory;
        p.targetEntityId = runtimeId;
        p.hasPosition = false;
        Log("inventory", "open (Interact OpenInventory, runtime id {})", runtimeId);
        return Send(p);
    }

    bool BedrockSession::OpenMountInventory() {
        const int64_t uid = ridingUid.load();
        entity::Entity mount;
        if (uid == 0 || !entities.FindUnique(uid, mount))
            return false;
        InteractPacket p;
        p.actionId = InteractPacketActionId::OpenInventory;
        p.targetEntityId = mount.runtimeId;
        p.hasPosition = false;
        Log("inventory", "open mount inventory (target {})", mount.runtimeId);
        return Send(p);
    }

    bool BedrockSession::CloseInventory() {
        InvReturnCursor();
        {
            std::lock_guard lock(statusMutex);
            invCloseWanted = true;
        }
        InvPump();
        return true;
    }

    bool BedrockSession::SendCloseNow() {
        int window;
        {
            std::lock_guard lock(statusMutex);
            window = inventoryOpen ? inventoryWindow : 0;
            inventoryOpen = false;
            workbenchOpen = false;
        }
        ContainerClosePacket p;
        p.windowId = static_cast<WindowID>(window);
        p.windowType = WindowType::Inventory;
        p.server = false;
        Log("inventory", "close window {}", window);
        return Send(p);
    }

    bool BedrockSession::TakeServerClosedInventory() {
        std::lock_guard lock(statusMutex);
        const bool v = serverClosedInventory;
        serverClosedInventory = false;
        return v;
    }

    static void FillSlot(StackRequestSlotInfo& s, int slot, const ItemV4& raw) {
        s.slotType.containerId = ContainerSlotType::HotbarAndInventory;
        s.slot = static_cast<uint8_t>(slot);
        s.stackId = raw.stackId.value_or(0);
    }

    bool BedrockSession::SwapInventorySlots(int a, int b) {
        if (a == b || a < 0 || b < 0 || a > 35 || b > 35)
            return false;
        ItemStackRequestPacket p;
        ItemStackRequest r;
        ItemStackRequestActionsEntry act;
        {
            std::lock_guard lock(statusMutex);
            FillSlot(act.source, a, inventoryRaw[a]);
            FillSlot(act.destination, b, inventoryRaw[b]);
            if (inventoryRaw[a].networkId == 0 && inventoryRaw[b].networkId == 0)
                return false;
            std::swap(inventoryRaw[a], inventoryRaw[b]);
            auto at = [&](int i) -> ItemStackInfo& { return i < 9 ? inventory.hotbar[i] : inventory.main[i - 9]; };
            std::swap(at(a), at(b));
        }
        act.typeId = ItemStackRequestActionsEntryTypeId::Swap;
        act.legacyTypeId = static_cast<uint8_t>(ItemStackRequestActionsEntryTypeId::Swap);
        r.requestId = nextStackRequestId;
        nextStackRequestId -= 2;
        r.actions.push_back(act);
        r.cause = static_cast<ItemStackRequestCause>(-1);
        Log("inventory", "swap request {} {} <-> {}", r.requestId, a, b);
        p.requests.push_back(std::move(r));
        return Send(p);
    }

    bool BedrockSession::DropInventorySlot(int slot, bool whole) {
        if (slot < 0 || slot > 35)
            return false;
        return SendDropRequest(slot, whole ? 0 : 1, -1);
    }
}
