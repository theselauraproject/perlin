#include "util/Env.hpp"
#include "client/multiplayer/BedrockSession.hpp"
#include "client/player/PerlinStatusEffect.hpp"
#include "client/resources/ServerBlocks.hpp"

#include <algorithm>
#include <iterator>
#include <array>
#include <iostream>
#include "util/Log.hpp"

namespace perlin {
    using namespace proto;

    namespace {
        bool EndsWith(const std::string& s, const char* suffix) {
            const size_t n = std::char_traits<char>::length(suffix);
            return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
        }
        bool Has(const std::string& s, const char* part) {
            return s.contains(part);
        }

        int MaxStack(const std::string& full) {
            const std::string n = full.starts_with("minecraft:") ? full.substr(10) : full;
            static const char* const kOne[] = {
                "_sword",       "_pickaxe", "_axe",    "_shovel", "_hoe",      "_helmet", "_chestplate",
                "_leggings",    "_boots",   "_boat",   "_raft",   "_minecart", "_bed",    "_shulker_box",
                "_horse_armor", "_harness", "_bucket", "potion",  "_book",     "_disc",   "_banner_pattern"};
            for (const char* s : kOne)
                if (EndsWith(n, s) || (std::string(s) == "potion" && Has(n, "potion")))
                    return 1;
            static const char* const kOneNames[] = {"bow",
                                                    "crossbow",
                                                    "trident",
                                                    "shield",
                                                    "elytra",
                                                    "fishing_rod",
                                                    "shears",
                                                    "flint_and_steel",
                                                    "carrot_on_a_stick",
                                                    "warped_fungus_on_a_stick",
                                                    "saddle",
                                                    "totem_of_undying",
                                                    "cake",
                                                    "mace",
                                                    "brush",
                                                    "spyglass",
                                                    "minecart",
                                                    "turtle_helmet",
                                                    "wolf_armor",
                                                    "shulker_box",
                                                    "writable_book",
                                                    "written_book",
                                                    "enchanted_book",
                                                    "boat",
                                                    "lead_cake",
                                                    "bundle"};
            for (const char* s : kOneNames)
                if (n == s)
                    return 1;
            static const char* const k16[] = {"ender_pearl",  "snowball",    "egg",        "bucket",
                                              "honey_bottle", "wind_charge", "armor_stand"};
            for (const char* s : k16)
                if (n == s)
                    return 16;
            if (EndsWith(n, "_sign") || EndsWith(n, "_hanging_sign") || EndsWith(n, "_banner"))
                return 16;
            return 64;
        }

        int ArmorSlotOf(const std::string& full) {
            const std::string n = full.starts_with("minecraft:") ? full.substr(10) : full;
            if (EndsWith(n, "_helmet") || n == "carved_pumpkin" || EndsWith(n, "_head") ||
                EndsWith(n, "_skull"))
                return 0;
            if (EndsWith(n, "_chestplate") || n == "elytra")
                return 1;
            if (EndsWith(n, "_leggings"))
                return 2;
            if (EndsWith(n, "_boots"))
                return 3;

            CustomItemDef custom;
            if (ServerBlocks::Instance().Item(full, custom))
                return custom.armorSlot;
            return -1;
        }

        bool OffhandAllowed(const std::string& full) {
            const std::string n = full.starts_with("minecraft:") ? full.substr(10) : full;
            static const char* const kAllowed[] = {"shield",         "totem_of_undying", "arrow",
                                                   "spectral_arrow", "tipped_arrow",     "firework_rocket",
                                                   "filled_map",     "empty_map",        "map"};
            for (const char* a : kAllowed)
                if (n == a)
                    return true;
            return false;
        }

        const std::vector<uint8_t>* NbtOf(const ItemV4& item) {
            if (const auto* with = std::get_if<ItemExtraDataWithBlockingTick>(&item.extra))
                return with->nbt ? &with->nbt->nbt.bytes : nullptr;
            if (const auto* without = std::get_if<ItemExtraDataWithoutBlockingTick>(&item.extra))
                return without->nbt ? &without->nbt->nbt.bytes : nullptr;
            return nullptr;
        }

        bool SameStack(const ItemV4& a, const ItemV4& b) {
            if (a.networkId != b.networkId || a.metadata != b.metadata ||
                a.blockRuntimeId != b.blockRuntimeId)
                return false;
            const auto* na = NbtOf(a);
            const auto* nb = NbtOf(b);
            if (!na && !nb)
                return true;
            if (!na || !nb)
                return false;
            return *na == *nb;
        }

        bool TagMatches(const std::string& tag, const std::string& full) {
            const std::string t = tag.starts_with("minecraft:") ? tag.substr(10) : tag;
            const std::string n = full.starts_with("minecraft:") ? full.substr(10) : full;
            if (t == "planks")
                return EndsWith(n, "_planks");
            if (t == "logs" || t == "logs_that_burn" || Has(t, "_logs"))
                return Has(n, "_log") || EndsWith(n, "_stem") || EndsWith(n, "_wood") ||
                       EndsWith(n, "_hyphae");
            if (t == "coals")
                return n == "coal" || n == "charcoal";
            if (t == "stone_crafting_materials" || t == "stone_tool_materials")
                return n == "cobblestone" || n == "blackstone" || n == "cobbled_deepslate";
            if (t == "soul_fire_base_blocks")
                return n == "soul_sand" || n == "soul_soil";
            if (t == "wool")
                return EndsWith(n, "_wool");
            if (t == "wooden_slabs")
                return EndsWith(n, "_slab") && Has(n, "planks") == false && !Has(n, "stone") &&
                       !Has(n, "brick");
            std::string base = t;
            if (!base.empty() && base.back() == 's')
                base.pop_back();
            return !base.empty() && Has(n, base.c_str());
        }

        uint8_t LegacyActionId(ItemStackRequestActionsEntryTypeId id) {
            const uint32_t v = static_cast<uint32_t>(id);
            return static_cast<uint8_t>(v >= 7 ? v + 2 : v);
        }

        ContainerSlotType gChestSlotType = ContainerSlotType::Container;
        int gFurnaceKind = -1;

