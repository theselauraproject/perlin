#include "client/systems/multiplayer/handlers/HandlerSupport.hpp"

namespace perlin::handlers {
    using namespace proto;

    bool SaneWorldPos(float x, float y, float z) {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::fabs(x) < 100000.0f &&
               std::fabs(y) < 10000.0f && std::fabs(z) < 100000.0f;
    }

    std::string UuidKey(const Uuid& u) {
        static const char* hex = "0123456789abcdef";
        std::string s;
        for (uint8_t b : u.bytes) {
            s += hex[b >> 4];
            s += hex[b & 15];
        }
        return s;
    }

    bool MetaNumber(const MetadataDictionaryEntry& m, double& out) {
        if (const auto* v = std::get_if<1>(&m.value)) {
            out = static_cast<double>(*v);
            return true;
        }
        if (const auto* v = std::get_if<2>(&m.value)) {
            out = static_cast<double>(*v);
            return true;
        }
        if (const auto* v = std::get_if<3>(&m.value)) {
            out = static_cast<double>(*v);
            return true;
        }
        if (const auto* inner = std::get_if<4>(&m.value)) {
            if (const auto* v = std::get_if<1>(inner)) {
                out = *v;
                return true;
            }
            if (const auto* v = std::get_if<2>(inner)) {
                out = *v;
                return true;
            }
            if (const auto* v = std::get_if<3>(inner)) {
                out = *v;
                return true;
            }
            if (const auto* v = std::get_if<4>(inner)) {
                out = *v;
                return true;
            }
            if (const auto* v = std::get_if<8>(inner)) {
                out = static_cast<double>(*v);
                return true;
            }
        }
        return false;
    }

    bool MetaInt(const MetadataDictionaryEntry& m, int64_t& out) {
        const auto take = [&](const auto& v) {
            out = static_cast<int64_t>(v);
            return true;
        };
        if (const auto* v = std::get_if<1>(&m.value))
            return take(*v);
        if (const auto* v = std::get_if<2>(&m.value))
            return take(*v);
        if (const auto* v = std::get_if<3>(&m.value))
            return take(*v);
        if (const auto* inner = std::get_if<4>(&m.value)) {
            if (const auto* v = std::get_if<1>(inner))
                return take(*v);
            if (const auto* v = std::get_if<2>(inner))
                return take(*v);
            if (const auto* v = std::get_if<3>(inner))
                return take(*v);
            if (const auto* v = std::get_if<4>(inner))
                return take(*v);
            if (const auto* v = std::get_if<8>(inner))
                return take(*v);
        }
        return false;
    }

