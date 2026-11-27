#include "client/systems/multiplayer/ItemNbtInfo.hpp"
#include "client/systems/multiplayer/ItemDamage.hpp"

#include <cstring>

namespace perlin {
    namespace {
        struct Reader {
            const std::vector<uint8_t>& b;
            size_t p = 0;
            bool net = false;
            bool fail = false;

            uint8_t U8() {
                if (p >= b.size()) {
                    fail = true;
                    return 0;
                }
                return b[p++];
            }
            uint64_t Var(int maxBytes) {
                uint64_t v = 0;
                for (int i = 0; i < maxBytes; i++) {
                    const uint8_t c = U8();
                    v |= static_cast<uint64_t>(c & 0x7f) << (7 * i);
                    if (!(c & 0x80))
                        return v;
                    if (fail)
                        return 0;
                }
                fail = true;
                return 0;
            }
            int16_t I16() {
                if (p + 2 > b.size()) {
                    fail = true;
                    return 0;
                }
                int16_t v;
                std::memcpy(&v, &b[p], 2);
                p += 2;
                return v;
            }
            int32_t I32() {
                if (net) {
                    const uint32_t r = static_cast<uint32_t>(Var(5));
                    return static_cast<int32_t>((r >> 1) ^ (~(r & 1) + 1));
                }
                if (p + 4 > b.size()) {
                    fail = true;
                    return 0;
                }
                int32_t v;
                std::memcpy(&v, &b[p], 4);
                p += 4;
                return v;
            }
            void Skip(size_t n) {
                if (p + n > b.size()) {
                    fail = true;
                    return;
                }
                p += n;
            }
            std::string Str() {
                size_t n = net ? static_cast<size_t>(Var(5)) : static_cast<uint16_t>(I16());
                if (fail || p + n > b.size()) {
                    fail = true;
                    return {};
                }
                std::string s(reinterpret_cast<const char*>(b.data() + p), n);
                p += n;
                return s;
            }
        };

        struct Walk {
            Reader r;
            ItemNbtInfo info;
            int depth = 0;

            void SkipPayload(uint8_t type) {
                if (r.fail || depth > 32)
                    return r.fail = true, void();
                switch (type) {
                case 1:
                    r.Skip(1);
                    break;
                case 2:
                    r.Skip(2);
                    break;
                case 3:
                    r.I32();
                    break;
                case 4:
                    r.net ? static_cast<void>(r.Var(10)) : r.Skip(8);
                    break;
                case 5:
                    r.Skip(4);
                    break;
                case 6:
                    r.Skip(8);
                    break;
                case 7: {
                    const int32_t n = r.I32();
                    if (n < 0)
                        return r.fail = true, void();
                    r.Skip(static_cast<size_t>(n));
                    break;
                }
                case 8:
                    r.Str();
                    break;
                case 9: {
                    const uint8_t t = r.U8();
                    const int32_t n = r.I32();
                    if (n < 0)
                        return r.fail = true, void();
                    depth++;
                    for (int32_t i = 0; i < n && !r.fail; i++)
                        SkipPayload(t);
                    depth--;
                    break;
                }
                case 10: {
                    depth++;
                    while (!r.fail) {
                        const uint8_t t = r.U8();
                        if (t == 0)
                            break;
                        r.Str();
                        SkipPayload(t);
                    }
                    depth--;
                    break;
                }
                case 11: {
                    const int32_t n = r.I32();
                    if (n < 0)
                        return r.fail = true, void();
                    for (int32_t i = 0; i < n && !r.fail; i++)
                        r.I32();
                    break;
                }
                case 12: {
                    const int32_t n = r.I32();
                    if (n < 0)
                        return r.fail = true, void();
                    for (int32_t i = 0; i < n && !r.fail; i++)
                        r.net ? static_cast<void>(r.Var(10)) : r.Skip(8);
                    break;
                }
                default:
                    r.fail = true;
                    break;
                }
            }

            void Enchants(bool active) {
                const uint8_t t = r.U8();
                const int32_t n = r.I32();
                if (t != 10 || n < 0 || n > 64)
                    return r.fail = true, void();
                for (int32_t i = 0; i < n && !r.fail; i++) {
                    int id = -1, lvl = 0;
                    bool hasId = false;
                    while (!r.fail) {
                        const uint8_t ct = r.U8();
                        if (ct == 0)
                            break;
                        const std::string name = r.Str();
                        if (ct == 2 && name == "id") {
                            id = r.I16();
                            hasId = true;
                        } else if (ct == 2 && name == "lvl")
                            lvl = r.I16();
                        else
                            SkipPayload(ct);
                    }
                    if (id >= 0)
                        info.enchants.emplace_back(id, lvl);
                    if (active) {
                        // ItemEnchants::_fromList narrows its ShortTag id to
                        // an unsigned byte and ignores ids outside its table.
                        const int nativeId = hasId ? static_cast<uint8_t>(id) : 0;
                        if (nativeId <= 41)
                            info.activeEnchants.emplace_back(nativeId, lvl);
                    }
                }
            }

            void Display() {
                while (!r.fail) {
                    const uint8_t t = r.U8();
                    if (t == 0)
                        break;
                    const std::string name = r.Str();
                    if (t == 8 && name == "Name")
                        info.customName = r.Str();
                    else if (t == 9 && name == "Lore") {
                        const uint8_t et = r.U8();
                        const int32_t n = r.I32();
                        if (n < 0 || n > 64)
                            return r.fail = true, void();
                        for (int32_t i = 0; i < n && !r.fail; i++) {
                            if (et == 8)
                                info.lore.push_back(r.Str());
                            else
                                SkipPayload(et);
                        }
                    } else
                        SkipPayload(t);
                }
            }