        void Address(StackRequestSlotInfo& s, int a, const ItemV4& raw) {
            using C = ContainerSlotType;
            C type = C::HotbarAndInventory;
            int slot = a;
            if (a >= BedrockSession::kInvArmor0 && a < BedrockSession::kInvOffhand) {
                type = C::Armor;
                slot = a - BedrockSession::kInvArmor0;
            } else if (a == BedrockSession::kInvOffhand) {
                type = C::Offhand;
                slot = 1;
            } else if (a >= BedrockSession::kInvCraft0 && a < BedrockSession::kInvOutput) {
                type = C::CraftingInput;
                slot = 28 + a - BedrockSession::kInvCraft0;
            } else if (a >= BedrockSession::kInvWork0 && a < BedrockSession::kInvSlots) {
                type = C::CraftingInput;
                slot = 32 + a - BedrockSession::kInvWork0;
            } else if (a == BedrockSession::kInvOutput) {
                type = C::CreativeOutput;
                slot = 50;
            } else if (a == BedrockSession::kInvCursor) {
                type = C::Cursor;
                slot = 0;
            } else if (a >= BedrockSession::kInvChest0 && a < BedrockSession::kInvChest0 + 3 && gFurnaceKind >= 0) {
                const int f = a - BedrockSession::kInvChest0;
                type = f == 1   ? C::FurnaceFuel
                       : f == 2 ? C::FurnaceOutput
                       : gFurnaceKind == 1 ? C::BlastFurnaceIngredient
                       : gFurnaceKind == 2 ? C::SmokerIngredient
                                           : C::FurnaceIngredient;
                slot = f == 0 ? 0 : f == 1 ? 1 : 2;
            } else if (a >= BedrockSession::kInvChest0 && a < BedrockSession::kInvTotal) {
                type = gChestSlotType;
                slot = a - BedrockSession::kInvChest0;
            } else if (a >= BedrockSession::kInvHorse0 && a < BedrockSession::kInvChest0) {
                const int h = a - BedrockSession::kInvHorse0;
                if (h < 2) {
                    type = C::HorseEquip;
                    slot = h;
                } else {
                    type = C::Container;
                    slot = h - 2;
                }
            }
            s.slotType.containerId = type;
            s.slotType.dynamicContainerId.reset();
            s.slot = static_cast<uint8_t>(slot);
            s.stackId = raw.stackId.value_or(0);
        }
    }

    BedrockSession::ItemStackInfo& BedrockSession::InvInfoLocked(int a) {
        if (a < 9)
            return inventory.hotbar[a];
        if (a < 36)
            return inventory.main[a - 9];
        if (a < 40)
            return inventory.armor[a - 36];
        if (a == kInvOffhand)
            return inventory.offhand;
        if (a < kInvOutput)
            return inventory.craft[a - kInvCraft0];
        if (a == kInvOutput)
            return inventory.output;
        if (a >= kInvChest0 && a < kInvTotal)
            return inventory.chest[a - kInvChest0];
        if (a >= kInvHorse0 && a < kInvChest0)
            return inventory.horse[a - kInvHorse0];
        if (a >= kInvWork0)
            return inventory.work[a - kInvWork0];
        return inventory.cursor;
    }

    ItemV4& BedrockSession::InvRawLocked(int a) {
        if (a >= kInvChest0 && a < kInvTotal)
            return chestRaw[a - kInvChest0];
        if (a >= kInvHorse0 && a < kInvChest0)
            return horseRaw[a - kInvHorse0];
        return a < 36 ? inventoryRaw[a] : extraRaw[a - 36];
    }

    namespace {
        struct Ing {
            bool empty = true;
            std::string name, tag;
            int meta = 0;
        };
        bool IngMatches(const Ing& ing, const BedrockSession::ItemStackInfo& it) {
            if (ing.empty)
                return it.Empty();
            if (it.Empty())
                return false;
            if (ing.name.empty() && !ing.tag.empty())
                return TagMatches(ing.tag, it.name);
            if (ing.name != it.name)
                return false;
            return ing.meta == 0x7fff || ing.meta < 0 || ing.meta == it.aux || it.aux == 0;
        }
    }

    void BedrockSession::InvRecomputeOutputLocked() {
        craftedRecipe = -1;
        inventory.output = ItemStackInfo{};
        extraRaw[kInvOutput - 36] = ItemV4{};
        const int side = workbenchOpen ? 3 : 2;
        const int cells = side * side;
        const auto cell = [&](int i) -> const ItemStackInfo& {
            return workbenchOpen ? inventory.work[i] : inventory.craft[i];
        };
        int filled = 0;
        for (int i = 0; i < cells; i++)
            filled += cell(i).Empty() ? 0 : 1;
        if (filled == 0)
            return;
        for (size_t ri = 0; ri < recipes.size(); ri++) {
            const RecipeInfo& r = recipes[ri];
            bool ok = false;
            if (r.shaped) {
                if (r.w > side || r.h > side)
                    continue;
                int x0 = side, y0 = side, x1 = -1, y1 = -1;
                for (int i = 0; i < cells; i++)
                    if (!cell(i).Empty()) {
                        x0 = std::min(x0, i % side);
                        x1 = std::max(x1, i % side);
                        y0 = std::min(y0, i / side);
                        y1 = std::max(y1, i / side);
                    }
                if (x1 - x0 + 1 != r.w || y1 - y0 + 1 != r.h)
                    continue;
                for (int mirror = 0; mirror < 2 && !ok; mirror++) {
                    bool good = true;
                    for (int y = 0; y < r.h && good; y++)
                        for (int x = 0; x < r.w && good; x++) {
                            const size_t idx = static_cast<size_t>(y * r.w + (mirror ? r.w - 1 - x : x));
                            if (idx >= r.in.size()) {
                                good = false;
                                break;
                            }
                            const auto& src = r.in[idx];
                            Ing ing{src.empty, src.name, src.tag, src.meta};
                            good = IngMatches(ing, cell((y0 + y) * side + (x0 + x)));
                        }
                    ok = good;
                }
            } else {
                std::vector<Ing> need;
                for (const auto& i : r.in)
                    if (!i.empty)
                        need.push_back(Ing{i.empty, i.name, i.tag, i.meta});
                if (static_cast<int>(need.size()) != filled)
                    continue;
                std::array<bool, 9> used{};
                std::function<bool(size_t)> place = [&](size_t k) {
                    if (k == need.size())
                        return true;
                    for (int c = 0; c < cells; c++) {
                        if (used[static_cast<size_t>(c)] || cell(c).Empty() || !IngMatches(need[k], cell(c)))
                            continue;
                        used[static_cast<size_t>(c)] = true;
                        if (place(k + 1))
                            return true;
                        used[static_cast<size_t>(c)] = false;
                    }
                    return false;
                };
                ok = place(0);
            }
            if (ok) {
                craftedRecipe = static_cast<int>(ri);
                inventory.output = r.outInfo;
                extraRaw[kInvOutput - 36] = r.out;
                return;
            }
        }
    }

