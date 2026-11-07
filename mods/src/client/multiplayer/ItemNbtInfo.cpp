#include "client/multiplayer/ItemNbtInfo.hpp"

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
                std::string s(reinterpret_cast<const char*>(&b[p]), n);
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

            void Enchants() {
                const uint8_t t = r.U8();
                const int32_t n = r.I32();
                if (t != 10 || n < 0 || n > 64)
                    return r.fail = true, void();
                for (int32_t i = 0; i < n && !r.fail; i++) {
                    int id = -1, lvl = 0;
                    while (!r.fail) {
                        const uint8_t ct = r.U8();
                        if (ct == 0)
                            break;
                        const std::string name = r.Str();
                        if (ct == 2 && name == "id")
                            id = r.I16();
                        else if (ct == 2 && name == "lvl")
                            lvl = r.I16();
                        else
                            SkipPayload(ct);
                    }
                    if (id >= 0)
                        info.enchants.emplace_back(id, lvl);
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
                    if (t == 2 && name == "Damage")
                        info.damage = r.I16();
                    else if (t == 3 && name == "Damage")
                        info.damage = r.I32();
                    else if (t == 3 && name == "customColor") {
                        info.color = r.I32();
                        info.hasColor = true;
                    } else if (t == 9 && (name == "ench" || name == "StoredEnchantments"))
                        Enchants();
                    else if (t == 10 && name == "display")
                        Display();
                    else
                        SkipPayload(t);
                }
                return !r.fail;
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
}