            void ChargedItem() {
                info.chargedItemName.clear();
                info.chargedItemCount = 0;
                ++depth;
                while (!r.fail) {
                    const uint8_t type = r.U8();
                    if (type == 0)
                        break;
                    const std::string name = r.Str();
                    if (name == "Name") {
                        info.chargedItemName.clear();
                        if (type == 8)
                            info.chargedItemName = r.Str();
                        else
                            SkipPayload(type);
                    } else if (name == "Count") {
                        info.chargedItemCount = 0;
                        if (type == 1)
                            info.chargedItemCount = r.U8();
                        else
                            SkipPayload(type);
                    } else {
                        SkipPayload(type);
                    }
                }
                --depth;
            }

            bool Run() {
                if (r.U8() != 10)
                    return false;
                r.Str();
                while (!r.fail) {
                    const uint8_t t = r.U8();
                    if (t == 0)
                        break;
                    const std::string name = r.Str();
                    info.tagCount++;
                    if (t == 2 && name == "Damage") {
                        info.hasDamage = true;
                        info.damageTagType = t;
                        info.damage = r.I16();
                    } else if (t == 3 && name == "Damage") {
                        info.hasDamage = true;
                        info.damageTagType = t;
                        info.damage = r.I32();
                    } else if (t == 3 && name == "customColor") {
                        info.color = r.I32();
                        info.hasColor = true;
                    } else if (t == 9 && (name == "ench" || name == "StoredEnchantments"))
                        Enchants(name == "ench");
                    else if (t == 10 && name == "display")
                        Display();
                    else if (t == 10 && name == "chargedItem")
                        ChargedItem();
                    else
                        SkipPayload(t);
                }
                // The item codec supplies exactly one NBT root. Requiring its
                // full extent also prevents a malformed little-endian root
                // from looking like an empty network-NBT root on fallback.
                return !r.fail && r.p == r.b.size();
            }
        };

        bool Attempt(const std::vector<uint8_t>& bytes, bool net, ItemNbtInfo& out) {
            Walk w{Reader{bytes, 0, net, false}, {}, 0};
            if (!w.Run())
                return false;
            w.info.ok = true;
            out = std::move(w.info);
            return true;
        }
    }

    ItemNbtInfo ParseItemNbt(const std::vector<uint8_t>& bytes) {
        ItemNbtInfo out;
        if (!Attempt(bytes, false, out))
            Attempt(bytes, true, out);
        return out;
    }

    bool SetItemNbtDamage(std::vector<uint8_t>& bytes, int16_t damage) {
        static const std::vector<uint8_t> emptyCompound{10, 0, 0, 0};
        const auto& source = bytes.empty() ? emptyCompound : bytes;
        Walk walk{Reader{source, 0, false, false}, {}, 0};
        if (walk.r.U8() != 10)
            return false;
        walk.r.Str();
        if (walk.r.fail)
            return false;

        std::vector<uint8_t> result(source.begin(), source.begin() + walk.r.p);
        const auto appendDamage = [&] {
            static constexpr uint8_t name[]{3, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e'};
            result.insert(result.end(), std::begin(name), std::end(name));
            const uint32_t value = static_cast<uint32_t>(static_cast<int32_t>(damage));
            for (unsigned shift = 0; shift < 32; shift += 8)
                result.push_back(static_cast<uint8_t>(value >> shift));
        };
        bool replaced = false;
        while (!walk.r.fail) {
            const size_t start = walk.r.p;
            const uint8_t type = walk.r.U8();
            if (walk.r.fail)
                return false;
            if (type == 0) {
                if (walk.r.p != source.size())
                    return false;
                if (!replaced)
                    appendDamage();
                result.push_back(0);
                bytes = std::move(result);
                return true;
            }
            const std::string name = walk.r.Str();
            walk.SkipPayload(type);
            if (walk.r.fail)
                return false;
            if (name == "Damage") {
                if (!replaced)
                    appendDamage();
                replaced = true;
            } else {
                result.insert(result.end(), source.begin() + start, source.begin() + walk.r.p);
            }
        }
        return false;
    }

    bool SetItemDamage(proto::ItemV4& item, int16_t damage) {
        if (item.networkId == 0 || item.count == 0)
            return false;
        const auto update = [&](auto& extra) {
            std::vector<uint8_t> bytes = extra.nbt ? extra.nbt->nbt.bytes : std::vector<uint8_t>{};
            if (!SetItemNbtDamage(bytes, damage))
                return false;
            if (!extra.nbt) {
                extra.nbt.emplace();
                extra.nbt->version = 1;
            }
            extra.nbt->nbt.bytes = std::move(bytes);
            extra.hasNbt = decltype(extra.hasNbt)::True;
            return true;
        };
        if (auto* extra = std::get_if<proto::ItemExtraDataWithBlockingTick>(&item.extra))
            return update(*extra);
        if (auto* extra = std::get_if<proto::ItemExtraDataWithoutBlockingTick>(&item.extra))
            return update(*extra);
        if (item.networkId == net::context::ShieldItemID()) {
            proto::ItemExtraDataWithBlockingTick extra;
            if (!update(extra))
                return false;
            item.extra = std::move(extra);
        } else {
            proto::ItemExtraDataWithoutBlockingTick extra;
            if (!update(extra))
                return false;
            item.extra = std::move(extra);
        }
        return true;
    }
}