    bool BedrockSession::InvBusy() const {
        std::lock_guard lock(statusMutex);
        return !invInFlight.empty() || !invQueue.empty();
    }

    void BedrockSession::InvEnqueue(std::function<bool(ItemStackRequest&)> op) {
        {
            std::lock_guard lock(statusMutex);
            invQueue.push_back(std::move(op));
        }
        InvPump();
    }

    void BedrockSession::InvPump() {
        for (;;) {
            ItemStackRequestPacket pkt;
            bool send = false, close = false;
            {
                std::lock_guard lock(statusMutex);
                const auto now = std::chrono::steady_clock::now();
                std::erase_if(invInFlight, [&](const auto& e) {
                    return now - e.second >= std::chrono::milliseconds(1500);
                });
                if (invInFlight.size() >= 8)
                    return;
                if (invQueue.empty()) {
                    if (!invCloseWanted || !invInFlight.empty())
                        return;
                    invCloseWanted = false;
                    close = true;
                } else {
                    auto op = std::move(invQueue.front());
                    invQueue.pop_front();
                    ItemStackRequest r;
                    r.requestId = nextStackRequestId;
                    nextStackRequestId -= 2;
                    r.cause = static_cast<ItemStackRequestCause>(-1);
                    InvSnapshot snapshot;
                    snapshot.view = inventory;
                    std::copy(std::begin(inventoryRaw), std::end(inventoryRaw), std::begin(snapshot.raw));
                    std::copy(std::begin(extraRaw), std::end(extraRaw), std::begin(snapshot.extra));
                    std::copy(std::begin(chestRaw), std::end(chestRaw), std::begin(snapshot.chest));
                    std::copy(std::begin(horseRaw), std::end(horseRaw), std::begin(snapshot.horse));
                    gChestSlotType = chestSlotType;
                    gFurnaceKind = inventory.furnaceKind;
                    if (legacyInventory) {
                        if (op(r) && !r.actions.empty()) {
                            Log("inventory", "legacy send {} actions", r.actions.size());
                            SendLegacyInventoryLocked(snapshot);
                        }
                        continue;
                    }
                    if (op(r) && !r.actions.empty()) {
                        invSnapshots[r.requestId] = std::move(snapshot);
                        if (invSnapshots.size() > 16)
                            invSnapshots.erase(std::prev(invSnapshots.end()));
                        {
                            std::string text;
                            for (const auto& a : r.actions)
                                text += " " + std::to_string(static_cast<int>(a.typeId)) + "(" +
                                        std::to_string(static_cast<int>(a.count)) + "," +
                                        std::to_string(static_cast<int>(a.source.slotType.containerId)) +
                                        ":" + std::to_string(static_cast<int>(a.source.slot)) + "#" +
                                        std::to_string(a.source.stackId) + "->" +
                                        std::to_string(static_cast<int>(a.destination.slotType.containerId)) +
                                        ":" + std::to_string(static_cast<int>(a.destination.slot)) + "#" +
                                        std::to_string(a.destination.stackId) + ")";
                            invSummaries[r.requestId] = text;
                            if (invSummaries.size() > 32)
                                invSummaries.erase(std::prev(invSummaries.end()));
                            if (PERLIN_ENV("PERLIN_INV_DEBUG"))
                                Log("inventory", "send request {}:{}", r.requestId, text);
                        }
                        invPendingRequest = r.requestId;
                        invPendingAt = now;
                        invInFlight[r.requestId] = now;
                        pkt.requests.push_back(std::move(r));
                        send = true;
                    }
                }
            }
            if (close) {
                SendCloseNow();
                return;
            }
            if (send)
                Send(pkt);
        }
    }

    namespace {
        struct Mover {
            BedrockSession& s;
            ItemStackRequest& r;
            std::function<BedrockSession::ItemStackInfo&(int)> info;
            std::function<ItemV4&(int)> raw;

            void Move(int src, int dst, int n) {
                auto& si = info(src);
                auto& di = info(dst);
                auto& sr = raw(src);
                auto& dr = raw(dst);
                ItemStackRequestActionsEntry a;
                const ItemStackRequestActionsEntryTypeId type =
                    dst == BedrockSession::kInvCursor ? ItemStackRequestActionsEntryTypeId::Take
                                                      : ItemStackRequestActionsEntryTypeId::Place;
                a.typeId = type;
                a.legacyTypeId = LegacyActionId(type);
                a.count = static_cast<uint8_t>(n);
                Address(a.source, src, sr);
                Address(a.destination, dst, dr);
                r.actions.push_back(a);
                if (di.Empty()) {
                    di = si;
                    dr = sr;
                    di.count = n;
                    dr.count = static_cast<uint16_t>(n);
                    if (n != si.count) {
                        dr.stackId = r.requestId;
                        dr.hasStackId = true;
                    }
                } else {
                    di.count += n;
                    dr.count = static_cast<uint16_t>(dr.count + n);
                }
                si.count -= n;
                sr.count = static_cast<uint16_t>(sr.count - n);
                if (si.count <= 0) {
                    si = BedrockSession::ItemStackInfo{};
                    sr = ItemV4{};
                }
            }

            void Swap(int a, int b) {
                ItemStackRequestActionsEntry act;
                act.typeId = ItemStackRequestActionsEntryTypeId::Swap;
                act.legacyTypeId = LegacyActionId(act.typeId);
                Address(act.source, a, raw(a));
                Address(act.destination, b, raw(b));
                r.actions.push_back(act);
                std::swap(info(a), info(b));
                std::swap(raw(a), raw(b));
            }
        };
    }