    void ApplyMetadata(entity::EntityTracker& tracker, uint64_t rid,
                       const std::vector<MetadataDictionaryEntry>& metadata) {
        std::optional<int> variant, mark, color;
        float boxWidth = -1.0f, boxHeight = -1.0f;
        static const bool debugTags = std::getenv("PERLIN_DEBUG_NAMETAGS") != nullptr;
        for (const MetadataDictionaryEntry& m : metadata) {
            if (m.key == MetadataDictionaryEntryKey::Nametag) {
                if (const auto* inner = std::get_if<4>(&m.value))
                    if (const auto* str = std::get_if<5>(inner)) {
                        tracker.SetNameTag(rid, *str);
                        if (debugTags) {
                            std::string flat = *str;
                            for (char& c : flat)
                                if (c == '\n')
                                    c = '|';
                            Log("nametag", "set rid={} text={}", rid, flat);
                        }
                    }
                continue;
            }
            double n = 0;
            if (!MetaNumber(m, n))
                continue;
            switch (m.key) {
            case MetadataDictionaryEntryKey::Flags: {
                int64_t bits = 0;
                tracker.SetFlags(rid, MetaInt(m, bits) ? static_cast<uint64_t>(bits)
                                                       : static_cast<uint64_t>(static_cast<int64_t>(n)));
                break;
            }
            case MetadataDictionaryEntryKey::FlagsExtended: {
                int64_t bits = 0;
                const uint64_t ext = MetaInt(m, bits) ? static_cast<uint64_t>(bits)
                                                      : static_cast<uint64_t>(static_cast<int64_t>(n));
                tracker.SetFlagsExtended(rid, ext);
                tracker.SetCrawling(rid, ((ext >> 50) & 1ull) != 0);
                tracker.SetSleeping(rid, ((ext >> 12) & 1ull) != 0);
                break;
            }
            case MetadataDictionaryEntryKey::PlayerFlags:
                tracker.SetSleeping(rid, (static_cast<int64_t>(n) & 2) != 0, true);
                break;
            case MetadataDictionaryEntryKey::Scale:
                tracker.SetScale(rid, static_cast<float>(n));
                break;
            case MetadataDictionaryEntryKey::BoundingboxWidth:
                boxWidth = static_cast<float>(n);
                break;
            case MetadataDictionaryEntryKey::BoundingboxHeight:
                boxHeight = static_cast<float>(n);
                break;
            case MetadataDictionaryEntryKey::Variant:
                variant = static_cast<int>(n);
                break;
            case MetadataDictionaryEntryKey::ExperienceValue: {
                const int xp = static_cast<int>(n);
                static constexpr int kSteps[] = {2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3};
                int icon = 0;
                for (size_t i = 0; i < std::size(kSteps); i++)
                    if (xp >= kSteps[i]) {
                        icon = 10 - static_cast<int>(i);
                        break;
                    }
                variant = icon;
                break;
            }
            case MetadataDictionaryEntryKey::MarkVariant:
                mark = static_cast<int>(n);
                break;
            case MetadataDictionaryEntryKey::Color:
                color = static_cast<int>(n);
                break;
            case MetadataDictionaryEntryKey::TargetEid: {
                int64_t target = 0;
                if (MetaInt(m, target))
                    tracker.SetTarget(rid, target);
                break;
            }
            case MetadataDictionaryEntryKey::OwnerEid:
                tracker.SetOwner(rid, static_cast<int64_t>(n));
                break;
            default:
                break;
            }
        }
        const auto clamp = [](std::optional<int> v) { return v ? std::optional<int>(std::max(*v, 0)) : v; };
        if (variant || mark || color)
            tracker.SetVariant(rid, clamp(variant), clamp(mark), clamp(color));

        if (boxWidth >= 0.0f || boxHeight >= 0.0f)
            tracker.SetBoundingBox(rid, boxWidth, boxHeight);
    }

    uint64_t MetaFlags(const std::vector<MetadataDictionaryEntry>& metadata) {
        for (const MetadataDictionaryEntry& m : metadata) {
            int64_t bits = 0;
            if (m.key == MetadataDictionaryEntryKey::Flags && MetaInt(m, bits))
                return static_cast<uint64_t>(bits);
        }
        return 0;
    }

    std::shared_ptr<const entity::SkinData> ConvertSkin(const Skin& s) {
        static std::atomic<uint64_t> counter{1};
        auto out = std::make_shared<entity::SkinData>();
        out->serial = counter++;
        out->skinId = s.skinId;
        out->skinWidth = s.skinData.width;
        out->skinHeight = s.skinData.height;
        out->skinRgba = s.skinData.data;
        if (static_cast<size_t>(out->skinWidth) * static_cast<size_t>(out->skinHeight) * 4 !=
            out->skinRgba.size()) {
            out->skinWidth = out->skinHeight = 0;
            out->skinRgba.clear();
        }
        out->capeWidth = s.capeData.width;
        out->capeHeight = s.capeData.height;
        out->capeRgba = s.capeData.data;
        if (static_cast<size_t>(out->capeWidth) * static_cast<size_t>(out->capeHeight) * 4 !=
            out->capeRgba.size()) {
            out->capeWidth = out->capeHeight = 0;
            out->capeRgba.clear();
        }
        out->slim = s.armSize == SkinArmSize::Slim;
        if (!s.geometryData.empty() && s.geometryData.contains("geometry")) {
            out->geometryJson = s.geometryData;
            const std::string& patch = s.skinResourcePack;
            const size_t at = patch.find("\"default\"");
            if (at != std::string::npos) {
                const size_t q1 = patch.find('"', patch.find(':', at) + 1);
                const size_t q2 = q1 == std::string::npos ? q1 : patch.find('"', q1 + 1);
                if (q2 != std::string::npos)
                    out->geometryName = patch.substr(q1 + 1, q2 - q1 - 1);
            }
            if (out->geometryName.empty())
                out->geometryName = "geometry.humanoid.custom";
        }
        return out;
    }

    bool EqualsIgnoreCase(const std::string& a, std::string_view b) {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        return true;
    }
}
