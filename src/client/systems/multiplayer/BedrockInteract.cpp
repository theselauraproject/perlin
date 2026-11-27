#include "client/systems/multiplayer/BedrockSession.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <iterator>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <vector>
#include "world/level/block/BlockPalette.hpp"
#include "client/systems/blocks/BlockUse.hpp"
#include "util/Log.hpp"

namespace perlin {
    using namespace proto;

    namespace {
        Vec3F V(const float p[3]) {
            return {p[0], p[1], p[2]};
        }

        int32_t NextLegacyItemUseId() {
            // The matched client's legacy IDs share a process-wide counter;
            // the first allocation is -4, separate from odd stack requests.
            static std::atomic<int32_t> counter{0};
            int32_t previous = counter.load(std::memory_order_relaxed);
            for (;;) {
                const int32_t base = std::min(previous, int32_t{-2});
                const int32_t next = std::bit_cast<int32_t>(static_cast<uint32_t>(base) - 2u);
                if (counter.compare_exchange_weak(previous, next, std::memory_order_relaxed))
                    return next;
            }
        }
    }

    bool BedrockSession::SendUseItemOnBlock(int bx, int by, int bz, int face, const float click[3], const float eye[3], int use, const int dest[3], bool repeat) {
        if (face < 0 || face > 5) return false;
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemUse;
        TransactionUseItem u;
        int selected = 0;
        bool onCooldown = false;
        {
            std::lock_guard lock(statusMutex);
            selected = std::clamp(inventory.selected, 0, 8);
            u.heldItem = useOffhand ? InvRawLocked(kInvOffhand) : inventoryRaw[selected];
            onCooldown = ItemOnCooldownLocked(u.heldItem);
        }
        if (use == 2 && (u.heldItem.count == 0 || u.heldItem.blockRuntimeId == 0))
            return false;
        if (use == 2) {
            static constexpr int offsets[6][3] = {{0, -1, 0}, {0, 1, 0},  {0, 0, -1},
                                                  {0, 0, 1},  {-1, 0, 0}, {1, 0, 0}};
            const bool same = dest[0] == bx && dest[1] == by && dest[2] == bz;
            if (!same && (int64_t(dest[0]) != int64_t(bx) + offsets[face][0] ||
                          int64_t(dest[1]) != int64_t(by) + offsets[face][1] ||
                          int64_t(dest[2]) != int64_t(bz) + offsets[face][2]))
                return false;
            bool loaded = false;
            const auto id = netLevel.GetBlock(dest[0], dest[1], dest[2], loaded);
            if (!blockuse::IsReplaceableCell(loaded, id, BlockPalette::Instance().Name(id)))
                return false;
        }
        u.actionType = TransactionUseItemActionType::ClickBlock;
        u.triggerType = repeat ? TransactionUseItemTriggerType::SimulationTick : TransactionUseItemTriggerType::PlayerInput;
        u.blockPosition = {bx, by, bz};
        u.face = static_cast<uint8_t>(face);
        u.hotbarSlot = selected;
        u.hand = useOffhand ? TransactionUseItemHand::OffHand : TransactionUseItemHand::MainHand;
        u.playerPos = haveLastAuthPos ? Vec3F{lastAuthPos[0], lastAuthPos[1] + kPlayerEyeHeight, lastAuthPos[2]} : V(eye);
        u.clickPos = V(click);
        {
            bool loaded = false;
            const uint16_t clicked = netLevel.GetBlock(bx, by, bz, loaded);
            if (use == 2 && !loaded)
                return false;
            u.blockRuntimeId =
                loaded ? static_cast<uint32_t>(BlockPalette::Instance().RuntimeId(clicked)) : 0;
        }
        haveLoggedInteraction = true;
        Log("place",
            "use item on ({},{},{}) face {} clicked id {} held {} x{} block {} use {} dest ({},{},{}) trigger={} afterAuthTick={}", bx,
            by, bz, face, u.blockRuntimeId, u.heldItem.networkId, static_cast<int>(u.heldItem.count),
            u.heldItem.blockRuntimeId,
            (use == 2   ? "place"
             : use == 1 ? "interact"
                        : "nothing"),
            dest[0], dest[1], dest[2], repeat ? "held-repeat" : "press", LastAuthTick());
        // A block's own interaction runs before ItemStack::useOn. Cooldown
        // prevents using the held block, but does not prevent opening a chest.
        const bool predictSuccess = use != 0 && !(use == 2 && onCooldown);
        if (predictSuccess && !itemUseOnActive.exchange(true)) {
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
        u.clientPrediction = predictSuccess ? TransactionUseItemClientPrediction::Success
                                            : TransactionUseItemClientPrediction::Failure;
        p.transaction.transactionData = u;
        if (!Send(p))
            return false;
        if (use == 2 && onCooldown)
            return false;
        if (use != 0) {
            itemUseDest[0] = dest[0];
            itemUseDest[1] = dest[1];
            itemUseDest[2] = dest[2];
        }
        if (use == 2) {
            const int mode = GameType() == 5 ? World().worldGameMode : GameType();
            const bool creative = mode == 1 || mode == 6;
            if (!creative) {
                std::lock_guard lock(statusMutex);
                const int held = useOffhand ? kInvOffhand : std::clamp(inventory.selected, 0, 8);
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
            pendingPlace = {dest[0], dest[1],
                            dest[2], std::chrono::steady_clock::now(),
                            true,    static_cast<int32_t>(u.heldItem.blockRuntimeId)};
        }
        return true;
    }

    bool BedrockSession::SendUseItemInAir(const float eye[3], bool held) {
        const net::context::ScopedSettings codecScope(PacketCodecSettings());
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemUse;
        TransactionUseItem u;
        const int32_t legacyId = NextLegacyItemUseId();
        const uint64_t worldAtUse = WorldSerial();
        int selected = 0, address = 0;
        bool offhand = false, immediate = false, consumes = false;
        bool onCooldown = false;
        uint64_t epoch = 0, slotRevision = 0, cooldownRevision = 0;
        ItemV4 after;
        std::vector<uint8_t> beforeBytes;
        std::optional<itemcooldown::Definition> cooldown;
        std::string cooldownKey;
        {
            std::lock_guard lock(statusMutex);
            selected = std::clamp(inventory.selected, 0, 8);
            offhand = useOffhand;
            address = offhand ? kInvOffhand : selected;
            u.heldItem = InvRawLocked(address);
            onCooldown = ItemOnCooldownLocked(u.heldItem, itemcooldown::Type::Use);
            const auto name = itemNames.find(u.heldItem.networkId);
            immediate = !onCooldown && u.heldItem.count > 0 && u.heldItem.networkId != 0 &&
                        name != itemNames.end() &&
                        (name->second == "minecraft:ender_pearl" || name->second == "minecraft:wind_charge");
            if (immediate) {
                epoch = itemStateRevision;
                slotRevision = itemSlotRevisions[address];
                if (!net::EncodeChecked(u.heldItem, beforeBytes))
                    return false;
                after = u.heldItem;
                consumes = !Instabuild();
                if (consumes) {
                    --after.count;
                    if (after.count == 0) {
                        after = ItemV4{};
                    } else {
                        after.hasStackId = true;
                        after.stackId = legacyId;
                        p.transaction.legacy.legacyRequestId = legacyId;
                        p.transaction.legacy.legacyTransactions.emplace();
                        TransactionLegacyLegacyTransactionsEntry changed;
                        changed.containerId = static_cast<uint8_t>(offhand ? ContainerSlotType::Offhand
                                                                          : ContainerSlotType::Inventory);
                        changed.changedSlots.push_back({static_cast<uint8_t>(offhand ? 0 : selected)});
                        p.transaction.legacy.legacyTransactions->push_back(std::move(changed));
                    }
                    TransactionActionsEntry change;
                    change.sourceType = TransactionActionsEntrySourceType::Container;
                    change.windowId = static_cast<int8_t>(offhand ? 119 : 0);
                    change.slot = static_cast<uint32_t>(offhand ? 0 : selected);
                    change.oldItem = u.heldItem;
                    change.newItem = after;
                    p.transaction.actions.push_back(std::move(change));
                }
                // Native use starts cooldown after consumption and only if the
                // post-use item remains nonempty. Instabuild retains that item.
                if (after.networkId != 0 && after.count > 0) {
                    if (const auto def = itemCooldownDefinitions.find(name->second);
                        def != itemCooldownDefinitions.end() && def->second.type == itemcooldown::Type::Use) {
                        cooldown = def->second;
                        cooldownKey = itemcooldown::CategoryKey(cooldown->category);
                        if (const auto revision = itemCooldownRevisions.find(cooldownKey);
                            revision != itemCooldownRevisions.end())
                            cooldownRevision = revision->second;
                    }
                }
            }
        }
        u.actionType = TransactionUseItemActionType::ClickAir;
        u.triggerType = TransactionUseItemTriggerType::Unknown;
        u.clientPrediction = TransactionUseItemClientPrediction::Failure;
        // The matched client's baseUseItem still sends a failed air-use attempt
        // with cooldown state Off; cooldown gates the local item-use action.
        u.blockPosition = {0, 0, 0};
        u.face = 255;
        u.hotbarSlot = offhand ? 0 : selected;
        u.hand = offhand ? TransactionUseItemHand::OffHand : TransactionUseItemHand::MainHand;
        u.playerPos = HeadPos(eye);
        u.clickPos = {0, 0, 0};
        u.blockRuntimeId = 0;
        if (ServerProtocol() == 2193) {
            // The matched client transaction serializer leaves descriptor
            // includeNetIds false. The local predicted stack keeps its even
            // legacy ID for subsequent ItemStackRequests; only these outgoing
            // descriptor copies omit it. Other profiles are not inferred from
            // this version's reflected serializer.
            const auto omitNetId = [](ItemV4& item) {
                item.hasStackId = false;
                item.stackId.reset();
            };
            omitNetId(u.heldItem);
            for (auto& action : p.transaction.actions) {
                omitNetId(action.oldItem);
                omitNetId(action.newItem);
            }
        }
        p.transaction.transactionData = u;
        if (!Send(p))
            return false;
        if (immediate) {
            std::lock_guard lock(statusMutex);
            if (epoch == itemStateRevision && worldAtUse == WorldSerial()) {
                if (consumes && slotRevision == itemSlotRevisions[address]) {
                    std::vector<uint8_t> currentBytes;
                    if (net::EncodeChecked(InvRawLocked(address), currentBytes) && currentBytes == beforeBytes) {
                        InvRawLocked(address) = after;
                        InvInfoLocked(address) = ConvertItemStack(after);
                        ++itemSlotRevisions[address];
                    }
                }
                const auto revision = itemCooldownRevisions.find(cooldownKey);
                const uint64_t currentRevision = revision == itemCooldownRevisions.end() ? 0 : revision->second;
                if (cooldown && cooldownRevision == currentRevision) {
                    itemCooldowns.Start(cooldown->category, cooldown->durationTicks);
                    ++itemCooldownRevisions[cooldownKey];
                }
            }
        }
        if (held && !onCooldown && !immediate)
            QueueInputFlag(InputFlag::StartUsingItem);
        return true;
    }

    bool BedrockSession::SendReleaseItem(const float eye[3]) {
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemRelease;
        TransactionTransactionDataCase2 r;
        {
            std::lock_guard lock(statusMutex);
            const int selected = std::clamp(inventory.selected, 0, 8);
            r.hotbarSlot = useOffhand ? 0 : selected;
            r.heldItem = useOffhand ? InvRawLocked(kInvOffhand) : inventoryRaw[selected];
        }
        r.hand = useOffhand ? 1 : 0;
        r.actionType = TransactionTransactionDataCase2ActionType::Release;
        r.headPos = HeadPos(eye);
        p.transaction.transactionData = r;
        return Send(p);
    }

    bool BedrockSession::SendAttackEntity(uint64_t targetRuntimeId, const float eye[3],
                                          const float click[3]) {
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemUseOnEntity;
        TransactionTransactionDataCase1 a;
        {
            std::lock_guard lock(statusMutex);
            a.hotbarSlot = std::clamp(inventory.selected, 0, 8);
            a.heldItem = attackOffhand ? InvRawLocked(kInvOffhand) : inventoryRaw[a.hotbarSlot];
        }
        a.hand = attackOffhand ? 1 : 0;
        a.entityRuntimeId = targetRuntimeId;
        a.actionType = TransactionTransactionDataCase1ActionType::Attack;
        a.playerPos = V(eye);
        a.clickPos = V(click);
        p.transaction.transactionData = a;
        return Send(p);
    }

    bool BedrockSession::SendInteractEntity(uint64_t targetRuntimeId, const float eye[3],
                                            const float click[3]) {
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemUseOnEntity;
        TransactionTransactionDataCase1 a;
        {
            std::lock_guard lock(statusMutex);
            a.hotbarSlot = std::clamp(inventory.selected, 0, 8);
            a.heldItem = useOffhand ? InvRawLocked(kInvOffhand) : inventoryRaw[a.hotbarSlot];
        }
        a.hand = useOffhand ? 1 : 0;
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
        if (slot < 0 || slot >= 36)
            return false;
        if (legacyInventory) {
            InventoryTransactionPacket tx;
            tx.transaction.transactionType = TransactionTransactionType::Normal;
            {
                std::lock_guard lock(statusMutex);
                const ItemV4 was = inventoryRaw[slot];
                if (was.networkId == 0 || was.count == 0)
                    return false;
                const int n = count <= 0 ? was.count : std::min<int>(count, was.count);
                const auto bare = [](ItemV4 it) {
                    if (it.hasStackId && it.stackId.value_or(0) <= 0) {
                        it.hasStackId = false;
                        it.stackId.reset();
                    }
                    return it;
                };
                ItemV4 left = was, dropped = was;
                left.count = static_cast<decltype(left.count)>(was.count - n);
                dropped.count = static_cast<decltype(dropped.count)>(n);
                if (left.count == 0)
                    left = ItemV4{};
                TransactionActionsEntry c;
                c.sourceType = TransactionActionsEntrySourceType::Container;
                c.windowId = static_cast<int8_t>(0);
                c.slot = static_cast<uint32_t>(slot);
                c.oldItem = bare(was);
                c.newItem = bare(left);
                tx.transaction.actions.push_back(std::move(c));
                TransactionActionsEntry w;
                w.sourceType = TransactionActionsEntrySourceType::WorldInteraction;
                w.flags = 0u;
                w.slot = 0;
                w.oldItem = ItemV4{};
                w.newItem = bare(dropped);
                tx.transaction.actions.push_back(std::move(w));
                ItemStackInfo& shown = slot < 9 ? inventory.hotbar[slot] : inventory.main[slot - 9];
                if (left.count == 0 || left.networkId == 0) {
                    shown = ItemStackInfo{};
                    inventoryRaw[slot] = ItemV4{};
                } else {
                    shown.count = left.count;
                    inventoryRaw[slot] = left;
                }
            }
            Log("inventory", "legacy drop slot {} count {}", slot, count);
            return Send(tx);
        }
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
                a.source.slotType.containerId =
                    slot < 9 ? ContainerSlotType::Hotbar : ContainerSlotType::Inventory;
                a.source.slot = static_cast<uint8_t>(slot < 9 ? slot : slot - 9);
            } else {
                a.source.slotType.containerId = ContainerSlotType::HotbarAndInventory;
                a.source.slot = static_cast<uint8_t>(slot);
            }
            a.source.stackId = attempt >= 2 ? 0 : raw.stackId.value_or(0);
            a.count = static_cast<uint8_t>(count <= 0 ? std::min<int>(raw.count, 255)
                                                      : std::min<int>(count, raw.count));
            r.requestId = nextStackRequestId;
            nextStackRequestId -= 2;
            {
                InvSnapshot snapshot;
                snapshot.view = inventory;
                std::copy(std::begin(inventoryRaw), std::end(inventoryRaw), std::begin(snapshot.raw));
                std::copy(std::begin(extraRaw), std::end(extraRaw), std::begin(snapshot.extra));
                std::copy(std::begin(chestRaw), std::end(chestRaw), std::begin(snapshot.chest));
                std::copy(std::begin(horseRaw), std::end(horseRaw), std::begin(snapshot.horse));
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
        Log("inventory", "drop request {} slot {} stack {} count {} form {}", r.requestId, slot,
            a.source.stackId, static_cast<int>(a.count), attempt);
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

    bool BedrockSession::PredictBridgePlace(int x, int y, int z, int32_t heldRuntimeId, int face,
                                            const float click[3]) {
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
        if (face < 0 || face > 5 || !std::isfinite(click[1]))
            return false;
        const bool upper = blockuse::IsUpperSlabPlacement(face, click[1]);
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
                value.i = blockuse::StairDirection(pose.yaw);
                resolved++;
            } else if (pillar && text) {
                value.s = kAxis[blockuse::PillarAxis(face)];
                resolved++;
            }
        }
        if (resolved == 0 || resolved != static_cast<int>(states.size())) return false;
        std::ranges::sort(states, [](const auto& a, const auto& b) { return a.first < b.first; });
        const uint16_t placed = palette.Intern(BlockPalette::HashState(name, states));
        const int32_t runtime = palette.RuntimeId(placed);
        if (palette.Name(placed) != name || palette.InternRuntime(runtime) != placed)
            return false;
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
            Log("place", "server put block {} ({}) at ({},{},{}), we placed with block id {} ({}){}",
                runtimeId, pal.Name(pal.InternRuntime(runtimeId)), x, y, z, held,
                pal.Name(pal.InternRuntime(held)), (runtimeId != held ? "  <-- different" : ""));
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
        if (std::ranges::find(seen, networkId) != seen.end() || seen.size() > 200)
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
        ownInventoryAskedAt = std::chrono::steady_clock::now().time_since_epoch().count();
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

    void BedrockSession::ResetContainerStateLocked() {
        inventoryOpen = false;
        serverClosedInventory = false;
        workbenchOpen = false;
        workbenchJustOpened = false;
        horseJustOpened = false;
        chestJustOpened = false;
        invCloseWanted = false;
        inventoryWindow = 0;
        inventoryWindowType = WindowType::Inventory;
        serverCloseWindowType = WindowType::None;
        inventory.horseOpen = false;
        horseWindow = -1;
        inventory.chestOpen = false;
        inventory.furnaceKind = -1;
        chestWindow = -1;
        pendingWindows.clear();
        for (auto& item : inventory.work)
            item = ItemStackInfo{};
    }

    bool BedrockSession::SendCloseNow() {
        int window;
        {
            std::lock_guard lock(statusMutex);
            if (!inventoryOpen)
                return true;
            window = inventoryWindow;
            ResetContainerStateLocked();
        }
        ContainerClosePacket p;
        p.windowId = static_cast<WindowID>(window);
        p.windowType = WindowType::None;
        p.server = false;
        Log("inventory", "close window {}", window);
        return Send(p);
    }

    bool BedrockSession::TakeServerClosedInventory() {
        std::lock_guard lock(statusMutex);
        if (!serverClosedInventory || !inventoryOpen || inventoryWindowType != serverCloseWindowType)
            return false;
        serverClosedInventory = false;
        return true;
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
            auto at = [&](int i) -> ItemStackInfo& {
                return i < 9 ? inventory.hotbar[i] : inventory.main[i - 9];
            };
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