    static bool QuickMove(Mover& m, int src, const std::vector<std::pair<int, int>>& ranges) {
        auto& si = m.info(src);
        if (si.Empty())
            return false;
        const int max = MaxStack(si.name);
        bool moved = false;
        for (int pass = 0; pass < 2 && !si.Empty(); pass++)
            for (const auto& [from, to] : ranges)
                for (int d = from; d <= to && !si.Empty(); d++) {
                    if (d == src)
                        continue;
                    auto& di = m.info(d);
                    if (pass == 0) {
                        if (di.Empty() || !SameStack(m.raw(d), m.raw(src)) || di.count >= max)
                            continue;
                        m.Move(src, d, std::min(si.count, max - di.count));
                        moved = true;
                    } else if (di.Empty()) {
                        m.Move(src, d, std::min(si.count, max));
                        moved = true;
                    }
                }
        return moved;
    }

    static void CraftPrelude(Mover& m, BedrockSession::ItemStackInfo outInfo, const ItemV4& outRaw,
                             uint32_t recipeId) {
        ItemStackRequestActionsEntry craft;
        craft.typeId = ItemStackRequestActionsEntryTypeId::CraftRecipe;
        craft.legacyTypeId = LegacyActionId(craft.typeId);
        craft.recipeNetworkId = recipeId;
        craft.timesCrafted = 1;
        m.r.actions.push_back(craft);
        ItemStackRequestActionsEntry res;
        res.typeId = ItemStackRequestActionsEntryTypeId::ResultsDeprecated;
        res.legacyTypeId = LegacyActionId(res.typeId);
        ItemStackRequestInstanceDescriptor d;
        d.type = ItemStackRequestInstanceDescriptorType::Name;
        d.legacyType = 1;
        d.name = outInfo.name;
        d.metadata = static_cast<int32_t>(outRaw.metadata);
        d.count = static_cast<int16_t>(outInfo.count);
        d.blockRuntimeId = outRaw.blockRuntimeId;
        res.resultItems.push_back(d);
        res.timesCrafted = 1;
        m.r.actions.push_back(res);
        for (int i = 0; i < 13; i++) {
            const int a = i < 4 ? BedrockSession::kInvCraft0 + i : BedrockSession::kInvWork0 + i - 4;
            if (m.info(a).Empty())
                continue;
            ItemStackRequestActionsEntry c;
            c.typeId = ItemStackRequestActionsEntryTypeId::Consume;
            c.legacyTypeId = LegacyActionId(c.typeId);
            c.count = 1;
            Address(c.source, a, m.raw(a));
            m.r.actions.push_back(c);
            auto& inf = m.info(a);
            auto& rw = m.raw(a);
            inf.count -= 1;
            rw.count = static_cast<uint16_t>(rw.count - 1);
            if (inf.count <= 0) {
                inf = BedrockSession::ItemStackInfo{};
                rw = ItemV4{};
            }
        }
    }

