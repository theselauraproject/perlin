#include "render/chunk/Residency.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace perlin::render {
    namespace {
        int FloorDiv(int a, int b) {
            int q = a / b;
            return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
        }
        int Cheb(int ax, int az, int bx, int bz) {
            return std::max(std::abs(ax - bx), std::abs(az - bz));
        }
    }

    int ChunkResidency::LevelForDistance(int d) const {
        if (d > cfg.lodRadius)
            return -1;
        if (d <= cfg.fullRadius)
            return 0;
        int k = d - cfg.fullRadius;
        int level = 1;
        int span = std::max(1, cfg.lodStep);
        while (k > span && level < cfg.maxLevel) {
            k -= span;
            span *= 2;
            level++;
        }
        return level;
    }

    UnitKey ChunkResidency::UnitFor(ColumnPos c, int level) {
        if (level == 0)
            return {0, c.x, c.z};
        const int w = 1 << level;
        return {level, FloorDiv(c.x, w) * w, FloorDiv(c.z, w) * w};
    }

    int ChunkResidency::ColumnLevel(ColumnPos c, ColumnPos cam) const {
        if (LevelForDistance(Cheb(c.x, c.z, cam.x, cam.z)) < 0)
            return -1;
        // a coarse tile is only used when even its nearest column is far enough for that level
        for (int level = cfg.maxLevel; level >= 1; level--) {
            const UnitKey tile = UnitFor(c, level);
            const int w = tile.Width();
            const int dx = std::max({0, tile.x - cam.x, cam.x - (tile.x + w - 1)});
            const int dz = std::max({0, tile.z - cam.z, cam.z - (tile.z + w - 1)});
            if (LevelForDistance(std::max(dx, dz)) >= level)
                return level;
        }
        return 0;
    }

    uint8_t ChunkResidency::MaskFor(ColumnPos c) const {
        auto has = [&](int x, int z) {
            const auto it = columns.find({x, z});
            return it != columns.end() && it->second.present;
        };
        uint8_t m = 0;
        if (has(c.x - 1, c.z))
            m |= 1;
        if (has(c.x + 1, c.z))
            m |= 2;
        if (has(c.x, c.z - 1))
            m |= 4;
        if (has(c.x, c.z + 1))
            m |= 8;
        return m;
    }

    void ChunkResidency::SetData(ColumnPos pos, bool present) {
        Column& c = columns[pos];
        c.present = present;
        c.version = nextVersion++;
        if (!present)
            columns.erase(pos);
    }

    void ChunkResidency::MarkDirty(ColumnPos pos) {
        const auto it = columns.find(pos);
        if (it != columns.end())
            it->second.version = nextVersion++;
    }

    void ChunkResidency::OnBuilt(const UnitKey& unit, uint64_t version, uint8_t neighbourMask,
                                 uint64_t bytes) {
        Unit& u = units[unit];
        u.hasMesh = true;
        u.meshVersion = version;
        u.meshMask = neighbourMask;
        u.bytes = bytes;
        u.inFlight = false;
        u.failures = 0;
    }

    void ChunkResidency::OnBuildFailed(const UnitKey& unit) {
        const auto it = units.find(unit);
        if (it == units.end())
            return;
        Unit& u = it->second;
        u.inFlight = false;
        u.failures++;
        // a build that keeps failing must not be re-requested every tick
        u.retryAt = lastNow + (250ull << std::min<uint32_t>(u.failures, 6));
    }

    void ChunkResidency::OnLost(const UnitKey& unit) {
        const auto it = units.find(unit);
        if (it == units.end())
            return;
        it->second.hasMesh = false;
        it->second.inFlight = false;
        it->second.bytes = 0;
    }

    void ChunkResidency::Reset() {
        columns.clear();
        units.clear();
        holes.clear();
        haveCamera = false;
    }

    void ChunkResidency::Tick(uint64_t now, ColumnPos camera, float velX, float velZ, Plan& plan) {
        if (!haveCamera || Cheb(camera.x, camera.z, effectiveCamera.x, effectiveCamera.z) > cfg.hysteresis) {
            effectiveCamera = camera;
            haveCamera = true;
        }
        const ColumnPos cam = effectiveCamera;
        lastNow = now;

        struct Want {
            uint64_t version = 0;
        };
        std::unordered_map<UnitKey, Want, UnitKeyHash> want;
        want.reserve(columns.size());
        for (const auto& [pos, col] : columns) {
            if (!col.present)
                continue;
            const int level = ColumnLevel(pos, cam);
            if (level < 0)
                continue;
            Want& w = want[UnitFor(pos, level)];
            w.version = std::max(w.version, col.version);
        }

        for (auto& [key, u] : units) {
            u.wanted = false;
            u.covering = false;
        }
        holes.clear();

        struct Candidate {
            UnitKey key;
            bool hadMesh;
            int distance;
        };
        std::vector<Candidate> candidates;
        uint32_t inFlight = 0;
        for (const auto& [key, u] : units)
            if (u.inFlight)
                inFlight++;

        for (const auto& [key, w] : want) {
            Unit& u = units[key];
            u.wanted = true;
            u.wantedVersion = w.version;
            u.wantedMask = key.level == 0 ? MaskFor({key.x, key.z}) : 0;
            const bool current = u.hasMesh && u.meshVersion == w.version && (u.wantedMask & ~u.meshMask) == 0;
            if (current) {
                if (u.unusedSince != 0) {
                    stats.retainedRevived++;
                    u.unusedSince = 0;
                }
                continue;
            }
            u.unusedSince = 0;
            if (!u.hasMesh) {
                const int width = key.Width();
                for (int dz = 0; dz < width; dz++)
                    for (int dx = 0; dx < width; dx++)
                        holes.insert({key.x + dx, key.z + dz});
            }
            const bool pendingSame =
                u.inFlight && u.pendingVersion == w.version && u.pendingMask == u.wantedMask;
            if (pendingSame || now < u.retryAt)
                continue;
            const int w2 = key.Width();
            const int dx = std::max({0, key.x - cam.x, cam.x - (key.x + w2 - 1)});
            const int dz = std::max({0, key.z - cam.z, cam.z - (key.z + w2 - 1)});
            candidates.push_back({key, u.hasMesh, std::max(dx, dz)});
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            // holes first, then nearest; rebuilds of a mesh that is already visible can wait
            if (a.hadMesh != b.hadMesh)
                return !a.hadMesh;
            if (a.distance != b.distance)
                return a.distance < b.distance;
            if (a.key.level != b.key.level)
                return a.key.level < b.key.level;
            if (a.key.x != b.key.x)
                return a.key.x < b.key.x;
            return a.key.z < b.key.z;
        });
        const uint32_t room = inFlight >= cfg.maxInFlight ? 0 : cfg.maxInFlight - inFlight;
        const uint32_t take =
            std::min<uint32_t>({static_cast<uint32_t>(candidates.size()), cfg.maxBuildsPerTick, room});
        for (uint32_t i = 0; i < take; i++) {
            Unit& u = units[candidates[i].key];
            u.inFlight = true;
            u.pendingVersion = u.wantedVersion;
            u.pendingMask = u.wantedMask;
            plan.build.push_back({candidates[i].key, u.wantedVersion, u.wantedMask});
        }

        // what nobody wants any more: keep it while it hides a hole, then for retainMs, then drop
        const float lookX = static_cast<float>(cam.x) + velX * cfg.lookaheadChunks;
        const float lookZ = static_cast<float>(cam.z) + velZ * cfg.lookaheadChunks;
        struct Retained {
            UnitKey key;
            float score;
            uint64_t bytes;
        };
        std::vector<Retained> retained;
        uint64_t retainedBytes = 0;
        std::vector<UnitKey> drop;
        for (auto& [key, u] : units) {
            if (u.wanted)
                continue;
            if (!u.hasMesh) {
                if (!u.inFlight)
                    drop.push_back(key);
                continue;
            }
            if (!holes.empty()) {
                const int width = key.Width();
                for (int dz = 0; dz < width && !u.covering; dz++)
                    for (int dx = 0; dx < width; dx++)
                        if (holes.contains({key.x + dx, key.z + dz})) {
                            u.covering = true;
                            break;
                        }
            }
            if (u.covering || u.unusedSince == 0)
                u.unusedSince = now;
            if (!u.covering && now - u.unusedSince > cfg.retainMs) {
                plan.evict.push_back(key);
                u.hasMesh = false;
                u.bytes = 0;
                stats.retainedExpired++;
                if (!u.inFlight)
                    drop.push_back(key);
                continue;
            }
            if (!u.covering) {
                const float cx = static_cast<float>(key.x) + static_cast<float>(key.Width()) * 0.5f;
                const float cz = static_cast<float>(key.z) + static_cast<float>(key.Width()) * 0.5f;
                const float dist = std::max(std::fabs(cx - lookX), std::fabs(cz - lookZ));
                // older and farther from where the camera is heading goes first
                const float age = static_cast<float>(now - u.unusedSince) /
                                  static_cast<float>(std::max<uint64_t>(1, cfg.retainMs));
                retained.push_back({key, dist + age, u.bytes});
                retainedBytes += u.bytes;
            }
        }
        if (retainedBytes > cfg.retainBudgetBytes) {
            std::sort(retained.begin(), retained.end(), [](const Retained& a, const Retained& b) {
                if (a.score != b.score)
                    return a.score > b.score;
                if (a.key.x != b.key.x)
                    return a.key.x < b.key.x;
                return a.key.z < b.key.z;
            });
            for (const Retained& r : retained) {
                if (retainedBytes <= cfg.retainBudgetBytes)
                    break;
                Unit& u = units[r.key];
                plan.evict.push_back(r.key);
                u.hasMesh = false;
                u.bytes = 0;
                retainedBytes -= r.bytes;
                stats.retainedOverBudget++;
                if (!u.inFlight)
                    drop.push_back(r.key);
            }
        }
        for (const UnitKey& k : drop)
            units.erase(k);
    }

    void ChunkResidency::CollectDraw(std::vector<DrawItem>& out) const {
        for (const auto& [key, u] : units)
            if (u.hasMesh && (u.wanted || u.covering))
                out.push_back({key});
        // stable order keeps the frame's draw list reproducible
        std::sort(out.begin(), out.end(), [](const DrawItem& a, const DrawItem& b) {
            if (a.unit.level != b.unit.level)
                return a.unit.level < b.unit.level;
            if (a.unit.x != b.unit.x)
                return a.unit.x < b.unit.x;
            return a.unit.z < b.unit.z;
        });
    }

    ResidencyStats ChunkResidency::Stats() const {
        ResidencyStats s = stats;
        s.wanted = s.resident = s.retained = s.inFlight = 0;
        s.residentBytes = s.retainedBytes = 0;
        for (const auto& [key, u] : units) {
            if (u.wanted)
                s.wanted++;
            if (u.inFlight)
                s.inFlight++;
            if (!u.hasMesh)
                continue;
            if (u.wanted) {
                s.resident++;
                s.residentBytes += u.bytes;
            } else {
                s.retained++;
                s.retainedBytes += u.bytes;
            }
        }
        return s;
    }
}
