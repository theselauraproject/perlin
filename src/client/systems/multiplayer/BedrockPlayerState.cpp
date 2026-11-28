#include <set>
#include "client/systems/multiplayer/ItemNbtInfo.hpp"
#include "client/systems/multiplayer/ItemDamage.hpp"
#include <array>
#include "util/Env.hpp"
#include <cstdlib>
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "world/movement/PlayerPhysics.hpp"
#include "world/movement/PerlinStatusEffect.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <bit>
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

        bool IsChargedCrossbow(const BedrockSession::ItemStackInfo& item,
                               const std::map<int, std::string>& registry) {
            if (item.Empty() || item.name != "minecraft:crossbow" || item.chargedItemCount <= 0 ||
                item.chargedItemName.empty() || item.chargedItemName == "minecraft:air")
                return false;
            // Native loaded-state detection checks a resolved non-null item,
            // without restricting it to the vanilla projectile whitelist.
            return std::ranges::any_of(registry, [&](const auto& entry) {
                return entry.first != 0 && entry.second == item.chargedItemName;
            });
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

    void BedrockSession::ResetPlayerAbilities() {
        mayFly = noClip = instabuild = isFlying = false;
        mayMine = true;
        flySpeed = 0.05f;
        vFlySpeed = 1.0f;
        walkSpeed = 0.1f;
        ++abilitiesSeq;
    }

    void BedrockSession::ResetPlayerAttributes() {
        std::lock_guard lock(attrMutex);
        ownFlagWords = {};
        ownFlagWordsKnown = {};
        ownPlayerFlags = 0;
        inputLocks = 0;
        immobile = false;
        ownAttributes.clear();
        movementAttribute.reset();
        movementState = {};
        pendingAttributeUpdates.clear();
        freezeUpdates.clear();
        ownScale = 1.0f;
        ownWidth = 0.6f;
        ownHeight = 1.8f;
        ownNameTag.clear();
        ownSeatOffset.reset();
    }

    void BedrockSession::ResetPlayerState() {
        ResetPlayerAbilities();
        ResetPlayerAttributes();
        chargedItemUseReset = false;
        serverUsingItem = -1;
        shieldItemId = -1;
        ResetServerGui();
        cameraEffects.Reset();
        {
            std::lock_guard locatorLock(locatorMutex);
            locatorWaypoints.clear();
            trackedPlayerLocations.clear();
        }
        std::lock_guard lock(statusMutex);
        ++itemStateRevision;
        itemSlotRevisions.fill(0);
        itemCooldownRevisions.clear();
        vitals = Vitals{};
        activeEffects.clear();
        itemCooldowns.Clear();
        itemCooldownDefinitions.clear();
        pendingEffectUpdates.clear();
        serverMotions.clear();
        pendingMovementEffects.clear();
        inventory = InventoryView{};
        ResetContainerStateLocked();
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
        recipeUnlocks.Clear();
        creative.groups.clear();
        creative.items.clear();
        ++creative.version;
        creativeRaw.clear();
        craftedRecipe = -1;
        titleText = TitleText{};
    }

    BedrockSession::Vitals BedrockSession::GetVitals() const {
        std::lock_guard lock(statusMutex);
        Vitals current = vitals;
        current.regeneration = current.hungerEffect = current.poison = current.wither = false;
        const uint64_t now = LastAuthTick();
        for (const ActiveEffect& effect : activeEffects) {
            if (physics::bds::StatusEffectTicksAt(effect.duration, effect.appliedTick, now) == 0)
                continue;
            switch (effect.id) {
            case 10:
                current.regeneration = true;
                break;
            case 17:
                current.hungerEffect = true;
                break;
            case 19:
                current.poison = true;
                break;
            case 20:
                current.wither = true;
                break;
            default:
                break;
            }
        }
        return current;
    }

    BedrockSession::InventoryView BedrockSession::GetInventory() const {
        std::lock_guard lock(statusMutex);
        return inventory;
    }

    void BedrockSession::TickItemCooldowns() {
        std::lock_guard lock(statusMutex);
        itemCooldowns.Tick();
    }

    int32_t BedrockSession::ItemCooldownTicks(std::string_view itemName) const {
        std::lock_guard lock(statusMutex);
        const auto it = itemCooldownDefinitions.find(itemName);
        return it == itemCooldownDefinitions.end() ? 0 : itemCooldowns.Remaining(it->second.category);
    }

    bool BedrockSession::ItemUseOnCooldown(std::string_view itemName) const {
        std::lock_guard lock(statusMutex);
        const auto it = itemCooldownDefinitions.find(itemName);
        return it != itemCooldownDefinitions.end() && it->second.type == itemcooldown::Type::Use &&
               itemCooldowns.Active(it->second.category);
    }

    bool BedrockSession::HeldItemUseOnCooldown() const {
        std::lock_guard lock(statusMutex);
        const int selected = std::clamp(inventory.selected, 0, 8);
        const ItemV4& item = useOffhand ? extraRaw[kInvOffhand - kInvArmor0] : inventoryRaw[selected];
        return ItemOnCooldownLocked(item, itemcooldown::Type::Use);
    }

    bool BedrockSession::HeldItemIsChargedCrossbow() const {
        std::lock_guard lock(statusMutex);
        const auto& item = useOffhand ? inventory.offhand
                                     : inventory.hotbar[std::clamp(inventory.selected, 0, 8)];
        return IsChargedCrossbow(item, itemNames);
    }

    bool BedrockSession::CanStartUsingHeldItem(bool inWaterOrRain) const {
        std::lock_guard lock(statusMutex);
        const int selected = std::clamp(inventory.selected, 0, 8);
        const auto& item = useOffhand ? inventory.offhand : inventory.hotbar[selected];
        if (item.Empty())
            return false;
        if (item.name == "minecraft:crossbow")
            return !IsChargedCrossbow(item, itemNames);
        if (item.name != "minecraft:trident")
            return true;
        const ItemV4& raw = useOffhand ? extraRaw[kInvOffhand - kInvArmor0] : inventoryRaw[selected];
        const auto* nbt = ItemNbt(raw);
        if (!nbt)
            return true;
        const ItemNbtInfo parsed = ParseItemNbt(*nbt);
        if (!parsed.ok)
            return true;
        // TridentItem::use reads the low signed short of the root IntTag.
        // The matched builtin is registered with maximum damage 250.
        const int16_t damage = parsed.damageTagType == 3
                                   ? std::bit_cast<int16_t>(static_cast<uint16_t>(parsed.damage))
                                   : 0;
        if (damage >= 250)
            return false;
        // EnchantUtils::getEnchantLevel uses the active "ench" list, not
        // StoredEnchantments, and selects the first matching enchantment.
        for (const auto& [id, level] : parsed.activeEnchants)
            if (id == 30)
                return level == 0 || inWaterOrRain;
        return true;
    }

    int BedrockSession::HeldItemEnchantLevel(int id) const {
        std::lock_guard lock(statusMutex);
        const int selected = std::clamp(inventory.selected, 0, 8);
        const ItemV4& raw = useOffhand ? extraRaw[kInvOffhand - kInvArmor0] : inventoryRaw[selected];
        if (raw.networkId == 0 || raw.count == 0)
            return 0;
        const auto* nbt = ItemNbt(raw);
        if (!nbt)
            return 0;
        const ItemNbtInfo parsed = ParseItemNbt(*nbt);
        if (parsed.ok)
            for (const auto& [enchant, level] : parsed.activeEnchants)
                if (enchant == id)
                    return level;
        return 0;
    }

    bool BedrockSession::ItemOnCooldownLocked(const ItemV4& item,
                                            std::optional<itemcooldown::Type> action) const {
        if (item.networkId == 0 || item.count == 0)
            return false;
        const auto name = itemNames.find(item.networkId);
        if (name == itemNames.end())
            return false;
        const auto definition = itemCooldownDefinitions.find(name->second);
        return definition != itemCooldownDefinitions.end() &&
               (!action || definition->second.type == *action) &&
               itemCooldowns.Active(definition->second.category);
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

        for (const auto& [id, enchantLevel] : inventory.armor[3].enchants)
            if (id == 36)
                level = std::max(level, enchantLevel);
        return level;
    }

    int BedrockSession::SwiftSneakEnchant() const {
        std::lock_guard lock(statusMutex);
        int level = 0;

        for (const auto& [id, enchantLevel] : inventory.armor[2].enchants)
            if (id == 37)
                level = std::max(level, enchantLevel);
        return level;
    }

    namespace {
        std::vector<physics::AttributeModifierValue>
        ModifierValues(const std::vector<PlayerAttributesEntryModifiersEntry>& modifiers) {
            std::vector<physics::AttributeModifierValue> values;
            values.reserve(modifiers.size());
            for (const auto& m : modifiers)
                values.push_back({m.amount, m.operation, m.operand});
            return values;
        }

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

    void BedrockSession::ApplyMovementAttribute(const PlayerAttributesEntry& entry) {
        ApplyMovementAttribute(movementState, entry);
    }

    void BedrockSession::ApplyAttributeUpdate(const AttributeUpdate& update) {
        std::lock_guard lock(attrMutex);
        for (const PlayerAttributesEntry& entry : update.attributes) {
            if (!IsMovementAttribute(entry.name))
                continue;
            ownAttributes[entry.name] = entry.current;
            if (entry.name == "minecraft:movement") {
                movementAttribute = entry;
                ApplyMovementAttribute(entry);
            }
        }
    }

    float BedrockSession::PredictedMovementSpeed(bool sprinting, std::optional<float> freezeStrength) {
        std::lock_guard lock(attrMutex);
        return PredictedMovementSpeed(movementState, sprinting, freezeStrength);
    }

    float BedrockSession::PredictedMovementSpeed(bool sprinting, std::optional<float> freezeStrength,
                                                 MovementAttributeState& snapshot) {
        std::lock_guard lock(attrMutex);
        const float result = PredictedMovementSpeed(movementState, sprinting, freezeStrength);
        snapshot = movementState;
        return result;
    }

    void BedrockSession::ApplyMovementAttribute(MovementAttributeState& state,
                                                const PlayerAttributesEntry& entry) {
        state.known = true;
        state.modifiers = entry.modifiers;
        const float baseMin = std::fmin(entry.defaultMax, entry.defaultMin);
        state.base = {baseMin, entry.defaultMax,
                        entry.defaultMax < entry.default_ ? entry.defaultMax
                                                          : std::fmax(entry.default_, baseMin)};
        state.limits = {std::fmin(entry.max, entry.min), entry.max};
        state.value = physics::ClampAttribute(entry.current, state.limits[0], state.limits[1],
                                                ModifierValues(state.modifiers));
    }

    float BedrockSession::PredictedMovementSpeed(MovementAttributeState& state, bool sprinting,
                                                 std::optional<float> freezeStrength) {

        std::optional<float> freeze;
        if (freezeStrength && std::fabs(*freezeStrength) > 0.00000011920929f)
            freeze = *freezeStrength * -0.05f;
        if (!state.known) {
            const float factor = 0.3f + 1.0f;
            const float value = freeze ? *freeze + 0.1f : 0.1f;
            return sprinting ? factor * value : value;
        }

        const auto withFreeze = [&](float value) {
            if (!freezeStrength)
                return value;
            const auto isFreeze = [](const PlayerAttributesEntryModifiersEntry& m) {
                return m.name == "Freeze effect";
            };
            std::optional<float> sent;
            for (const auto& m : state.modifiers)
                if (isFreeze(m))
                    sent = m.amount;
            if (sent == freeze)
                return value;
            std::vector<PlayerAttributesEntryModifiersEntry> modifiers = state.modifiers;
            std::erase_if(modifiers, isFreeze);
            auto values = ModifierValues(modifiers);
            if (freeze)
                values.push_back({*freeze, 0, 2});
            std::array<float, 2> limits = state.limits;
            return physics::RecalculateAttribute(state.base, values, value, limits);
        };
        const bool present = std::ranges::any_of(state.modifiers, IsSprintModifier);
        if (sprinting == present)
            return withFreeze(state.value);
        if (sprinting) {
            PlayerAttributesEntryModifiersEntry boost;
            boost.id = "d208fc00-42aa-4aad-9276-d5446530de43";
            boost.name = "Sprinting speed boost";
            boost.amount = 0.3f;
            boost.operation = 2;
            boost.operand = 2;
            boost.serializable = false;
            state.modifiers.push_back(boost);
        } else {
            std::erase_if(state.modifiers, IsSprintModifier);
        }
        state.value = physics::RecalculateAttribute(state.base, ModifierValues(state.modifiers),
                                                      state.value, state.limits);
        return withFreeze(state.value);
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
        p.hand = useOffhand || attackOffhand ? 1 : 0;
        return Send(p);
    }

    void BedrockSession::InjectClientPacket(uint32_t id, const std::vector<uint8_t>& body) {
        const net::context::ScopedSettings codecScope(PacketCodecSettings());
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

    BedrockSession::ItemStackInfo BedrockSession::ConvertItemStack(const ItemV4& raw) {
        ItemStackInfo info;
        if (raw.networkId == 0 || raw.count == 0)
            return info;
        auto it = itemNames.find(raw.networkId);
        info.name = it == itemNames.end() ? std::string("minecraft:unknown") : it->second;
        if (it == itemNames.end()) {
            static std::set<int> reported;
            if (reported.insert(raw.networkId).second)
                Log("items", "unknown network id {} (registry has {} items)", raw.networkId,
                    itemNames.size());
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
        if (const auto* with = std::get_if<ItemExtraDataWithBlockingTick>(&raw.extra))
            info.canDestroy = with->canDestroy;
        else if (const auto* without = std::get_if<ItemExtraDataWithoutBlockingTick>(&raw.extra))
            info.canDestroy = without->canDestroy;
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
                info.chargedItemName = parsed.chargedItemName;
                info.chargedItemCount = parsed.chargedItemCount;
                // A charged projectile has its own Name and Damage fields.
                // Successful structured parsing takes precedence over byte
                // searches that cannot distinguish nested item metadata.
                info.customName = parsed.customName;
                info.damage = parsed.hasDamage ? std::max(0, parsed.damage) : 0;
            }
            info.enchanted = !info.enchants.empty() || Contains(*nbt, "ench") ||
                             Contains(*nbt, "Enchantments") || Contains(*nbt, "StoredEnchantments") ||
                             Contains(*nbt, "minecraft:enchantments");
        }
        static const char* const kAlwaysGlint[] = {"minecraft:enchanted_golden_apple",
                                                   "minecraft:experience_bottle", "minecraft:nether_star",
                                                   "minecraft:end_crystal", "minecraft:enchanted_book"};
        for (const char* name : kAlwaysGlint)
            if (info.name == name)
                info.enchanted = true;
        return info;
    }

    void BedrockSession::StoreInventoryItems(WindowIDVarint window,
                                             const std::optional<FullContainerName>& container, int firstSlot,
                                             const std::vector<ItemV4>& items, bool whole) {
        std::lock_guard lock(statusMutex);
        const ContainerSlotType type =
            container ? container->containerId : ContainerSlotType::HotbarAndInventory;
        const bool offhand = window == WindowIDVarint::Offhand || type == ContainerSlotType::Offhand;
        const bool armor = window == WindowIDVarint::Armor || type == ContainerSlotType::Armor;
        const bool playerInventory =
            window == WindowIDVarint::Inventory || window == WindowIDVarint::Hotbar ||
            window == WindowIDVarint::FixedInventory || type == ContainerSlotType::Hotbar ||
            type == ContainerSlotType::Inventory || type == ContainerSlotType::HotbarAndInventory;
        static const bool debug = PERLIN_ENV("PERLIN_INV_DEBUG") != nullptr;
        if (debug)
            Log("inv", "window {} container {} first {} n {}{}", static_cast<uint32_t>(window),
                static_cast<int>(type), firstSlot, items.size(), (whole ? " (content)" : " (slot)"));
        const bool mainOnly = type == ContainerSlotType::Inventory && whole && items.size() == 27;
        const bool uiWindow = window == WindowIDVarint::Ui || type == ContainerSlotType::Cursor ||
                              type == ContainerSlotType::CraftingInput ||
                              type == ContainerSlotType::CraftingOutput ||
                              type == ContainerSlotType::CreativeOutput;
        const bool horseWin =
            inventory.horseOpen && horseWindow > 0 && static_cast<int>(window) == horseWindow;
        const bool chestWin = (inventory.chestOpen || inventory.furnaceKind >= 0) && chestWindow >= 0 &&
                              static_cast<int>(window) == chestWindow;
        // A container window (1..99) that is not open yet: keep its items for the coming open.
        const int windowNumber = static_cast<int>(window);
        if (!chestWin && !horseWin && !uiWindow && !offhand && !armor && !playerInventory && windowNumber > 0 &&
            windowNumber < 100) {
            PendingWindow& pending = pendingWindows[windowNumber];
            const auto now = std::chrono::steady_clock::now();
            if (whole || now - pending.at > std::chrono::seconds(5)) {
                pending.slots.clear();
                pending.size = 0;
            }
            if (whole)
                pending.size = static_cast<int>(items.size());
            for (size_t i = 0; i < items.size(); i++) {
                const int c = firstSlot + static_cast<int>(i);
                if (c >= 0 && c < kInvChestCount) {
                    pending.slots[c] = items[i];
                    pending.size = std::max(pending.size, c + 1);
                }
            }
            pending.at = now;
            if (debug)
                Log("inv", "held {} item(s) for window {} until it opens", items.size(), windowNumber);
            return;
        }
        if (chestWin && whole && inventory.furnaceKind < 0) {
            inventory.chestSlots = std::clamp(static_cast<int>(items.size()), 0, kInvChestCount);
            int filled = 0;
            for (const ItemV4& it : items)
                filled += it.networkId != 0 ? 1 : 0;
            Log("inventory", "window {} content: {} slots, {} filled", chestWindow, items.size(), filled);
        }
        for (size_t i = 0; i < items.size(); i++) {
            if (chestWin) {
                const int c = firstSlot + static_cast<int>(i);
                if (c >= 0 && c < kInvChestCount) {
                    inventory.chest[c] = ConvertItemStack(items[i]);
                    chestRaw[c] = items[i];
                    ++itemSlotRevisions[kInvChest0 + c];
                }
                continue;
            }
            if (horseWin) {
                const int rawSlot = firstSlot + static_cast<int>(i);
                const int h = type == ContainerSlotType::HorseEquip  ? rawSlot
                              : type == ContainerSlotType::Container ? rawSlot + 2
                                                                     : rawSlot;
                if (h >= 0 && h < kInvHorseCount) {
                    inventory.horse[h] = ConvertItemStack(items[i]);
                    horseRaw[h] = items[i];
                    ++itemSlotRevisions[kInvHorse0 + h];
                    if (h >= 2)
                        inventory.horseChestSlots = std::max(inventory.horseChestSlots, h - 1);
                }
                continue;
            }
            const int slot = firstSlot + static_cast<int>(i) +
                             (type == ContainerSlotType::Inventory && !whole && firstSlot < 27 ? 9 : 0);
            const ItemStackInfo info = ConvertItemStack(items[i]);
            if (uiWindow) {
                int addr = -1;
                if (type == ContainerSlotType::Cursor ||
                    (window == WindowIDVarint::Ui && !container && slot == 0))
                    addr = kInvCursor;
                else if (slot >= 28 && slot <= 31 &&
                         (type == ContainerSlotType::CraftingInput || window == WindowIDVarint::Ui))
                    addr = kInvCraft0 + slot - 28;
                else if (slot >= 32 && slot <= 40 &&
                         (type == ContainerSlotType::CraftingInput || window == WindowIDVarint::Ui))
                    addr = kInvWork0 + slot - 32;
                else if (type == ContainerSlotType::CraftingOutput ||
                         type == ContainerSlotType::CreativeOutput)
                    addr = kInvOutput;
                if (addr >= 0) {
                    InvInfoLocked(addr) = info;
                    InvRawLocked(addr) = items[i];
                    ++itemSlotRevisions[addr];
                    if (IsCraftAddr(addr))
                        InvRecomputeOutputLocked();
                }
            } else if (offhand) {
                if (slot == 0) {
                    inventory.offhand = info;
                    extraRaw[kInvOffhand - 36] = items[i];
                    ++itemSlotRevisions[kInvOffhand];
                }
            } else if (armor) {
                if (slot >= 0 && slot < 4) {
                    inventory.armor[slot] = info;
                    extraRaw[kInvArmor0 - 36 + slot] = items[i];
                    ++itemSlotRevisions[kInvArmor0 + slot];
                }
            } else if (playerInventory && window != WindowIDVarint::Ui) {
                const auto stamp = std::chrono::steady_clock::now();
                if (mainOnly) {
                    if (slot >= 0 && slot < 27) {
                        inventory.main[slot] = info;
                        inventoryRaw[slot + 9] = items[i];
                        invServerAt[slot + 9] = stamp;
                        ++itemSlotRevisions[slot + 9];
                    }
                } else if (slot >= 0 && slot < 9) {
                    inventory.hotbar[slot] = info;
                    inventoryRaw[slot] = items[i];
                    invServerAt[slot] = stamp;
                    ++itemSlotRevisions[slot];
                } else if (slot >= 9 && slot < 36) {
                    inventory.main[slot - 9] = info;
                    inventoryRaw[slot] = items[i];
                    invServerAt[slot] = stamp;
                    ++itemSlotRevisions[slot];
                }
            }
        }
    }

    void BedrockSession::RegisterInventoryHandlers() {
        On<InventoryContentPacket>([this](const InventoryContentPacket& p) {
            StoreInventoryItems(p.windowId, p.container, 0, p.input, true);
        });
        On<InventorySlotPacket>([this](const InventorySlotPacket& p) {
            StoreInventoryItems(p.windowId, p.container, static_cast<int>(p.slot),
                                std::vector<ItemV4>{p.item}, false);
        });

        On<InventoryTransactionPacket>([this](const InventoryTransactionPacket& p) {
            static int logged = 0;
            if (logged < 12) {
                logged++;
                std::cout << "[inventory] server transaction type "
                          << static_cast<int>(p.transaction.transactionType) << " actions "
                          << p.transaction.actions.size();
                for (const TransactionActionsEntry& a : p.transaction.actions)
                    std::cout << " (src " << static_cast<uint32_t>(a.sourceType) << " win "
                              << (a.windowId ? static_cast<int>(*a.windowId) : -1) << " slot " << a.slot
                              << " new " << a.newItem.networkId << "x" << a.newItem.count << ")";
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
                case 0:
                    w = WindowIDVarint::Inventory;
                    break;
                case 119:
                    w = WindowIDVarint::Offhand;
                    break;
                case 120:
                    w = WindowIDVarint::Armor;
                    break;
                case 122:
                    w = WindowIDVarint::Hotbar;
                    break;
                case 123:
                    w = WindowIDVarint::FixedInventory;
                    break;
                default:
                    continue;
                }
                StoreInventoryItems(w, std::nullopt, static_cast<int>(a.slot), std::vector<ItemV4>{a.newItem},
                                    false);
            }
        });

        On<ItemStackResponsePacket>([this](const ItemStackResponsePacket& p) {
            std::vector<std::array<int, 3>> retries;
            {
                std::lock_guard lock(statusMutex);
                static const bool debug = PERLIN_ENV("PERLIN_INV_DEBUG") != nullptr;
                for (const ItemStackResponsesEntry& r : p.responses) {
                    // Legacy use predictions have negative even IDs. They are
                    // reconciled by slot/content updates, not this odd request
                    // response family. Invalid IDs must not mutate inventory
                    // or release a pending ordinary request.
                    if (r.requestId >= 0 || (static_cast<uint32_t>(r.requestId) & 1u) == 0)
                        continue;
                    if (const auto pending = pendingDrops.find(r.requestId); pending != pendingDrops.end()) {
                        if (r.status == ItemStackResponsesEntryStatus::Ok) {
                            dropForm = pending->second.attempt;
                        } else if (pending->second.attempt < 2) {
                            retries.push_back(
                                {pending->second.slot, pending->second.count, pending->second.attempt + 1});
                        }
                        pendingDrops.erase(pending);
                    }
                    if (r.status != ItemStackResponsesEntryStatus::Ok)
                        Log("inventory", "request {} rejected, status {}:{}", r.requestId,
                            static_cast<int>(r.status),
                            (invSummaries.contains(r.requestId) ? invSummaries[r.requestId] : std::string()));
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
                            std::copy(std::begin(snap->second.raw), std::end(snap->second.raw),
                                      std::begin(inventoryRaw));
                            std::copy(std::begin(snap->second.extra), std::end(snap->second.extra),
                                      std::begin(extraRaw));
                            std::copy(std::begin(snap->second.chest), std::end(snap->second.chest),
                                      std::begin(chestRaw));
                            std::copy(std::begin(snap->second.horse), std::end(snap->second.horse),
                                      std::begin(horseRaw));
                            for (auto& revision : itemSlotRevisions)
                                ++revision;
                            InvRecomputeOutputLocked();
                        }
                        invSnapshots.erase(snap);
                    }
                    invInFlight.erase(r.requestId);
                    const bool newerPending =
                        !invSnapshots.empty() && invSnapshots.begin()->first < r.requestId;
                    if (r.status != ItemStackResponsesEntryStatus::Ok || !r.containers)
                        continue;
                    for (const auto& c : *r.containers) {
                        const ContainerSlotType type = c.slotType.containerId;
                        for (const auto& s : c.slots) {
                            const int slot = inventory::ResponseAddress(
                                type, s.slot, {inventory.chestOpen, inventory.horseOpen, inventory.furnaceKind});
                            if (slot < 0)
                                continue;
                            ++itemSlotRevisions[slot];
                            ItemStackInfo& info = InvInfoLocked(slot);
                            ItemV4& raw = InvRawLocked(slot);

                            if (newerPending) {
                                if (s.itemStackId && raw.hasStackId && raw.stackId == r.requestId)
                                    raw.stackId = s.itemStackId;
                                continue;
                            }
                            if (s.count != 0 && raw.networkId == 0 && before && slot < 36) {
                                const ItemV4& old = before->raw[slot];
                                if (old.networkId != 0 &&
                                    (!s.itemStackId || !old.stackId || *s.itemStackId == *old.stackId)) {
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
            const auto stack = [](const ItemLegacy& o) {
                ItemV4 r;
                if (o.networkId != 0) {
                    r.networkId = static_cast<int16_t>(o.networkId);
                    r.count = static_cast<uint16_t>(std::max<int>(1, o.count));
                    r.metadata = o.metadata;
                    r.blockRuntimeId = static_cast<uint32_t>(o.blockRuntimeId);
                    r.extra = o.extra;
                }
                return r;
            };
            CreativeCatalog next;
            creativeRaw.clear();
            for (const auto& g : p.groups) {
                CreativeGroup group;
                group.category = static_cast<int>(g.category);
                group.name = g.name;
                group.icon = ConvertItemStack(stack(g.iconItem));
                next.groups.push_back(std::move(group));
            }
            for (const auto& i : p.items) {
                CreativeItem item;
                item.entryId = i.entryId;
                ItemV4 raw = stack(i.item);
                item.info = ConvertItemStack(raw);
                item.group = i.groupIndex < p.groups.size() ? static_cast<int>(i.groupIndex) : -1;
                if (item.info.name.empty())
                    continue;
                creativeRaw[i.entryId] = std::move(raw);
                next.items.push_back(std::move(item));
            }
            next.version = creative.version + 1;
            creative = std::move(next);
        });

        On<CraftingDataPacket>([this](const CraftingDataPacket& p) {
            const auto ingredient = [](const RecipeIngredient& in) {
                RecipeIngredientInfo r;
                if (in.type == RecipeIngredientType::Valid &&
                    (in.descriptorType == "name" || in.descriptorType == "item_tag")) {
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
                ri.id = r.recipeId;
                ri.alwaysUnlocked = !r.unlockingRequirement ||
                                    r.unlockingRequirement->context == RecipeUnlockingRequirementContext::AlwaysUnlocked;
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
                ri.id = r.recipeId;
                ri.alwaysUnlocked = !r.unlockingRequirement ||
                                    r.unlockingRequirement->context == RecipeUnlockingRequirementContext::AlwaysUnlocked;
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

        On<UnlockedRecipesPacket>([this](const UnlockedRecipesPacket& p) {
            if (runtimeId == 0)
                return;
            std::lock_guard lock(statusMutex);
            recipeUnlocks.Apply(static_cast<uint32_t>(p.unlockType), p.recipes, [this](const std::string& id) {
                return std::ranges::any_of(recipes, [&](const RecipeInfo& recipe) { return recipe.id == id; });
            });
        });
    }

    void BedrockSession::RegisterContainerHandlers() {
        On<ContainerOpenPacket>([this](const ContainerOpenPacket& p) {
            if (runtimeId == 0)
                return;
            Log("bedrock", "container open window {} type {}", static_cast<int>(p.windowId),
                static_cast<int>(p.windowType));
            std::lock_guard lock(statusMutex);
            inventoryWindow = static_cast<int>(p.windowId);
            inventoryWindowType = p.windowType;
            serverClosedInventory = false;
            serverCloseWindowType = WindowType::None;
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
            inventory.chestFitRows = false;
            inventory.furnaceKind = -1;
            chestWindow = -1;
            if (p.windowType == WindowType::Furnace || p.windowType == WindowType::BlastFurnace ||
                p.windowType == WindowType::Smoker) {
                inventory.furnaceKind = p.windowType == WindowType::Furnace        ? 0
                                        : p.windowType == WindowType::BlastFurnace ? 1
                                                                                   : 2;
                inventory.chestSlots = 3;
                chestWindow = static_cast<int>(p.windowId);
                chestJustOpened = true;
                for (int c = 0; c < 3; c++) {
                    inventory.chest[c] = ItemStackInfo{};
                    chestRaw[c] = ItemV4{};
                }
                std::lock_guard<std::mutex> lk(worldExtraMutex);
                windowData.erase(chestWindow);
            }

            const auto wt = p.windowType;

            const auto askedNs = ownInventoryAskedAt.exchange(0);
            const bool ownInventory =
                wt == WindowType::Inventory && askedNs != 0 &&
                std::chrono::steady_clock::now() -
                        std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(askedNs)) <
                    std::chrono::seconds(3);

            const bool actorInventory =
                wt == WindowType::Inventory && static_cast<int>(p.windowId) != 0 && !ownInventory;

            const bool actorOwned = actorInventory || (wt == WindowType::Container &&
                                                       p.runtimeEntityId != -1 && p.runtimeEntityId != 0);
            const bool plainContainer = wt == WindowType::Container || wt == WindowType::MinecartChest ||
                                        wt == WindowType::ChestBoat || wt == WindowType::Hopper ||
                                        wt == WindowType::Dropper || wt == WindowType::Dispenser ||
                                        actorInventory;
            if (!plainContainer && !ownInventory && wt != WindowType::Horse && wt != WindowType::Workbench &&
                inventory.furnaceKind < 0)
                Log("bedrock", "container open window {} type {} (unhandled)", static_cast<int>(p.windowId),
                    static_cast<int>(wt));
            if (plainContainer) {
                bool loaded = false;

                const std::string name =
                    actorOwned ? std::string()
                               : BlockPalette::Instance().Name(netLevel.GetBlock(
                                     p.coordinates.x, p.coordinates.y, p.coordinates.z, loaded));
                const auto has = [&](const char* s) { return name.contains(s); };
                chestSlotType = has("shulker_box") ? ContainerSlotType::Shulker
                                : has("barrel")    ? ContainerSlotType::Barrel
                                                   : ContainerSlotType::Container;
                inventory.chestTitle = has("ender_chest")   ? "container.enderchest"
                                       : has("shulker_box") ? "container.shulkerBox"
                                       : has("barrel")      ? "container.barrel"
                                                            : "container.chest";
                inventory.chestFitRows = actorOwned;
                if (actorOwned) {

                    entity::Entity holder;
                    if ((entities.FindUnique(p.runtimeEntityId, holder) ||
                         entities.Find(static_cast<uint64_t>(p.runtimeEntityId), holder)) &&
                        !holder.nameTag.empty())
                        inventory.chestTitle = holder.nameTag.substr(0, holder.nameTag.find('\n'));
                }
                inventory.chestOpen = true;
                inventory.chestSlots = 27;
                chestWindow = static_cast<int>(p.windowId);
                chestJustOpened = true;
                for (int c = 0; c < kInvChestCount; c++) {
                    inventory.chest[c] = ItemStackInfo{};
                    chestRaw[c] = ItemV4{};
                }
                // Contents that came before this open (some servers send them first).
                if (auto held = pendingWindows.find(chestWindow); held != pendingWindows.end()) {
                    if (std::chrono::steady_clock::now() - held->second.at < std::chrono::seconds(5)) {
                        for (const auto& [c, raw] : held->second.slots) {
                            inventory.chest[c] = ConvertItemStack(raw);
                            chestRaw[c] = raw;
                        }
                        if (held->second.size > 0)
                            inventory.chestSlots = std::clamp(held->second.size, 0, kInvChestCount);
                        Log("inventory", "window {}: applied {} item(s) sent before it opened", chestWindow,
                            held->second.slots.size());
                    }
                    pendingWindows.erase(held);
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
            if (runtimeId == 0)
                return;
            Log("bedrock", "container close window {}{}", static_cast<int>(p.windowId),
                (p.server ? " (server)" : ""));
            {
                std::lock_guard lock(statusMutex);
                if (!inventoryOpen)
                    return;
                if (p.server) {
                    // The native handler requests a screen close. The screen
                    // controller checks the container type, and deleting the
                    // current manager sends the one ordinary close response.
                    serverClosedInventory = true;
                    serverCloseWindowType = p.windowType;
                    return;
                }
            }
            // A response while a manager still exists is a protocol violation,
            // not an instruction to close it. Native 2193 deliberately names
            // the warning packet itself as the cause and does not compare IDs.
            PacketViolationWarningPacket warning;
            warning.violationType = static_cast<PacketViolationWarningPacketViolationType>(-1);
            warning.severity = PacketViolationWarningPacketSeverity::Warning;
            warning.packetId = PacketViolationWarningPacket::kId;
            warning.reason = "Server sent a ContainerClosePacket response packet when it should have sent the server initiated packet";
            Send(warning);
        });
    }

    void BedrockSession::RegisterEquipmentHandlers() {
        On<ClientStartItemCooldownPacket>([this](const ClientStartItemCooldownPacket& p) {
            if (runtimeId == 0)
                return;
            std::lock_guard lock(statusMutex);
            itemCooldowns.Start(p.category, p.duration);
            ++itemCooldownRevisions[itemcooldown::CategoryKey(p.category)];
        });

        On<PlayerHotbarPacket>([this](const PlayerHotbarPacket& p) {
            if (runtimeId == 0 || !p.selectSlot || p.selectedSlot > 8)
                return;
            std::lock_guard lock(statusMutex);
            inventory.selected = static_cast<int>(p.selectedSlot);
        });

        On<MobEquipmentPacket>([this](const MobEquipmentPacket& p) {
            if (runtimeId == 0 || p.runtimeEntityId != runtimeId || p.windowId != WindowID::Inventory ||
                p.selectedSlot > 8)
                return;
            std::lock_guard lock(statusMutex);
            // The native local-player branch only selects the hotbar slot.
            // InventorySlot/Content own item replacement; equipment updates for
            // remote actors are handled separately by EntityStateHandlers.
            inventory.selected = p.selectedSlot;
        });

        On<MobArmorEquipmentPacket>([this](const MobArmorEquipmentPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId)
                return;
            std::lock_guard lock(statusMutex);
            const ItemV4* items[]{&p.helmet, &p.chestplate, &p.leggings, &p.boots};
            for (int slot = 0; slot < 4; slot++) {
                inventory.armor[slot] = ConvertItemStack(*items[slot]);
                extraRaw[kInvArmor0 - 36 + slot] = *items[slot];
                ++itemSlotRevisions[kInvArmor0 + slot];
            }
        });

        On<PlayerArmorDamagePacket>([this](const PlayerArmorDamagePacket& p) {
            if (runtimeId == 0 || legacyInventory)
                return;
            std::lock_guard lock(statusMutex);
            for (const ArmorDamageEntry& entry : p.entries) {
                const int slot = static_cast<int>(entry.armorSlot);
                if (slot < 0 || slot >= 4)
                    continue;
                ItemV4& raw = extraRaw[kInvArmor0 - 36 + slot];
                if (SetItemDamage(raw, entry.damage)) {
                    inventory.armor[slot] = ConvertItemStack(raw);
                    ++itemSlotRevisions[kInvArmor0 + slot];
                }
            }
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

            const auto type = static_cast<uint32_t>(p.effectType);
            if (type > 1)
                return;
            std::lock_guard lock(statusMutex);
            pendingMovementEffects.push_back({type, p.effectDuration, p.tick});
        });

        On<MobEffectPacket>([this](const MobEffectPacket& p) {
            if (runtimeId == 0 || p.runtimeEntityId != runtimeId)
                return;
            if (p.eventId != MobEffectPacketEventId::Add &&
                p.eventId != MobEffectPacketEventId::Update &&
                p.eventId != MobEffectPacketEventId::Remove)
                return;
            if (PERLIN_ENV("PERLIN_BLOCK_DEBUG"))
                Log("effect", "id={} event={} duration={} serverTick={} afterAuthTick={}", p.effectId,
                    static_cast<int>(p.eventId), p.duration, p.tick, LastAuthTick());
            const bool on = p.eventId != MobEffectPacketEventId::Remove;
            std::lock_guard lock(statusMutex);
            pendingEffectUpdates.push_back(
                {p.tick, p.effectId, p.amplifier, on, p.duration, p.ambient, p.particles});
            auto existing = std::find_if(activeEffects.begin(), activeEffects.end(),
                                         [&](const ActiveEffect& effect) { return effect.id == p.effectId; });
            if (!on) {
                if (existing != activeEffects.end())
                    activeEffects.erase(existing);
                return;
            }

            const uint64_t now = LastAuthTick();
            const uint64_t stamp = p.tick != 0 ? p.tick : now;
            physics::bds::StatusEffectValue value{
                p.amplifier, physics::bds::StatusEffectTicksAt(p.duration, stamp, now),
                p.ambient, p.particles};
            if (existing != activeEffects.end()) {
                const int32_t remaining = physics::bds::StatusEffectTicksAt(
                    existing->duration, existing->appliedTick, now);
                if (remaining != 0)
                    value = physics::bds::MergeStatusEffect(
                        {existing->amplifier, remaining, existing->ambient, existing->particles}, value);
            }

            // The packet tick corrects history and shortens an already old
            // duration. Even a future-stamped effect begins its live countdown
            // immediately; the future timestamp is not an expiry-clock origin.
            ActiveEffect effect;
            effect.id = p.effectId;
            effect.amplifier = value.amplifier;
            effect.duration = value.duration;
            effect.appliedTick = now;
            effect.ambient = value.ambient;
            effect.particles = value.particles;
            if (existing != activeEffects.end())
                *existing = effect;
            else
                activeEffects.push_back(effect);
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
                    if (runtimeId != 0)
                        serverUsingItem = (value & (1 << 4)) != 0 ? 1 : 0;
                } else if (e.key == MetadataDictionaryEntryKey::Air && IntegerOf(e, value))
                    vitals.air = value;
                else if (e.key == MetadataDictionaryEntryKey::MaxAirdataMaxAir && IntegerOf(e, value) &&
                         value > 0)
                    vitals.maxAir = value;
            }
        });
    }

    void BedrockSession::RegisterHudHandlers() {
        On<LocatorBarPacket>([this](const LocatorBarPacket& p) {
            std::lock_guard lock(locatorMutex);
            for (const LocatorBarWaypoint& entry : p.waypoints) {
                const auto it = std::ranges::find_if(locatorWaypoints, [&](const LocatorWaypoint& w) {
                    return w.id == entry.groupHandle.bytes;
                });
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
                if (p.type == SetTitlePacketType::Reset) {
                    titleText.fadeIn = 10;
                    titleText.stay = 70;
                    titleText.fadeOut = 20;
                }
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
            if (t.type == TextPacketType::Popup || t.type == TextPacketType::Tip ||
                t.type == TextPacketType::JukeboxPopup) {
                std::lock_guard lock(statusMutex);
                titleText.actionBar = t.message;
                titleText.actionAt = std::chrono::steady_clock::now();
                titleText.serial++;
            }
        });
    }

    void BedrockSession::RegisterPlayerStateHandlers() {
        RegisterInventoryHandlers();
        RegisterContainerHandlers();
        RegisterEquipmentHandlers();
        RegisterHudHandlers();
    }
}

namespace perlin {
    void BedrockSession::ClearCrack(int x, int y, int z) {
        std::lock_guard lock(crackMutex);
        std::erase_if(crackStates, [&](const CrackState& c) { return c.x == x && c.y == y && c.z == z; });
        lastClearedCrack[0] = x;
        lastClearedCrack[1] = y;
        lastClearedCrack[2] = z;
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