    void BedrockSession::InvClick(int addr, bool right) {
        if (addr < 0 || addr >= kInvTotal || addr == kInvCursor)
            return;
        if (addr >= kInvChest0)
            Log("inventory", "chest click addr {}{}", addr,
                (InvInfoLocked(addr).Empty() ? " empty" : " has-item"));
        InvEnqueue([this, addr, right](ItemStackRequest& r) {
            Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); },
                    [this](int a) -> ItemV4& { return InvRawLocked(a); }};
            auto& S = InvInfoLocked(addr);
            auto& C = InvInfoLocked(kInvCursor);
            const ItemV4& sRaw = InvRawLocked(addr);
            const ItemV4& cRaw = InvRawLocked(kInvCursor);
            if (addr == kInvOutput) {
                if (S.Empty() || craftedRecipe < 0 || craftedRecipe >= static_cast<int>(recipes.size()))
                    return false;
                if (!C.Empty() && (!SameStack(cRaw, sRaw) || C.count + S.count > MaxStack(C.name)))
                    return false;
                const ItemStackInfo outInfo = S;
                const ItemV4 outRaw = sRaw;
                CraftPrelude(m, outInfo, outRaw, recipes[static_cast<size_t>(craftedRecipe)].netId);
                inventory.output = outInfo;
                extraRaw[kInvOutput - 36] = outRaw;
                extraRaw[kInvOutput - 36].stackId = r.requestId;
                extraRaw[kInvOutput - 36].hasStackId = true;
                m.Move(kInvOutput, kInvCursor, outInfo.count);
                InvRecomputeOutputLocked();
                return true;
            }
            const bool cEmpty = C.Empty(), sEmpty = S.Empty();
            if (inventory.furnaceKind >= 0 && addr == kInvChest0 + 2 && !cEmpty &&
                (sEmpty || !SameStack(sRaw, cRaw)))
                return false;
            if (!cEmpty) {
                if (addr >= kInvArmor0 && addr < kInvOffhand && ArmorSlotOf(C.name) != addr - kInvArmor0)
                    return false;
                if (addr == kInvOffhand && !OffhandAllowed(C.name))
                    return false;
                if (addr == kInvHorse0 && !C.name.contains("saddle"))
                    return false;
                if (addr == kInvHorse0 + 1 && !C.name.contains("horse_armor") && !C.name.contains("carpet"))
                    return false;
            }
            const int max = cEmpty ? 64 : MaxStack(C.name);
            if (!right) {
                if (cEmpty) {
                    if (sEmpty)
                        return false;
                    m.Move(addr, kInvCursor, S.count);
                } else if (sEmpty) {
                    m.Move(kInvCursor, addr, C.count);
                } else if (SameStack(sRaw, cRaw)) {
                    const int n = std::min(C.count, max - S.count);
                    if (n <= 0)
                        return false;
                    m.Move(kInvCursor, addr, n);
                } else {
                    m.Swap(kInvCursor, addr);
                }
            } else {
                if (cEmpty) {
                    if (sEmpty)
                        return false;
                    m.Move(addr, kInvCursor, (S.count + 1) / 2);
                } else if (sEmpty) {
                    m.Move(kInvCursor, addr, 1);
                } else if (SameStack(sRaw, cRaw)) {
                    if (S.count >= max)
                        return false;
                    m.Move(kInvCursor, addr, 1);
                } else {
                    m.Swap(kInvCursor, addr);
                }
            }
            if (IsCraftAddr(addr))
                InvRecomputeOutputLocked();
            return !r.actions.empty();
        });
    }

    void BedrockSession::InvShiftClick(int addr) {
        if (addr < 0 || addr >= kInvTotal || addr == kInvCursor)
            return;
        if (addr >= kInvChest0)
            Log("inventory", "chest shift-click addr {}{}", addr,
                (InvInfoLocked(addr).Empty() ? " empty" : " has-item"));
        InvEnqueue([this, addr](ItemStackRequest& r) {
            Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); },
                    [this](int a) -> ItemV4& { return InvRawLocked(a); }};
            auto& S = InvInfoLocked(addr);
            if (S.Empty())
                return false;
            const std::vector<std::pair<int, int>> mainThenHotbar = {{9, 35}, {0, 8}};
            bool ok = false;
            if (addr == kInvOutput) {
                if (craftedRecipe < 0 || craftedRecipe >= static_cast<int>(recipes.size()))
                    return false;
                const ItemStackInfo outInfo = S;
                ItemV4 outRaw = InvRawLocked(addr);
                CraftPrelude(m, outInfo, outRaw, recipes[static_cast<size_t>(craftedRecipe)].netId);
                inventory.output = outInfo;
                extraRaw[kInvOutput - 36] = outRaw;
                extraRaw[kInvOutput - 36].stackId = r.requestId;
                extraRaw[kInvOutput - 36].hasStackId = true;
                ok = QuickMove(m, kInvOutput, {{0, 8}, {9, 35}});
                inventory.output = ItemStackInfo{};
                extraRaw[kInvOutput - 36] = ItemV4{};
                InvRecomputeOutputLocked();
                return ok;
            }
            if (addr >= kInvChest0) {
                ok = QuickMove(m, addr, {{0, 8}, {9, 35}});
            } else if (addr < 36 && inventory.furnaceKind >= 0) {
                const std::string n = S.name.starts_with("minecraft:") ? S.name.substr(10) : S.name;
                const bool fuel = n == "coal" || n == "charcoal" || n == "coal_block" || n == "lava_bucket" ||
                                  n == "blaze_rod" || n == "stick" || n.ends_with("_planks") || n.contains("_log") ||
                                  n == "dried_kelp_block" || n == "bamboo";
                const int target = fuel ? kInvChest0 + 1 : kInvChest0;
                ok = QuickMove(m, addr, {{target, target}});
            } else if (addr < 36 && inventory.chestOpen && inventory.chestSlots > 0) {
                ok = QuickMove(m, addr, {{kInvChest0, kInvChest0 + inventory.chestSlots - 1}});
            } else if (addr >= kInvHorse0) {
                ok = QuickMove(m, addr, mainThenHotbar);
            } else if (addr < 36 && inventory.horseOpen &&
                       ((S.name.contains("saddle") && inventory.horse[0].Empty()) ||
                        ((S.name.contains("horse_armor") || S.name.contains("carpet")) &&
                         inventory.horse[1].Empty()))) {
                const int target = S.name.contains("saddle") ? kInvHorse0 : kInvHorse0 + 1;
                ok = QuickMove(m, addr, {{target, target}});
            } else if (addr < 36 && inventory.horseOpen && inventory.horseChestSlots > 0) {
                ok = QuickMove(m, addr, {{kInvHorse0 + 2, kInvHorse0 + 1 + inventory.horseChestSlots}});
            } else if (addr < 36) {
                const int slot = ArmorSlotOf(S.name);
                if (slot >= 0 && InvInfoLocked(kInvArmor0 + slot).Empty())
                    ok = QuickMove(m, addr, {{kInvArmor0 + slot, kInvArmor0 + slot}});
                if (!ok)
                    ok = QuickMove(m, addr,
                                   addr < 9 ? std::vector<std::pair<int, int>>{{9, 35}}
                                            : std::vector<std::pair<int, int>>{{0, 8}});
            } else {
                ok = QuickMove(m, addr, mainThenHotbar);
            }
            if (IsCraftAddr(addr))
                InvRecomputeOutputLocked();
            return ok;
        });
    }

    void BedrockSession::InvHotbarKey(int addr, int hotbar) {
        if (addr < 0 || (addr >= 36 && addr < kInvChest0) || addr >= kInvTotal || hotbar < 0 || hotbar > 8 ||
            addr == hotbar)
            return;
        InvEnqueue([this, addr, hotbar](ItemStackRequest& r) {
            Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); },
                    [this](int a) -> ItemV4& { return InvRawLocked(a); }};
            const bool addrEmpty = InvInfoLocked(addr).Empty(), hotbarEmpty = InvInfoLocked(hotbar).Empty();
            if (addrEmpty && hotbarEmpty)
                return false;

            if (hotbarEmpty)
                m.Move(addr, hotbar, InvInfoLocked(addr).count);
            else if (addrEmpty)
                m.Move(hotbar, addr, InvInfoLocked(hotbar).count);
            else
                m.Swap(hotbar, addr);
            return true;
        });
    }

    void BedrockSession::InvSwapOffhand(int addr) {
        if (addr < 0 || addr >= kInvTotal || addr == kInvOffhand || addr == kInvCursor || addr == kInvOutput)
            return;
        InvEnqueue([this, addr](ItemStackRequest& r) {
            Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); },
                    [this](int a) -> ItemV4& { return InvRawLocked(a); }};
            const auto& S = InvInfoLocked(addr);
            const auto& O = InvInfoLocked(kInvOffhand);
            if (S.Empty() && O.Empty())
                return false;
            if (!S.Empty() && !OffhandAllowed(S.name))
                return false;
            if (!O.Empty() && addr >= kInvArmor0 && addr < kInvOffhand && ArmorSlotOf(O.name) != addr - kInvArmor0)
                return false;
            m.Swap(addr, kInvOffhand);
            if (IsCraftAddr(addr))
                InvRecomputeOutputLocked();
            return true;
        });
    }

    void BedrockSession::SwapOffhand() {
        int slot = 0;
        {
            std::lock_guard lock(statusMutex);
            slot = std::clamp(inventory.selected, 0, 8);
        }
        InvSwapOffhand(slot);
    }

    BedrockSession::CreativeCatalog BedrockSession::Creative() const {
        std::lock_guard lock(statusMutex);
        return creative;
    }

    uint32_t BedrockSession::CreativeVersion() const {
        std::lock_guard lock(statusMutex);
        return creative.version;
    }

    void BedrockSession::InvSpread(const std::vector<int>& slots, int mode) {
        if (slots.empty())
            return;
        InvEnqueue([this, slots, mode](ItemStackRequest& r) {
            Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); },
                    [this](int a) -> ItemV4& { return InvRawLocked(a); }};
            auto& C = InvInfoLocked(kInvCursor);
            if (C.Empty())
                return false;
            const ItemV4 cRaw = InvRawLocked(kInvCursor);
            const int max = MaxStack(C.name);
            std::vector<int> valid;
            for (int a : slots) {
                if (a < 0 || (a >= kInvSlots && a < kInvChest0) || a >= kInvTotal || a == kInvCursor ||
                    a == kInvOutput)
                    continue;
                if (a >= kInvArmor0 && a < kInvOffhand && ArmorSlotOf(C.name) != a - kInvArmor0)
                    continue;
                if (a == kInvOffhand && !OffhandAllowed(C.name))
                    continue;
                const ItemStackInfo& S = InvInfoLocked(a);
                if (!S.Empty() && !SameStack(InvRawLocked(a), cRaw))
                    continue;
                valid.push_back(a);
            }
            if (valid.empty())
                return false;
            const int total = C.count;
            const int each = mode == 1 ? 1 : std::max(1, total / static_cast<int>(valid.size()));
            for (int a : valid) {
                if (InvInfoLocked(kInvCursor).Empty())
                    break;
                const int room = max - InvInfoLocked(a).count;
                const int n = std::min({each, room, InvInfoLocked(kInvCursor).count});
                if (n <= 0)
                    continue;
                m.Move(kInvCursor, a, n);
            }
            for (int a : valid)
                if (IsCraftAddr(a)) {
                    InvRecomputeOutputLocked();
                    break;
                }
            return !r.actions.empty();
        });
    }

    uint32_t BedrockSession::CreativeEntryFor(const ItemStackInfo& info) const {
        std::lock_guard lock(statusMutex);
        for (const CreativeItem& c : creative.items)
            if (c.info.name == info.name && c.info.aux == info.aux &&
                c.info.blockRuntimeId == info.blockRuntimeId)
                return c.entryId;
        for (const CreativeItem& c : creative.items)
            if (c.info.name == info.name && c.info.aux == info.aux)
                return c.entryId;
        return 0;
    }

    void BedrockSession::InvCreativeClone(int addr) {
        ItemStackInfo info;
        {
            std::lock_guard lock(statusMutex);
            if (addr < 0 || addr >= kInvSlots)
                return;
            info = InvInfoLocked(addr);
        }
        if (info.Empty())
            return;
        if (const uint32_t entry = CreativeEntryFor(info))
            InvCreativeTake(entry, 0);
    }

    void BedrockSession::InvCreativePlace(int addr) {
        ItemStackInfo info;
        {
            std::lock_guard lock(statusMutex);
            info = inventory.cursor;
        }
        if (info.Empty() || addr < 0 || addr >= 36)
            return;
        const uint32_t entry = CreativeEntryFor(info);
        if (!entry)
            return;
        InvEnqueue([this, entry, addr](ItemStackRequest& r) {
            const auto found = std::find_if(creative.items.begin(), creative.items.end(),
                                            [&](const CreativeItem& c) { return c.entryId == entry; });
            const auto rawIt = creativeRaw.find(entry);
            if (found == creative.items.end() || rawIt == creativeRaw.end())
                return false;
            auto& dst = InvInfoLocked(addr);
            ItemStackInfo out = found->info;
            out.count = MaxStack(out.name);
            ItemV4 raw = rawIt->second;
            raw.count = static_cast<uint16_t>(out.count);
            if (!dst.Empty() && !SameStack(InvRawLocked(addr), raw))
                return false;
            Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); },
                    [this](int a) -> ItemV4& { return InvRawLocked(a); }};
            ItemStackRequestActionsEntry craft;
            craft.typeId = ItemStackRequestActionsEntryTypeId::CraftCreative;
            craft.legacyTypeId = LegacyActionId(craft.typeId);
            craft.itemId = entry;
            craft.timesCrafted = 1;
            r.actions.push_back(craft);
            inventory.output = out;
            extraRaw[kInvOutput - 36] = raw;
            extraRaw[kInvOutput - 36].stackId = r.requestId;
            extraRaw[kInvOutput - 36].hasStackId = true;
            const int room = out.count - dst.count;
            if (room > 0)
                m.Move(kInvOutput, addr, room);
            inventory.output = ItemStackInfo{};
            extraRaw[kInvOutput - 36] = ItemV4{};
            return !r.actions.empty();
        });
    }

    void BedrockSession::InvCreativeTake(uint32_t entryId, int mode) {
        InvEnqueue([this, entryId, mode](ItemStackRequest& r) {
            const auto found = std::find_if(creative.items.begin(), creative.items.end(),
                                            [&](const CreativeItem& c) { return c.entryId == entryId; });
            const auto rawIt = creativeRaw.find(entryId);
            if (found == creative.items.end() || rawIt == creativeRaw.end())
                return false;
            ItemStackInfo out = found->info;
            out.count = mode == 1 ? 1 : MaxStack(out.name);
            ItemV4 raw = rawIt->second;
            raw.count = static_cast<uint16_t>(out.count);
            auto& C = InvInfoLocked(kInvCursor);
            const ItemV4& cRaw = InvRawLocked(kInvCursor);
            if (mode != 2 && !C.Empty() && (!SameStack(cRaw, raw) || C.count + out.count > MaxStack(C.name)))
                return false;
            Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); },
                    [this](int a) -> ItemV4& { return InvRawLocked(a); }};
            ItemStackRequestActionsEntry craft;
            craft.typeId = ItemStackRequestActionsEntryTypeId::CraftCreative;
            craft.legacyTypeId = LegacyActionId(craft.typeId);
            craft.itemId = entryId;
            craft.timesCrafted = 1;
            r.actions.push_back(craft);
            inventory.output = out;
            extraRaw[kInvOutput - 36] = raw;
            extraRaw[kInvOutput - 36].stackId = r.requestId;
            extraRaw[kInvOutput - 36].hasStackId = true;
            bool ok = true;
            if (mode == 2) {
                ok = QuickMove(m, kInvOutput, {{0, 8}, {9, 35}});
            } else {
                m.Move(kInvOutput, kInvCursor, out.count);
            }
            inventory.output = ItemStackInfo{};
            extraRaw[kInvOutput - 36] = ItemV4{};
            return ok;
        });
    }

    void BedrockSession::InvDestroyCursor() {
        InvEnqueue([this](ItemStackRequest& r) {
            auto& C = InvInfoLocked(kInvCursor);
            auto& raw = InvRawLocked(kInvCursor);
            if (C.Empty())
                return false;
            ItemStackRequestActionsEntry a;
            a.typeId = ItemStackRequestActionsEntryTypeId::Destroy;
            a.legacyTypeId = LegacyActionId(a.typeId);
            a.count = static_cast<uint8_t>(std::min(C.count, 255));
            Address(a.source, kInvCursor, raw);
            r.actions.push_back(a);
            C = ItemStackInfo{};
            raw = ItemV4{};
            return true;
        });
    }

    void BedrockSession::InvDropCursor(bool whole) {
        InvEnqueue([this, whole](ItemStackRequest& r) {
            auto& C = InvInfoLocked(kInvCursor);
            auto& raw = InvRawLocked(kInvCursor);
            if (C.Empty())
                return false;
            ItemStackRequestActionsEntry a;
            a.typeId = ItemStackRequestActionsEntryTypeId::Drop;
            a.legacyTypeId = LegacyActionId(a.typeId);
            a.count = static_cast<uint8_t>(whole ? std::min(C.count, 255) : 1);
            Address(a.source, kInvCursor, raw);
            r.actions.push_back(a);
            if (legacyInventory) {
                ItemV4 dropped = raw;
                dropped.count = static_cast<decltype(dropped.count)>(a.count);
                legacyDrops.push_back(std::move(dropped));
            }
            C.count -= a.count;
            raw.count = static_cast<uint16_t>(raw.count - a.count);
            if (C.count <= 0) {
                C = ItemStackInfo{};
                raw = ItemV4{};
            }
            return true;
        });
    }

    void BedrockSession::InvDropSlot(int addr, bool whole) {
        if (addr < 0 || addr >= 36)
            return;
        DropInventorySlot(addr, whole);
    }

    void BedrockSession::InvReturnCursor() {
        for (int src : {kInvCursor, kInvCraft0, kInvCraft0 + 1, kInvCraft0 + 2, kInvCraft0 + 3, kInvWork0,
                        kInvWork0 + 1, kInvWork0 + 2, kInvWork0 + 3, kInvWork0 + 4, kInvWork0 + 5,
                        kInvWork0 + 6, kInvWork0 + 7, kInvWork0 + 8}) {
            InvEnqueue([this, src](ItemStackRequest& r) {
                Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); },
                        [this](int a) -> ItemV4& { return InvRawLocked(a); }};
                const bool ok = QuickMove(m, src, {{0, 8}, {9, 35}});
                if (src >= kInvCraft0)
                    InvRecomputeOutputLocked();
                return ok;
            });
        }
    }
}

