#include <set>
#include "client/multiplayer/ItemNbtInfo.hpp"
#include <array>
#include "util/Env.hpp"
#include <cstdlib>
#include "client/multiplayer/BedrockSession.hpp"
#include "client/player/PlayerPhysics.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string_view>
#include "util/Log.hpp"

namespace perlin {
    using namespace proto;

    namespace {
        bool FindNbtInt(const std::vector<uint8_t>& nbt, const char* name, int& out) {
            const size_t n = std::strlen(name);
            for (size_t i = 0; i + n + 3 <= nbt.size(); i++) {
                if (nbt[i] != 0x03)
                    continue;
                if (i + 3 + n + 4 <= nbt.size() && nbt[i + 1] == n && nbt[i + 2] == 0 &&
                    std::memcmp(&nbt[i + 3], name, n) == 0) {
                    int32_t v;
                    std::memcpy(&v, &nbt[i + 3 + n], 4);
                    out = v;
                    return true;
                }
                if (i + 2 + n < nbt.size() && nbt[i + 1] == n && std::memcmp(&nbt[i + 2], name, n) == 0) {
                    uint32_t raw = 0;
                    int shift = 0;
                    size_t p = i + 2 + n;
                    while (p < nbt.size() && shift < 35) {
                        const uint8_t b = nbt[p++];
                        raw |= static_cast<uint32_t>(b & 0x7f) << shift;
                        if (!(b & 0x80))
                            break;
                        shift += 7;
                    }
                    out = static_cast<int>((raw >> 1) ^ (~(raw & 1) + 1));
                    return true;
                }
            }
            return false;
        }

        bool FindNbtString(const std::vector<uint8_t>& nbt, const char* name, std::string& out) {
            const size_t n = std::strlen(name);
            for (size_t i = 0; i + n + 4 <= nbt.size(); i++) {
                if (nbt[i] != 0x08)
                    continue;
                size_t p = 0;
                if (nbt[i + 1] == n && nbt[i + 2] == 0 && std::memcmp(&nbt[i + 3], name, n) == 0)
                    p = i + 3 + n;
                else if (nbt[i + 1] == n && std::memcmp(&nbt[i + 2], name, n) == 0)
                    p = i + 2 + n;
                else
                    continue;
                size_t len = 0;
                if (p + 2 <= nbt.size() && nbt[i + 2] == 0 && nbt[i + 1] == n) {
                    len = nbt[p] | (static_cast<size_t>(nbt[p + 1]) << 8);
                    p += 2;
                } else {
                    int shift = 0;
                    while (p < nbt.size() && shift < 35) {
                        const uint8_t b = nbt[p++];
                        len |= static_cast<size_t>(b & 0x7f) << shift;
                        if (!(b & 0x80))
                            break;
                        shift += 7;
                    }
                }
                if (len == 0 || p + len > nbt.size() || len > 256)
                    continue;
                out.assign(reinterpret_cast<const char*>(&nbt[p]), len);
                return true;
            }
            return false;
        }

        bool Contains(const std::vector<uint8_t>& bytes, const char* text) {
            const size_t n = std::strlen(text);
            if (bytes.size() < n)
                return false;
            return std::search(bytes.begin(), bytes.end(), text, text + n) != bytes.end();
        }

        const std::vector<uint8_t>* ItemNbt(const ItemV4& item) {
            if (const auto* with = std::get_if<ItemExtraDataWithBlockingTick>(&item.extra))
                return with->nbt ? &with->nbt->nbt.bytes : nullptr;
            if (const auto* without = std::get_if<ItemExtraDataWithoutBlockingTick>(&item.extra))
                return without->nbt ? &without->nbt->nbt.bytes : nullptr;
            return nullptr;
        }