namespace perlin {
    bool BedrockSession::TakeHorseOpened() {
        std::lock_guard lock(statusMutex);
        const bool v = horseJustOpened;
        horseJustOpened = false;
        return v;
    }

    bool BedrockSession::SendLegacyInventoryLocked(const InvSnapshot& before) {
        InventoryTransactionPacket pkt;
        pkt.transaction.transactionType = TransactionTransactionType::Normal;
        const auto place = [&](int a, int& window, int& slot) {
            slot = a;
            if (a < 36) {
                window = 0;
                return true;
            }
            if (a >= kInvArmor0 && a < kInvOffhand) {
                window = 120;
                slot = a - kInvArmor0;
                return true;
            }
            if (a == kInvOffhand) {
                window = 119;
                slot = 0;
                return true;
            }
            if (a == kInvCursor) {
                window = 124;
                slot = 0;
                return true;
            }
            if (a >= kInvCraft0 && a < kInvOutput) {
                window = 124;
                slot = 28 + a - kInvCraft0;
                return true;
            }
            if (a >= kInvWork0 && a < kInvSlots) {
                window = 124;
                slot = 32 + a - kInvWork0;
                return true;
            }
            if (a >= kInvChest0 && a < kInvTotal && chestWindow >= 0) {
                window = chestWindow;
                slot = a - kInvChest0;
                return true;
            }
            if (a >= kInvHorse0 && a < kInvChest0 && horseWindow >= 0) {
                window = horseWindow;
                slot = a - kInvHorse0;
                return true;
            }
            return false;
        };
        const auto oldRaw = [&](int a) -> const ItemV4& {
            if (a < 36)
                return before.raw[a];
            if (a >= kInvChest0)
                return before.chest[a - kInvChest0];
            if (a >= kInvHorse0)
                return before.horse[a - kInvHorse0];
            return before.extra[a - 36];
        };
        // Compare the whole stack, NBT included: an enchanted and a plain sword share id, count and
        // damage, so swapping them used to send nothing for those slots and the server undid it.
        const auto same = [](const ItemV4& x, const ItemV4& y) {
            return x.count == y.count && SameStack(x, y);
        };
        for (int a = 0; a < kInvTotal; a++) {
            if (a == kInvOutput)
                continue;
            const ItemV4& was = oldRaw(a);
            ItemV4 now = InvRawLocked(a);
            if (same(was, now))
                continue;
            int window = 0, slot = 0;
            if (!place(a, window, slot))
                return false;
            if (now.hasStackId && now.stackId.value_or(0) <= 0) {
                now.hasStackId = false;
                now.stackId.reset();
            }
            TransactionActionsEntry e;
            e.sourceType = TransactionActionsEntrySourceType::Container;
            e.windowId = static_cast<int8_t>(window);
            e.slot = static_cast<uint32_t>(slot);
            e.oldItem = was;
            e.newItem = now;
            pkt.transaction.actions.push_back(std::move(e));
        }
        for (ItemV4& dropped : legacyDrops) {
            if (dropped.hasStackId && dropped.stackId.value_or(0) <= 0) {
                dropped.hasStackId = false;
                dropped.stackId.reset();
            }
            TransactionActionsEntry w;
            w.sourceType = TransactionActionsEntrySourceType::WorldInteraction;
            w.flags = 0u;
            w.slot = 0;
            w.oldItem = ItemV4{};
            w.newItem = std::move(dropped);
            pkt.transaction.actions.push_back(std::move(w));
        }
        legacyDrops.clear();
        if (pkt.transaction.actions.empty())
            return false;
        return Send(pkt);
    }

    BedrockSession::FurnaceProgress BedrockSession::Furnace() const {
        FurnaceProgress out;
        int window = -1, kind = -1;
        {
            std::lock_guard lock(statusMutex);
            window = chestWindow;
            kind = inventory.furnaceKind;
        }
        if (kind < 0 || window < 0)
            return out;
        std::lock_guard<std::mutex> lk(worldExtraMutex);
        const auto it = windowData.find(window);
        if (it == windowData.end())
            return out;
        const auto get = [&](int k) {
            const auto v = it->second.find(k);
            return v == it->second.end() ? 0 : v->second;
        };
        const float total = kind == 0 ? 200.0f : 100.0f;
        out.cook = std::clamp(static_cast<float>(get(0)) / total, 0.0f, 1.0f);
        const int max = get(2);
        out.burn = max > 0 ? std::clamp(static_cast<float>(get(1)) / static_cast<float>(max), 0.0f, 1.0f) : 0.0f;
        return out;
    }

    bool BedrockSession::TakeChestOpened() {
        std::lock_guard lock(statusMutex);
        const bool v = chestJustOpened;
        chestJustOpened = false;
        return v;
    }

    bool BedrockSession::TakeWorkbenchOpened() {
        std::lock_guard lock(statusMutex);
        const bool v = workbenchJustOpened;
        workbenchJustOpened = false;
        return v;
    }