        bool IntegerOf(const MetadataDictionaryEntry& e, int& out) {
            bool found = false;
            std::visit(
                [&](const auto& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, int64_t>) {
                        out = static_cast<int>(v);
                        found = true;
                    } else if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, float>) {
                    } else {
                        std::visit(
                            [&](const auto& inner) {
                                using U = std::decay_t<decltype(inner)>;
                                if constexpr (std::is_same_v<U, int8_t> || std::is_same_v<U, int16_t> ||
                                              std::is_same_v<U, int32_t> || std::is_same_v<U, int64_t>) {
                                    out = static_cast<int>(inner);
                                    found = true;
                                }
                            },
                            v);
                    }
                },
                e.value);
            return found;
        }
    }

    void BedrockSession::ResetPlayerState() {
        {
            std::lock_guard locatorLock(locatorMutex);
            locatorWaypoints.clear();
        }
        std::lock_guard lock(statusMutex);
        vitals = Vitals{};
        activeEffects.clear();
        pendingEffectUpdates.clear();
        serverMotions.clear();
        pendingMovementEffects.clear();
        inventory = InventoryView{};
        for (ItemV4& raw : inventoryRaw)
            raw = ItemV4{};
        for (ItemV4& raw : extraRaw)
            raw = ItemV4{};
        invQueue.clear();
        invSnapshots.clear();
        ridingUid = 0;
        invPendingRequest = 0;
        invInFlight.clear();
        immobile = false;
        recipes.clear();
        craftedRecipe = -1;
        titleText = TitleText{};
    }

    BedrockSession::Vitals BedrockSession::GetVitals() const {
        std::lock_guard lock(statusMutex);
        return vitals;
    }

    BedrockSession::InventoryView BedrockSession::GetInventory() const {
        std::lock_guard lock(statusMutex);
        return inventory;
    }

    std::vector<BedrockSession::ServerMovementEffect> BedrockSession::TakeMovementEffects() {
        std::lock_guard lock(statusMutex);
        std::vector<ServerMovementEffect> effects;
        effects.swap(pendingMovementEffects);
        return effects;
    }

    std::vector<BedrockSession::EffectUpdate> BedrockSession::TakeEffectUpdates() {
        std::lock_guard lock(statusMutex);
        std::vector<EffectUpdate> updates;
        updates.swap(pendingEffectUpdates);
        return updates;
    }

    int BedrockSession::WaterWalkSpeedEnchant() const {
        std::lock_guard lock(statusMutex);
        int level = 0;
        for (const auto& item : inventory.armor)
            for (const auto& [id, enchantLevel] : item.enchants)
                if (id == 7)
                    level = std::max(level, enchantLevel);
        return level;
    }

    int BedrockSession::SoulSpeedEnchant() const {
        std::lock_guard lock(statusMutex);
        int level = 0;
        // boots only: the game reads armor slot 3 for SoulSpeedEnchantFlagComponent (0x1437e2f60)
        for (const auto& [id, enchantLevel] : inventory.armor[3].enchants)
            if (id == 36)
                level = std::max(level, enchantLevel);
        return level;
    }

    int BedrockSession::SwiftSneakEnchant() const {
        std::lock_guard lock(statusMutex);
        int level = 0;
        // leggings only: armor slot 2, enchant 37 for SwiftSneakEnchantComponent (0x1437e2f60)
        for (const auto& [id, enchantLevel] : inventory.armor[2].enchants)
            if (id == 37)
                level = std::max(level, enchantLevel);
        return level;
    }

    namespace {
        std::vector<physics::AttributeModifierValue> ModifierValues(
            const std::vector<PlayerAttributesEntryModifiersEntry>& modifiers) {
            std::vector<physics::AttributeModifierValue> values;
            values.reserve(modifiers.size());
            for (const auto& m : modifiers)
                values.push_back({m.amount, m.operation, m.operand});
            return values;
        }

        // hasModifier 0x142bf14a0: uuid, name, operation and operand; the uuid is parsed, so case does not matter
        bool IsSprintModifier(const PlayerAttributesEntryModifiersEntry& m) {
            static constexpr std::string_view kSprintUuid = "d208fc00-42aa-4aad-9276-d5446530de43";
            if (m.id.size() != kSprintUuid.size() || m.name != "Sprinting speed boost" || m.operation != 2 ||
                m.operand != 2)
                return false;
            for (size_t i = 0; i < kSprintUuid.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(m.id[i])) != kSprintUuid[i])
                    return false;
            return true;
        }
    }

    // client AttributeData apply 0x142bf4780: modifiers replaced, value set to the sent one, then base and limits
    void BedrockSession::ApplyMovementAttribute(const PlayerAttributesEntry& entry) {
        movementModifiers = entry.modifiers;
        const float baseMin = std::fmin(entry.defaultMax, entry.defaultMin);
        movementBase = {baseMin, entry.defaultMax,
                        entry.defaultMax < entry.default_ ? entry.defaultMax : std::fmax(entry.default_, baseMin)};
        movementLimits = {std::fmin(entry.max, entry.min), entry.max};
        movementValue = physics::ClampAttribute(entry.current, movementLimits[0], movementLimits[1],
                                                ModifierValues(movementModifiers));
    }

    // ActionSprintTriggerSystem 0x14b346c80 → 0x14b319130: the sprint modifier (0.3, multiply-total) is appended on
    // start, removed on stop; each change recalculates from the base, otherwise the value stays as sent
    float BedrockSession::PredictedMovementSpeed(bool sprinting, std::optional<float> freezeStrength) {
        std::lock_guard lock(attrMutex);
        // FreezingSystem 0x145E14260: "Freeze effect" = strength * -0.05 (addition on the value), removed at
        // |strength| <= 2^-23 and re-appended on every change
        std::optional<float> freeze;
        if (freezeStrength && std::fabs(*freezeStrength) > 0.00000011920929f)
            freeze = *freezeStrength * -0.05f;
        if (!movementAttribute) {
            const float factor = 0.3f + 1.0f;
            const float value = freeze ? *freeze + 0.1f : 0.1f;
            return sprinting ? factor * value : value;
        }
        // the server's freeze modifier lags a tick behind its move: ours replaces it when they differ
        const auto withFreeze = [&](float value) {
            if (!freezeStrength)
                return value;
            const auto isFreeze = [](const PlayerAttributesEntryModifiersEntry& m) { return m.name == "Freeze effect"; };
            std::optional<float> sent;
            for (const auto& m : movementModifiers)
                if (isFreeze(m))
                    sent = m.amount;
            if (sent == freeze)
                return value;
            std::vector<PlayerAttributesEntryModifiersEntry> modifiers = movementModifiers;
            std::erase_if(modifiers, isFreeze);
            auto values = ModifierValues(modifiers);
            if (freeze)
                values.push_back({*freeze, 0, 2});
            std::array<float, 2> limits = movementLimits;
            return physics::RecalculateAttribute(movementBase, values, value, limits);
        };
        const bool present = std::any_of(movementModifiers.begin(), movementModifiers.end(), IsSprintModifier);
        if (sprinting == present)
            return withFreeze(movementValue);
        if (sprinting) {
            PlayerAttributesEntryModifiersEntry boost;
            boost.id = "d208fc00-42aa-4aad-9276-d5446530de43";
            boost.name = "Sprinting speed boost";
            boost.amount = 0.3f;
            boost.operation = 2;
            boost.operand = 2;
            boost.serializable = false;
            movementModifiers.push_back(boost);
        } else {
            std::erase_if(movementModifiers, IsSprintModifier);
        }
        movementValue = physics::RecalculateAttribute(movementBase, ModifierValues(movementModifiers), movementValue,
                                                      movementLimits);
        return withFreeze(movementValue);
    }

    BedrockSession::TitleText BedrockSession::GetTitleText() const {
        std::lock_guard lock(statusMutex);
        return titleText;
    }

    bool BedrockSession::SelectHotbarSlot(int slot) {
        if (slot < 0 || slot > 8)
            return false;
        MobEquipmentPacket p;
        {
            std::lock_guard lock(statusMutex);
            inventory.selected = slot;
            p.item = inventoryRaw[slot];
        }
        p.runtimeEntityId = runtimeId;
        p.slot = static_cast<uint8_t>(slot);
        p.selectedSlot = static_cast<uint8_t>(slot);
        p.windowId = WindowID::Inventory;
        return Send(p);
    }

    bool BedrockSession::SendSwingArm(const char* source) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastSwingAt < std::chrono::milliseconds(150))
            return false;
        lastSwingAt = now;
        AnimatePacket p;
        p.actionId = AnimatePacketActionId::SwingArm;
        p.runtimeEntityId = runtimeId;
        p.hasSwingSource = true;
        p.swingSource = source;
        return Send(p);
    }

    void BedrockSession::InjectClientPacket(uint32_t id, const std::vector<uint8_t>& body) {
        if (!replayMode)
            return;
        if (!replayClientReady) {
            replayClientReady = true;
            replayClientDispatcher.On<AnimatePacket>([this](const AnimatePacket& p) {
                if (p.actionId != AnimatePacketActionId::SwingArm)
                    return;
                ReplayClientEvent e;
                e.kind = ReplayClientEvent::Kind::Swing;
                replayClientEvents.push_back(e);
            });
            replayClientDispatcher.On<MobEquipmentPacket>([this](const MobEquipmentPacket& p) {
                ReplayClientEvent e;
                e.kind = ReplayClientEvent::Kind::Hotbar;
                e.slot = p.selectedSlot;
                replayClientEvents.push_back(e);
            });
            replayClientDispatcher.On<PlayerActionPacket>([this](const PlayerActionPacket& p) {
                ReplayClientEvent e;
                e.kind = ReplayClientEvent::Kind::BlockAction;
                e.action = static_cast<uint32_t>(p.action);
                e.x = p.position.x;
                e.y = p.position.y;
                e.z = p.position.z;
                e.face = p.face;
                replayClientEvents.push_back(e);
            });
            replayClientDispatcher.On<PlayerAuthInputPacket>([this](const auto& p) {
                const auto each = [&](const auto& list) {
                    for (const auto& a : list) {
                        ReplayClientEvent e;
                        e.kind = ReplayClientEvent::Kind::BlockAction;
                        e.action = static_cast<uint32_t>(a.action);
                        e.x = a.position.x;
                        e.y = a.position.y;
                        e.z = a.position.z;
                        e.face = a.face;
                        replayClientEvents.push_back(e);
                    }
                };
                if constexpr (requires { p.blockAction.has_value(); }) {
                    if (p.blockAction.has_value())
                        each(*p.blockAction);
                } else {
                    each(p.blockAction);
                }
            });
        }
        replayClientDispatcher.Dispatch(id, body.data(), body.size());
    }

    void BedrockSession::RegisterPlayerStateHandlers() {
        const auto convert = [this](const ItemV4& raw) {
            ItemStackInfo info;
            if (raw.networkId == 0 || raw.count == 0)
                return info;
            auto it = itemNames.find(raw.networkId);
            info.name = it == itemNames.end() ? std::string("minecraft:unknown") : it->second;
            if (it == itemNames.end()) {
                static std::set<int> reported;
                if (reported.insert(raw.networkId).second)
                    Log("items", "unknown network id {} (registry has {} items)", raw.networkId, itemNames.size());
            }
            info.networkId = raw.networkId;
            info.count = raw.count;
            info.aux = static_cast<int>(raw.metadata);
            info.blockRuntimeId = raw.blockRuntimeId;
            if (raw.blockRuntimeId != 0 && it != itemNames.end()) {
                auto& pal = BlockPalette::Instance();
                const uint16_t local = pal.InternRuntime(static_cast<int32_t>(raw.blockRuntimeId));
                const int32_t hash = pal.NetworkId(local);
                if (local != 0 && pal.Name(local).empty() && pal.LearnName(hash, it->second)) {
                    Log("blocks", "learned {} for block id {} from an item stack", it->second, hash);
                    blockNamesLearned = true;
                }
            }
            if (const auto* nbt = ItemNbt(raw)) {
                int damage = 0;
                FindNbtString(*nbt, "Name", info.customName);
                if (FindNbtInt(*nbt, "Damage", damage))
                    info.damage = std::max(0, damage);
                const ItemNbtInfo parsed = ParseItemNbt(*nbt);
                if (parsed.ok) {
                    info.nbtTags = parsed.tagCount;
                    info.hasColor = parsed.hasColor;
                    info.color = parsed.color;
                    info.enchants = parsed.enchants;
                    info.lore = parsed.lore;
                    if (!parsed.customName.empty())
                        info.customName = parsed.customName;
                    if (parsed.damage >= 0)
                        info.damage = parsed.damage;
                }
                info.enchanted = !info.enchants.empty() || Contains(*nbt, "ench") || Contains(*nbt, "Enchantments") || Contains(*nbt, "StoredEnchantments") ||
                                 Contains(*nbt, "minecraft:enchantments");
            }
            static const char* const kAlwaysGlint[] = {"minecraft:enchanted_golden_apple", "minecraft:experience_bottle",
                                                       "minecraft:nether_star", "minecraft:end_crystal", "minecraft:enchanted_book"};
            for (const char* name : kAlwaysGlint)
                if (info.name == name)
                    info.enchanted = true;
            return info;
        };

        const auto store = [this, convert](const WindowIDVarint window, const std::optional<FullContainerName>& container,
                                           int firstSlot, const std::vector<ItemV4>& items, bool whole) {
            std::lock_guard lock(statusMutex);
            const ContainerSlotType type = container ? container->containerId : ContainerSlotType::HotbarAndInventory;
            const bool offhand = window == WindowIDVarint::Offhand || type == ContainerSlotType::Offhand;
            const bool armor = window == WindowIDVarint::Armor || type == ContainerSlotType::Armor;
            const bool playerInventory = window == WindowIDVarint::Inventory || window == WindowIDVarint::Hotbar ||
                                         window == WindowIDVarint::FixedInventory || type == ContainerSlotType::Hotbar ||
                                         type == ContainerSlotType::Inventory || type == ContainerSlotType::HotbarAndInventory;
            static const bool debug = PERLIN_ENV("PERLIN_INV_DEBUG") != nullptr;
            if (debug)
                Log("inv", "window {} container {} first {} n {}{}", static_cast<uint32_t>(window), static_cast<int>(type), firstSlot, items.size(), (whole ? " (content)" : " (slot)"));
            const bool mainOnly = type == ContainerSlotType::Inventory && whole && items.size() == 27;
            const bool uiWindow = window == WindowIDVarint::Ui || type == ContainerSlotType::Cursor || type == ContainerSlotType::CraftingInput ||
                                  type == ContainerSlotType::CraftingOutput || type == ContainerSlotType::CreativeOutput;
            const bool horseWin = inventory.horseOpen && horseWindow > 0 && static_cast<int>(window) == horseWindow;
            const bool chestWin = inventory.chestOpen && chestWindow >= 0 && static_cast<int>(window) == chestWindow;
            if (chestWin && whole)
                inventory.chestSlots = std::clamp(static_cast<int>(items.size()), 0, kInvChestCount);
            for (size_t i = 0; i < items.size(); i++) {
                if (chestWin) {
                    const int c = firstSlot + static_cast<int>(i);
                    if (c >= 0 && c < kInvChestCount) {
                        inventory.chest[c] = convert(items[i]);
                        chestRaw[c] = items[i];
                    }
                    continue;
                }
                if (horseWin) {
                    const int rawSlot = firstSlot + static_cast<int>(i);
                    const int h = type == ContainerSlotType::HorseEquip ? rawSlot : type == ContainerSlotType::Container ? rawSlot + 2 : rawSlot;
                    if (h >= 0 && h < kInvHorseCount) {
                        inventory.horse[h] = convert(items[i]);
                        horseRaw[h] = items[i];
                        if (h >= 2)
                            inventory.horseChestSlots = std::max(inventory.horseChestSlots, h - 1);
                    }
                    continue;
                }
                const int slot = firstSlot + static_cast<int>(i) + (type == ContainerSlotType::Inventory && !whole && firstSlot < 27 ? 9 : 0);
                const ItemStackInfo info = convert(items[i]);
                if (uiWindow) {
                    int addr = -1;
                    if (type == ContainerSlotType::Cursor || (window == WindowIDVarint::Ui && !container && slot == 0))
                        addr = kInvCursor;
                    else if (slot >= 28 && slot <= 31 && (type == ContainerSlotType::CraftingInput || window == WindowIDVarint::Ui))
                        addr = kInvCraft0 + slot - 28;
                    else if (slot >= 32 && slot <= 40 && (type == ContainerSlotType::CraftingInput || window == WindowIDVarint::Ui))
                        addr = kInvWork0 + slot - 32;
                    else if (type == ContainerSlotType::CraftingOutput || type == ContainerSlotType::CreativeOutput)
                        addr = kInvOutput;
                    if (addr >= 0) {
                        InvInfoLocked(addr) = info;
                        InvRawLocked(addr) = items[i];
                        if (IsCraftAddr(addr))
                            InvRecomputeOutputLocked();
                    }
                } else if (offhand) {
                    if (slot == 0) { inventory.offhand = info; extraRaw[kInvOffhand - 36] = items[i]; }
                } else if (armor) {
                    if (slot >= 0 && slot < 4) { inventory.armor[slot] = info; extraRaw[kInvArmor0 - 36 + slot] = items[i]; }
                } else if (playerInventory && window != WindowIDVarint::Ui) {
                    const auto stamp = std::chrono::steady_clock::now();
                    if (mainOnly) {
                        if (slot >= 0 && slot < 27) { inventory.main[slot] = info; inventoryRaw[slot + 9] = items[i]; invServerAt[slot + 9] = stamp; }
                    } else if (slot >= 0 && slot < 9) {
                        inventory.hotbar[slot] = info;
                        inventoryRaw[slot] = items[i];
                        invServerAt[slot] = stamp;
                    } else if (slot >= 9 && slot < 36) {
                        inventory.main[slot - 9] = info;
                        inventoryRaw[slot] = items[i];
                        invServerAt[slot] = stamp;
                    }
                }
            }
        };

        On<InventoryContentPacket>([store](const InventoryContentPacket& p) {
            store(p.windowId, p.container, 0, p.input, true);
        });
        On<InventorySlotPacket>([store](const InventorySlotPacket& p) {
            store(p.windowId, p.container, static_cast<int>(p.slot), std::vector<ItemV4>{p.item}, false);
        });

        On<InventoryTransactionPacket>([store](const InventoryTransactionPacket& p) {
            static int logged = 0;
            if (logged < 12) {
                logged++;
                std::cout << "[inventory] server transaction type " << static_cast<int>(p.transaction.transactionType) << " actions "
                          << p.transaction.actions.size();
                for (const TransactionActionsEntry& a : p.transaction.actions)
                    std::cout << " (src " << static_cast<uint32_t>(a.sourceType) << " win " << (a.windowId ? static_cast<int>(*a.windowId) : -1)
                              << " slot " << a.slot << " new " << a.newItem.networkId << "x" << a.newItem.count << ")";
                std::cout << std::endl;
            }
            if (p.transaction.transactionType != TransactionTransactionType::Normal)
                return;
            for (const TransactionActionsEntry& a : p.transaction.actions) {
                if (a.sourceType != TransactionActionsEntrySourceType::Container)
                    continue;
                const int window = a.windowId ? static_cast<int>(*a.windowId) : 0;
                WindowIDVarint w;
                switch (window) {
                case 0: w = WindowIDVarint::Inventory; break;
                case 119: w = WindowIDVarint::Offhand; break;
                case 120: w = WindowIDVarint::Armor; break;
                case 122: w = WindowIDVarint::Hotbar; break;
                case 123: w = WindowIDVarint::FixedInventory; break;
                default: continue;
                }
                store(w, std::nullopt, static_cast<int>(a.slot), std::vector<ItemV4>{a.newItem}, false);
            }
        });

        On<ItemStackResponsePacket>([this](const ItemStackResponsePacket& p) {
            std::vector<std::array<int, 3>> retries;
            {
            std::lock_guard lock(statusMutex);
            static const bool debug = PERLIN_ENV("PERLIN_INV_DEBUG") != nullptr;
            for (const ItemStackResponsesEntry& r : p.responses) {
                if (const auto pending = pendingDrops.find(r.requestId); pending != pendingDrops.end()) {
                    if (r.status == ItemStackResponsesEntryStatus::Ok) {
                        dropForm = pending->second.attempt;
                    } else if (pending->second.attempt < 2) {
                        retries.push_back({pending->second.slot, pending->second.count, pending->second.attempt + 1});
                    }
                    pendingDrops.erase(pending);
                }
                if (r.status != ItemStackResponsesEntryStatus::Ok)
                    Log("inventory", "request {} rejected, status {}:{}", r.requestId, static_cast<int>(r.status), (invSummaries.count(r.requestId) ? invSummaries[r.requestId] : std::string()));
                else if (debug)
                    Log("inventory", "request {} ok", r.requestId);
                std::optional<InvSnapshot> before;
                if (const auto snap = invSnapshots.find(r.requestId); snap != invSnapshots.end()) {
                    if (r.status == ItemStackResponsesEntryStatus::Ok)
                        before = snap->second;
                    else {
                        invSnapshots.erase(invSnapshots.begin(), snap);
                        const int selected = inventory.selected;
                        inventory = snap->second.view;
                        inventory.selected = selected;
                        std::copy(std::begin(snap->second.raw), std::end(snap->second.raw), std::begin(inventoryRaw));
                        std::copy(std::begin(snap->second.extra), std::end(snap->second.extra), std::begin(extraRaw));
                        std::copy(std::begin(snap->second.chest), std::end(snap->second.chest), std::begin(chestRaw));
                        InvRecomputeOutputLocked();
                    }
                    invSnapshots.erase(snap);
                }
                invInFlight.erase(r.requestId);
                const bool newerPending = !invSnapshots.empty() && invSnapshots.begin()->first < r.requestId;
                if (r.status != ItemStackResponsesEntryStatus::Ok || !r.containers || newerPending)
                    continue;
                for (const auto& c : *r.containers) {
                    const ContainerSlotType type = c.slotType.containerId;
                    for (const auto& s : c.slots) {
                        int slot = -1;
                        switch (type) {
                        case ContainerSlotType::HotbarAndInventory:
                        case ContainerSlotType::Hotbar: slot = s.slot; break;
                        case ContainerSlotType::Inventory: slot = s.slot < 27 ? s.slot + 9 : s.slot; break;
                        case ContainerSlotType::Armor: slot = kInvArmor0 + s.slot; break;
                        case ContainerSlotType::Offhand: slot = kInvOffhand; break;
                        case ContainerSlotType::CraftingInput: slot = s.slot >= 28 && s.slot <= 31 ? kInvCraft0 + s.slot - 28 : (s.slot >= 32 && s.slot <= 40 ? kInvWork0 + s.slot - 32 : -1); break;
                        case ContainerSlotType::CraftingOutput:
                        case ContainerSlotType::CreativeOutput: slot = kInvOutput; break;
                        case ContainerSlotType::Cursor: slot = kInvCursor; break;
                        case ContainerSlotType::Container:
                        case ContainerSlotType::Shulker:
                        case ContainerSlotType::Barrel:
                            if (inventory.chestOpen && s.slot < kInvChestCount)
                                slot = kInvChest0 + s.slot;
                            break;
                        default: break;
                        }
                        if (slot < 0 || (slot >= kInvSlots && slot < kInvChest0) || slot >= kInvTotal)
                            continue;
                        ItemStackInfo& info = InvInfoLocked(slot);
                        ItemV4& raw = InvRawLocked(slot);
                        if (s.count != 0 && raw.networkId == 0 && before && slot < 36) {
                            const ItemV4& old = before->raw[slot];
                            if (old.networkId != 0 && (!s.itemStackId || !old.stackId || *s.itemStackId == *old.stackId)) {
                                info = slot < 9 ? before->view.hotbar[slot] : before->view.main[slot - 9];
                                raw = old;
                            }
                        }
                        if (s.count == 0) {
                            info = ItemStackInfo{};
                            raw = ItemV4{};
                        } else {
                            info.count = s.count;
                            raw.count = s.count;
                            if (s.itemStackId) {
                                raw.stackId = s.itemStackId;
                                raw.hasStackId = true;
                            }
                        }
                    }
                }
                InvRecomputeOutputLocked();
            }
            }
            for (const auto& r : retries)
                SendDropRequest(r[0], r[1], r[2]);
            {
                std::lock_guard lock(statusMutex);
                for (const ItemStackResponsesEntry& r : p.responses)
                    if (r.requestId == invPendingRequest)
                        invPendingRequest = 0;
            }
            InvPump();
        });

        On<CreativeContentPacket>([this](const CreativeContentPacket& p) {
            std::lock_guard lock(statusMutex);
            const auto info = [this](const ItemLegacy& o) {
                ItemStackInfo r;
                const auto it = itemNames.find(o.networkId);
                if (o.networkId != 0) {
                    r.name = it == itemNames.end() ? std::string("minecraft:unknown") : it->second;
                    r.count = std::max<int>(1, o.count);
                    r.aux = static_cast<int>(o.metadata);
                    r.blockRuntimeId = static_cast<uint32_t>(o.blockRuntimeId);
                }
                return r;
            };
            CreativeCatalog next;
            creativeRaw.clear();
            for (const auto& g : p.groups) {
                CreativeGroup group;
                group.category = static_cast<int>(g.category);
                group.name = g.name;
                group.icon = info(g.iconItem);
                next.groups.push_back(std::move(group));
            }
            for (const auto& i : p.items) {
                CreativeItem item;
                item.entryId = i.entryId;
                item.info = info(i.item);
                item.group = static_cast<int>(i.groupIndex);
                if (item.info.name.empty())
                    continue;
                ItemV4 raw;
                raw.networkId = static_cast<int16_t>(i.item.networkId);
                raw.count = static_cast<uint16_t>(item.info.count);
                raw.metadata = i.item.metadata;
                raw.blockRuntimeId = static_cast<uint32_t>(i.item.blockRuntimeId);
                creativeRaw[i.entryId] = raw;
                next.items.push_back(std::move(item));
            }
            next.version = creative.version + 1;
            creative = std::move(next);
        });

        On<CraftingDataPacket>([this](const CraftingDataPacket& p) {
            const auto ingredient = [](const RecipeIngredient& in) {
                RecipeIngredientInfo r;
                if (in.type == RecipeIngredientType::Valid && (in.descriptorType == "name" || in.descriptorType == "item_tag")) {
                    r.empty = false;
                    r.name = in.name;
                    r.tag = in.tag;
                    r.meta = in.metadata;
                }
                return r;
            };
            std::vector<RecipeInfo> list;
            const auto outInfo = [this](const ItemLegacy& o) {
                ItemStackInfo info;
                const auto it = itemNames.find(o.networkId);
                if (o.networkId != 0 && o.count > 0) {
                    info.name = it == itemNames.end() ? std::string("minecraft:unknown") : it->second;
                    info.count = o.count;
                    info.aux = static_cast<int>(o.metadata);
                    info.blockRuntimeId = static_cast<uint32_t>(o.blockRuntimeId);
                }
                return info;
            };
            const auto outRaw = [](const ItemLegacy& o) {
                ItemV4 raw;
                raw.networkId = static_cast<int16_t>(o.networkId);
                raw.count = o.count;
                raw.metadata = o.metadata;
                raw.blockRuntimeId = static_cast<uint32_t>(o.blockRuntimeId);
                return raw;
            };
            for (const ShapedRecipe& r : p.shapedRecipes) {
                if (r.block != "crafting_table" || r.width > 3 || r.height > 3 || r.output.empty())
                    continue;
                RecipeInfo ri;
                ri.shaped = true;
                ri.w = r.width;
                ri.h = r.height;
                for (const auto& in : r.input)
                    ri.in.push_back(ingredient(in));
                ri.out = outRaw(r.output[0]);
                ri.outInfo = outInfo(r.output[0]);
                ri.netId = r.networkId;
                list.push_back(std::move(ri));
            }
            for (const ShapelessRecipe& r : p.shapelessRecipes) {
                if (r.block != "crafting_table" || r.input.size() > 9 || r.output.empty())
                    continue;
                RecipeInfo ri;
                for (const auto& in : r.input)
                    ri.in.push_back(ingredient(in));
                ri.out = outRaw(r.output[0]);
                ri.outInfo = outInfo(r.output[0]);
                ri.netId = r.networkId;
                list.push_back(std::move(ri));
            }
            std::lock_guard lock(statusMutex);
            craftedRecipe = -1;
            if (p.clearRecipes)
                recipes.clear();
            for (auto& r : list)
                recipes.push_back(std::move(r));
            Log("inventory", "{} crafting-table recipes", recipes.size());
        });

        On<ContainerOpenPacket>([this](const ContainerOpenPacket& p) {
            Log("bedrock", "container open window {} type {}", static_cast<int>(p.windowId), static_cast<int>(p.windowType));
            std::lock_guard lock(statusMutex);
            inventoryWindow = static_cast<int>(p.windowId);
            inventoryOpen = true;
            workbenchOpen = p.windowType == WindowType::Workbench;
            if (p.windowType == WindowType::Horse) {
                inventory.horseOpen = true;
                inventory.horseChestSlots = 0;
                inventory.horseEntity = p.runtimeEntityId;
                horseWindow = static_cast<int>(p.windowId);
                horseJustOpened = true;
                for (int h = 0; h < kInvHorseCount; h++) {
                    inventory.horse[h] = ItemStackInfo{};
                    horseRaw[h] = ItemV4{};
                }
            } else {
                inventory.horseOpen = false;
            }
            inventory.chestOpen = false;
            chestWindow = -1;
            // plain chests on boats and minecarts arrive as their own window types
            const auto wt = p.windowType;
            const bool plainContainer = wt == WindowType::Container || wt == WindowType::MinecartChest ||
                wt == WindowType::ChestBoat || wt == WindowType::Hopper || wt == WindowType::Dropper ||
                wt == WindowType::Dispenser;
            if (wt != WindowType::Container && wt != WindowType::Horse && wt != WindowType::Workbench)
                Log("bedrock", "container open window {} type {} (unhandled)", static_cast<int>(p.windowId), static_cast<int>(wt));
            if (plainContainer) {
                bool loaded = false;
                const std::string name = BlockPalette::Instance().Name(netLevel.GetBlock(p.coordinates.x, p.coordinates.y, p.coordinates.z, loaded));
                const auto has = [&](const char* s) { return name.contains(s); };
                chestSlotType = has("shulker_box") ? ContainerSlotType::Shulker : has("barrel") ? ContainerSlotType::Barrel : ContainerSlotType::Container;
                inventory.chestTitle = has("ender_chest") ? "container.enderchest" : has("shulker_box") ? "container.shulkerBox"
                                     : has("barrel")      ? "container.barrel"
                                                          : "container.chest";
                inventory.chestOpen = true;
                inventory.chestSlots = 27;
                chestWindow = static_cast<int>(p.windowId);
                chestJustOpened = true;
                for (int c = 0; c < kInvChestCount; c++) {
                    inventory.chest[c] = ItemStackInfo{};
                    chestRaw[c] = ItemV4{};
                }
            }
            if (workbenchOpen) {
                workbenchJustOpened = true;
                for (auto& w : inventory.work)
                    w = ItemStackInfo{};
                InvRecomputeOutputLocked();
            }
        });
        On<ContainerClosePacket>([this](const ContainerClosePacket& p) {
            Log("bedrock", "container close window {}{}", static_cast<int>(p.windowId), (p.server ? " (server)" : ""));
            std::lock_guard lock(statusMutex);
            inventoryOpen = false;
            workbenchOpen = false;
            inventory.horseOpen = false;
            horseWindow = -1;
            inventory.chestOpen = false;
            chestWindow = -1;
            for (auto& w : inventory.work)
                w = ItemStackInfo{};
            if (p.server) {
                serverClosedInventory = true;
                ContainerClosePacket ack = p;
                ack.server = false;
                Send(ack);
            }
        });

        On<PlayerHotbarPacket>([this](const PlayerHotbarPacket& p) {
            if (!p.selectSlot)
                return;
            std::lock_guard lock(statusMutex);
            inventory.selected = static_cast<int>(std::min<uint32_t>(p.selectedSlot, 8));
        });

        On<MobEquipmentPacket>([this, convert](const MobEquipmentPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId)
                return;
            std::lock_guard lock(statusMutex);
            const int selected = std::min<int>(p.selectedSlot, 8);
            inventory.selected = selected;
            if (p.slot < 9) {
                inventory.hotbar[p.slot] = convert(p.item);
                inventoryRaw[p.slot] = p.item;
            }
        });

        On<MobArmorEquipmentPacket>([this, convert](const MobArmorEquipmentPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId)
                return;
            std::lock_guard lock(statusMutex);
            inventory.armor[0] = convert(p.helmet);
            inventory.armor[1] = convert(p.chestplate);
            inventory.armor[2] = convert(p.leggings);
            inventory.armor[3] = convert(p.boots);
        });

        On<UpdateAttributesPacket>([this](const UpdateAttributesPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId)
                return;
            std::lock_guard lock(statusMutex);
            for (const PlayerAttributesEntry& a : p.attributes) {
                const std::string name = a.name.starts_with("minecraft:") ? a.name.substr(10) : a.name;
                if (name == "health") {
                    vitals.health = a.current;
                    vitals.maxHealth = a.max > 0 ? a.max : 20.0f;
                    vitals.known = true;
                } else if (name == "player.hunger") {
                    vitals.hunger = a.current;
                } else if (name == "player.saturation") {
                    vitals.saturation = a.current;
                } else if (name == "absorption") {
                    vitals.absorption = a.current;
                } else if (name == "player.level") {
                    vitals.level = static_cast<int>(a.current);
                } else if (name == "player.experience") {
                    vitals.xpProgress = std::clamp(a.current, 0.0f, 1.0f);
                }
            }
        });

        On<MovementEffectPacket>([this](const MovementEffectPacket& p) {
            if (runtimeId == 0 || p.runtimeId != runtimeId)
                return;
            // type and duration are zigzag varints (reader 0x14072F7D0, retyped in overrides.json): unsigned doubles them
            const auto type = static_cast<uint32_t>(p.effectType);
            if (type > 1)
                return;
            std::lock_guard lock(statusMutex);
            pendingMovementEffects.push_back({type, p.effectDuration, p.tick});
        });

        On<MobEffectPacket>([this](const MobEffectPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId)
                return;
            if (PERLIN_ENV("PERLIN_BLOCK_DEBUG"))
                Log("effect", "id={} event={} duration={} serverTick={} afterAuthTick={}", p.effectId, static_cast<int>(p.eventId), p.duration, p.tick, LastAuthTick());
            const bool on = p.eventId != MobEffectPacketEventId::Remove;
            std::lock_guard lock(statusMutex);
            pendingEffectUpdates.push_back({p.tick, p.effectId, p.amplifier, on, p.duration});
            activeEffects.erase(std::remove_if(activeEffects.begin(), activeEffects.end(), [&](const ActiveEffect& e) { return e.id == p.effectId; }), activeEffects.end());
            if (on) {
                ActiveEffect e;
                e.id = p.effectId;
                e.amplifier = p.amplifier;
                e.ambient = p.ambient;
                e.particles = p.particles;
                e.duration = p.duration;
                e.appliedTick = p.tick != 0 ? p.tick : LastAuthTick();
                activeEffects.push_back(e);
            }
            switch (p.effectId) {
            case 10: vitals.regeneration = on; break;
            case 17: vitals.hungerEffect = on; break;
            case 19: vitals.poison = on; break;
            case 20: vitals.wither = on; break;
            default: break;
            }
        });

        On<SetEntityDataPacket>([this](const SetEntityDataPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId)
                return;
            std::lock_guard lock(statusMutex);
            for (const MetadataDictionaryEntry& e : p.metadata) {
                int value = 0;
                if (e.key == MetadataDictionaryEntryKey::Flags && IntegerOf(e, value)) {
                    vitals.onFire = (value & 1) != 0;
                    immobile = (value & (1 << 16)) != 0;
                }
                else if (e.key == MetadataDictionaryEntryKey::Air && IntegerOf(e, value))
                    vitals.air = value;
                else if (e.key == MetadataDictionaryEntryKey::MaxAirdataMaxAir && IntegerOf(e, value) && value > 0)
                    vitals.maxAir = value;
            }
        });

        On<LocatorBarPacket>([this](const LocatorBarPacket& p) {
            std::lock_guard lock(locatorMutex);
            for (const LocatorBarWaypoint& entry : p.waypoints) {
                const auto it = std::find_if(locatorWaypoints.begin(), locatorWaypoints.end(),
                                             [&](const LocatorWaypoint& w) { return w.id == entry.groupHandle.bytes; });
                if (entry.action == LocatorBarWaypointAction::Remove) {
                    if (it != locatorWaypoints.end())
                        locatorWaypoints.erase(it);
                    continue;
                }
                LocatorWaypoint* target = nullptr;
                if (it != locatorWaypoints.end()) {
                    target = &*it;
                } else {
                    locatorWaypoints.push_back({});
                    target = &locatorWaypoints.back();
                    target->id = entry.groupHandle.bytes;
                }
                const Waypoint& w = entry.waypoint;
                if (w.visible)
                    target->visible = *w.visible;
                if (w.worldPosition) {
                    target->hasPosition = true;
                    target->x = w.worldPosition->position.x;
                    target->y = w.worldPosition->position.y;
                    target->z = w.worldPosition->position.z;
                    target->dimension = w.worldPosition->dimensionId;
                }
                if (w.color) {
                    target->hasColor = true;
                    target->color = *w.color;
                }
                if (w.actorUniqueId) {
                    target->hasActor = true;
                    target->actorId = *w.actorUniqueId;
                }
            }
        });

        On<SetTitlePacket>([this](const SetTitlePacket& p) {
            std::lock_guard lock(statusMutex);
            const auto now = std::chrono::steady_clock::now();
            switch (p.type) {
            case SetTitlePacketType::Clear:
            case SetTitlePacketType::Reset:
                titleText.title.clear();
                titleText.subtitle.clear();
                titleText.actionBar.clear();
                titleText.serial++;
                break;
            case SetTitlePacketType::SetTitle:
            case SetTitlePacketType::SetTitleJson:
                titleText.title = p.text;
                titleText.titleAt = now;
                titleText.serial++;
                break;
            case SetTitlePacketType::SetSubtitle:
            case SetTitlePacketType::SetSubtitleJson:
                titleText.subtitle = p.text;
                titleText.titleAt = now;
                titleText.serial++;
                break;
            case SetTitlePacketType::ActionBarMessage:
            case SetTitlePacketType::ActionBarMessageJson:
                titleText.actionBar = p.text;
                titleText.actionAt = now;
                titleText.serial++;
                break;
            case SetTitlePacketType::SetDurations:
                titleText.fadeIn = p.fadeInTime;
                titleText.stay = p.stayTime;
                titleText.fadeOut = p.fadeOutTime;
                break;
            default:
                break;
            }
        });

        OnPartial<TextPacket>([this](const TextPacket& t) {
            if (t.type == TextPacketType::Popup || t.type == TextPacketType::Tip || t.type == TextPacketType::JukeboxPopup) {
                std::lock_guard lock(statusMutex);
                titleText.actionBar = t.message;
                titleText.actionAt = std::chrono::steady_clock::now();
                titleText.serial++;
            }
        });
    }
}

namespace perlin {
    void BedrockSession::ClearCrack(int x, int y, int z) {
        std::lock_guard lock(crackMutex);
        std::erase_if(crackStates, [&](const CrackState& c) { return c.x == x && c.y == y && c.z == z; });
        lastClearedCrack[0] = x; lastClearedCrack[1] = y; lastClearedCrack[2] = z;
        lastClearedAt = std::chrono::steady_clock::now();
    }

    std::vector<BedrockSession::BlockCrack> BedrockSession::Cracks() {
        std::lock_guard lock(crackMutex);
        const auto now = std::chrono::steady_clock::now();
        std::vector<BlockCrack> out;
        for (auto it = crackStates.begin(); it != crackStates.end();) {
            const double age = std::chrono::duration<double>(now - it->since).count();
            if (age > 15.0) {
                it = crackStates.erase(it);
                continue;
            }
            const double raw = it->progress + it->perSecond * age;
            if (raw > 1.25) {
                it = crackStates.erase(it);
                continue;
            }
            const double progress = std::clamp(raw, 0.0, 1.0);
            out.push_back({it->x, it->y, it->z, std::min(9, static_cast<int>(progress * 10.0))});
            ++it;
        }
        return out;
    }
}