    std::vector<BedrockSession::RecipeEntry> BedrockSession::GetRecipes(int side) const {
        std::lock_guard lock(statusMutex);
        std::vector<RecipeEntry> out;
        std::map<std::string, size_t> byKey;
        for (size_t ri = 0; ri < recipes.size(); ri++) {
            const RecipeInfo& r = recipes[ri];
            if (r.shaped ? (r.w > side || r.h > side) : (static_cast<int>(r.in.size()) > side * side))
                continue;
            int counts[36];
            for (int i = 0; i < 36; i++)
                counts[i] = (i < 9 ? inventory.hotbar[i] : inventory.main[i - 9]).count;
            bool craftable = true;
            for (const auto& src : r.in) {
                if (src.empty)
                    continue;
                const Ing ing{src.empty, src.name, src.tag, src.meta};
                bool found = false;
                for (int i = 0; i < 36 && !found; i++) {
                    const ItemStackInfo& it = i < 9 ? inventory.hotbar[i] : inventory.main[i - 9];
                    if (counts[i] > 0 && !it.Empty() && IngMatches(ing, it)) {
                        counts[i]--;
                        found = true;
                    }
                }
                if (!found) {
                    craftable = false;
                    break;
                }
            }
            const std::string key = r.outInfo.name + ";" + std::to_string(r.outInfo.aux) + ";" +
                                    std::to_string(r.outInfo.blockRuntimeId) + ";" +
                                    std::to_string(r.outInfo.count);
            if (const auto seen = byKey.find(key); seen != byKey.end()) {
                RecipeEntry& prev = out[seen->second];
                if (craftable && !prev.craftable) {
                    prev.craftable = true;
                    prev.index = static_cast<int>(ri);
                }
                continue;
            }
            byKey[key] = out.size();
            RecipeEntry e;
            e.index = static_cast<int>(ri);
            e.craftable = craftable;
            e.out = r.outInfo;
            out.push_back(std::move(e));
        }
        return out;
    }

    void BedrockSession::InvFillRecipe(int index) {
        bool big;
        {
            std::lock_guard lock(statusMutex);
            if (index < 0 || index >= static_cast<int>(recipes.size()))
                return;
            big = workbenchOpen;
        }
        const int side = big ? 3 : 2;
        for (int c = 0; c < side * side; c++) {
            const int addr = big ? kInvWork0 + c : kInvCraft0 + c;
            InvEnqueue([this, addr](ItemStackRequest& r) {
                Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); },
                        [this](int a) -> ItemV4& { return InvRawLocked(a); }};
                if (InvInfoLocked(addr).Empty())
                    return false;
                const bool ok = QuickMove(m, addr, {{0, 8}, {9, 35}});
                InvRecomputeOutputLocked();
                return ok;
            });
        }
        const RecipeInfo recipe = [&] {
            std::lock_guard lock(statusMutex);
            return recipes[static_cast<size_t>(index)];
        }();
        const int w = recipe.shaped ? recipe.w : side;
        std::vector<int> usedSources;
        for (size_t k = 0; k < recipe.in.size(); k++) {
            const auto& src = recipe.in[k];
            if (src.empty)
                continue;
            const int cx = recipe.shaped ? static_cast<int>(k) % w : static_cast<int>(k) % side;
            const int cy = recipe.shaped ? static_cast<int>(k) / w : static_cast<int>(k) / side;
            const int addr = big ? kInvWork0 + cy * side + cx : kInvCraft0 + cy * side + cx;
            InvEnqueue([this, src, addr](ItemStackRequest& r) {
                Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); },
                        [this](int a) -> ItemV4& { return InvRawLocked(a); }};
                const Ing ing{src.empty, src.name, src.tag, src.meta};
                for (int i = 0; i < 36; i++) {
                    const ItemStackInfo& it = InvInfoLocked(i);
                    if (it.Empty() || !IngMatches(ing, it))
                        continue;
                    m.Move(i, addr, 1);
                    InvRecomputeOutputLocked();
                    return true;
                }
                return false;
            });
        }
    }
}

namespace perlin {
    std::vector<BedrockSession::EffectState> BedrockSession::GetEffects() const {
        std::lock_guard lock(statusMutex);
        std::vector<EffectState> out;
        for (const ActiveEffect& e : activeEffects) {
            EffectState s;
            s.id = e.id;
            s.amplifier = e.amplifier;
            s.ambient = e.ambient;
            s.particles = e.particles;
            s.ticks = physics::bds::StatusEffectTicksAt(e.duration, e.appliedTick, LastAuthTick());
            if (s.ticks == 0)
                continue;
            out.push_back(s);
        }
        return out;
    }
}
