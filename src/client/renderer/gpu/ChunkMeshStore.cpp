#include "client/renderer/gpu/ChunkMeshStore.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cmath>
#include <cstdio>

namespace perlin::gpu {
    namespace {
        constexpr uint32_t kPageVerts = 1u << 20;
    }

    void ChunkMeshStore::Shutdown() {
        Clear();
        if (!gpu)
            return;
        for (Page& p : pages) {
            p.vb.Destroy(gpu->Dev());
            p.ext.Destroy(gpu->Dev());
            for (Arena& a : p.ib)
                a.Destroy(gpu->Dev());
        }
        pages.clear();
    }

    void ChunkMeshStore::SetExtension(uint32_t bytesPerVertex, ExtendFn convert) {
        if (bytesPerVertex == extBytes && static_cast<bool>(convert) == static_cast<bool>(extConvert)) {
            extConvert = std::move(convert);
            return;
        }
        chunks.clear();
        lods.clear();
        lodGroups.clear();
        visible.clear();
        visibleSections.clear();
        if (gpu) {
            gpu->Dev().wait_idle();
            for (Page& p : pages) {
                if (!p.live)
                    continue;
                p.vb.Destroy(gpu->Dev());
                p.ext.Destroy(gpu->Dev());
                for (Arena& a : p.ib)
                    a.Destroy(gpu->Dev());
                p.live = false;
            }
        }
        pages.clear();
        extBytes = bytesPerVertex;
        extConvert = std::move(convert);
        generation++;
    }

    void ChunkMeshStore::Clear() {
        meshEpoch++;
        chunks.clear();
        lods.clear();
        lodGroups.clear();
        visible.clear();
        visibleSections.clear();

        for (Page& p : pages) {
            if (!p.live)
                continue;
            p.vb.Reset();
            for (Arena& a : p.ib)
                a.Reset();
        }
        generation++;
    }

    ChunkMeshStore::Backup ChunkMeshStore::Save() {
        std::vector<Gpu::BufferSource> sources;
        for (Page& p : pages) {
            if (!p.live)
                continue;
            sources.push_back({p.vb.Buffer(), p.vb.BarrierState(), p.vb.Bytes()});
            if (extBytes)
                sources.push_back({p.ext.Buffer(), p.ext.BarrierState(), p.ext.Bytes()});
            for (Arena& a : p.ib)
                sources.push_back({a.Buffer(), a.BarrierState(), a.Bytes()});
        }
        Backup out;
        out.blobs = gpu->ReadBuffers(sources);
        return out;
    }

    void ChunkMeshStore::ReleaseGpu() {
        meshEpoch++;
        const auto drop = [&](Arena& a) {
            if (a.Buffer()) {
                gpu->ForgetBuffer(a.Buffer());
                gpu->Dev().destroy(a.Buffer());
            }
            a.Detach();
        };
        for (Page& p : pages) {
            if (!p.live)
                continue;
            drop(p.vb);
            if (extBytes)
                drop(p.ext);
            for (Arena& a : p.ib)
                drop(a);
        }
    }

    bool ChunkMeshStore::Load(Backup&& backup) {
        meshEpoch++;
        size_t next = 0;
        bool ok = true;
        const auto restore = [&](Arena& a) {
            a.Detach();
            if (!a.Rebuild(gpu->Dev())) {
                ok = false;
                return;
            }
            if (next < backup.blobs.size() && !backup.blobs[next].empty())
                gpu->WriteBuffer(a, 0, backup.blobs[next].data(), backup.blobs[next].size());
            next++;
        };
        for (Page& p : pages) {
            if (!p.live)
                continue;
            restore(p.vb);
            if (extBytes)
                restore(p.ext);
            for (Arena& a : p.ib)
                restore(a);
        }
        return ok;
    }

    int ChunkMeshStore::CreatePage(uint32_t vCap, const std::array<uint32_t, kRenderLayerCount>& iCap) {
        Page p;
        bool ok = p.vb.Create(gpu->Dev(), vCap, sizeof(Vertex),
                              conduit::buffer_usage::vertex | conduit::buffer_usage::storage, "chunk vertices");
        if (ok && extBytes)
            ok = p.ext.Create(gpu->Dev(), vCap, extBytes, conduit::buffer_usage::vertex, "chunk vertex extras");
        for (size_t l = 0; l < kRenderLayerCount && ok; l++)
            ok = p.ib[l].Create(gpu->Dev(), std::max(1u, iCap[l]), sizeof(uint32_t),
                                conduit::buffer_usage::index | conduit::buffer_usage::storage, "chunk indices");
        if (!ok) {
            p.vb.Destroy(gpu->Dev());
            p.ext.Destroy(gpu->Dev());
            for (Arena& a : p.ib)
                a.Destroy(gpu->Dev());
            return -1;
        }
        p.live = true;
        for (size_t i = 0; i < pages.size(); i++)
            if (!pages[i].live) {
                pages[i] = std::move(p);
                return static_cast<int>(i);
            }
        pages.push_back(std::move(p));
        return static_cast<int>(pages.size()) - 1;
    }

    bool ChunkMeshStore::Alloc(uint32_t v, const std::array<uint32_t, kRenderLayerCount>& need, Chunk& out) {
        const auto tryPage = [&](Page& pg, int index) {
            if (!pg.live)
                return false;
            pg.emptyFrames = 0;
            uint32_t vo = 0;
            if (!pg.vb.Alloc(v, vo))
                return false;
            std::array<uint32_t, kRenderLayerCount> io{};
            for (size_t l = 0; l < kRenderLayerCount; l++) {
                if (need[l] && !pg.ib[l].Alloc(need[l], io[l])) {
                    for (size_t k = 0; k < l; k++)
                        if (need[k])
                            pg.ib[k].Free(io[k], need[k]);
                    pg.vb.Free(vo, v);
                    return false;
                }
            }
            out.page = index;
            out.vOff = vo;
            out.iOff = io;
            return true;
        };
        for (size_t pi = 0; pi < pages.size(); pi++)
            if (tryPage(pages[pi], static_cast<int>(pi)))
                return true;
        const uint32_t pageVerts =
            std::min<uint32_t>(kPageVerts, (1u << 18) << std::min<size_t>(pages.size(), 2));
        const uint32_t base = (pageVerts / 4) * 6;
        std::array<uint32_t, kRenderLayerCount> caps = {base, base / 2, base / 2, base / 4, base / 8};
        for (size_t l = 0; l < kRenderLayerCount; l++)
            caps[l] = std::max(caps[l], need[l]);
        const int created = CreatePage(std::max(pageVerts, v), caps);
        return created >= 0 && tryPage(pages[static_cast<size_t>(created)], created);
    }

    void ChunkMeshStore::ReleaseEmptyPages() {
        for (Page& p : pages) {
            if (!p.live)
                continue;
            bool unused = p.vb.Unused();
            for (size_t l = 0; l < kRenderLayerCount && unused; l++)
                unused = p.ib[l].Unused();
            if (!unused) {
                p.emptyFrames = 0;
                continue;
            }
            if (++p.emptyFrames < 600)
                continue;
            p.vb.Destroy(gpu->Dev());
            p.ext.Destroy(gpu->Dev());
            for (Arena& a : p.ib)
                a.Destroy(gpu->Dev());
            p.live = false;
            meshEpoch++;
        }
    }

    void ChunkMeshStore::Free(Chunk& c) {
        if (c.page >= 0 && c.page < static_cast<int>(pages.size()) &&
            pages[static_cast<size_t>(c.page)].live) {
            Page& pg = pages[static_cast<size_t>(c.page)];
            pg.vb.Free(c.vOff, c.vCount);
            for (size_t l = 0; l < kRenderLayerCount; l++)
                if (c.layerCount[l])
                    pg.ib[l].Free(c.iOff[l], c.layerCount[l]);
        }
        c.page = -1;
    }

    void ChunkMeshStore::Update(bool lod, int cx, int cz, int size, const Tessellator& tess,
                                uint64_t version) {
        meshEpoch++;
        Upload(lod ? lods : chunks, lod, cx, cz, size, tess, version);
    }

    void ChunkMeshStore::Remove(bool lod, int cx, int cz) {
        meshEpoch++;
        auto& target = lod ? lods : chunks;
        const auto it = target.find(Key(cx, cz));
        if (it == target.end())
            return;
        if (lod)
            LodDrop(it->second);
        Free(it->second);
        target.erase(it);
        if (!lod)
            generation++;
    }

    void ChunkMeshStore::Recalc(LodGroup& g) {
        bool first = true;
        for (const Chunk* m : g.members) {
            const float s = static_cast<float>(m->size);
            const float x0 = m->cx * s, z0 = m->cz * s;
            if (first) {
                g.x0 = x0;
                g.z0 = z0;
                g.x1 = x0 + s;
                g.z1 = z0 + s;
                g.minY = m->minY;
                g.maxY = m->maxY;
                first = false;
            } else {
                g.x0 = std::min(g.x0, x0);
                g.z0 = std::min(g.z0, z0);
                g.x1 = std::max(g.x1, x0 + s);
                g.z1 = std::max(g.z1, z0 + s);
                g.minY = std::min(g.minY, m->minY);
                g.maxY = std::max(g.maxY, m->maxY);
            }
        }
    }

    void ChunkMeshStore::LodAdd(const Chunk& chunk) {
        LodGroup& g = lodGroups[Key(chunk.cx >> 3, chunk.cz >> 3)];
        g.members.push_back(&chunk);
        Recalc(g);
    }

    void ChunkMeshStore::LodDrop(const Chunk& chunk) {
        const auto it = lodGroups.find(Key(chunk.cx >> 3, chunk.cz >> 3));
        if (it == lodGroups.end())
            return;
        auto& members = it->second.members;
        const auto at = std::find(members.begin(), members.end(), &chunk);
        if (at != members.end()) {
            *at = members.back();
            members.pop_back();
        }
        if (members.empty())
            lodGroups.erase(it);
        else
            Recalc(it->second);
    }

    void ChunkMeshStore::Upload(std::unordered_map<int64_t, Chunk>& target, bool lod, int cx, int cz,
                                int size, const Tessellator& tess, uint64_t version) {
        const int64_t key = Key(cx, cz);
        const auto& verts = tess.Vertices();
        auto it = target.find(key);
        if (verts.empty() || tess.Empty()) {
            if (it != target.end()) {
                if (lod)
                    LodDrop(it->second);
                Free(it->second);
                target.erase(it);
                if (!lod)
                    generation++;
            }
            return;
        }
        if (it != target.end() && it->second.version == version)
            return;
        if (it != target.end()) {
            if (lod)
                LodDrop(it->second);
            Free(it->second);
            target.erase(it);
        }

        Chunk g;
        g.vCount = static_cast<uint32_t>(verts.size());
        for (size_t l = 0; l < kRenderLayerCount; l++)
            g.layerCount[l] = static_cast<uint32_t>(tess.Indices(static_cast<RenderLayer>(l)).size());
        if (!Alloc(g.vCount, g.layerCount, g))
            return;
        Page& pg = pages[static_cast<size_t>(g.page)];

        gpu->WriteBuffer(pg.vb, static_cast<uint64_t>(g.vOff) * sizeof(Vertex), verts.data(),
                         static_cast<uint64_t>(g.vCount) * sizeof(Vertex));
        if (extBytes && extConvert && pg.ext.Buffer()) {
            static thread_local std::vector<uint8_t> extra;
            extra.clear();
            extConvert(tess, extra);
            if (extra.size() == static_cast<size_t>(g.vCount) * extBytes)
                gpu->WriteBuffer(pg.ext, static_cast<uint64_t>(g.vOff) * extBytes, extra.data(),
                                 extra.size());
        }
        static thread_local std::vector<uint32_t> shifted;
        for (size_t l = 0; l < kRenderLayerCount; l++) {
            const auto& list = tess.Indices(static_cast<RenderLayer>(l));
            if (list.empty())
                continue;
            shifted.resize(list.size());
            for (size_t i = 0; i < list.size(); i++)
                shifted[i] = list[i] + g.vOff;
            gpu->WriteBuffer(pg.ib[l], static_cast<uint64_t>(g.iOff[l]) * sizeof(uint32_t), shifted.data(),
                             shifted.size() * sizeof(uint32_t));
        }

        float lo = verts[0].y, hi = verts[0].y;
        for (const Vertex& v : verts) {
            lo = std::min(lo, v.y);
            hi = std::max(hi, v.y);
        }
        g.minY = lo;
        g.maxY = hi;

        const auto& secs = tess.Sections();
        if (secs.empty()) {
            Section whole{lo, hi, 0, {}, {}};
            for (size_t l = 0; l < kRenderLayerCount; l++) {
                whole.start[l] = 0;
                whole.count[l] = g.layerCount[l];
            }
            g.sections.push_back(whole);
        } else {
            g.sections.reserve(secs.size());
            for (const MeshSection& ms : secs) {
                Section section{
                    static_cast<float>(ms.sy * 16), static_cast<float>(ms.sy * 16 + 16), ms.sy, {}, {}};
                for (size_t l = 0; l < kRenderLayerCount; l++) {
                    section.start[l] = ms.start[l];
                    section.count[l] = ms.count[l];
                }
                g.sections.push_back(section);
            }
        }
        g.cx = cx;
        g.cz = cz;
        g.size = size;
        g.version = version;
        {
            const auto& vis = tess.Visibility();
            if (!vis.empty() && vis.size() <= 32) {
                g.visMin = vis.front().sy;
                for (const SectionVis& v : vis)
                    g.conn.push_back(v.conn);
            }
        }
        if (!lod)
            generation++;
        Chunk& stored = target[key] = std::move(g);
        if (lod)
            LodAdd(stored);
    }

    uint64_t ChunkMeshStore::VertexBytes() const {
        uint64_t total = 0;
        for (const Page& p : pages)
            if (p.live)
                total += static_cast<uint64_t>(p.vb.Capacity()) * sizeof(Vertex);
        return total;
    }

    uint64_t ChunkMeshStore::IndexBytes() const {
        uint64_t total = 0;
        for (const Page& p : pages)
            if (p.live)
                for (const Arena& a : p.ib)
                    total += static_cast<uint64_t>(a.Capacity()) * sizeof(uint32_t);
        return total;
    }

    bool ChunkMeshStore::SectionReached(int sx, int sy, int sz, bool caveCulling) const {
        if (!caveCulling)
            return true;
        const auto it = chunks.find(Key(sx, sz));
        if (it == chunks.end() || it->second.secVis == ~0u)
            return true;
        const int bit = sy - it->second.visMin;
        return bit < 0 || bit >= 32 || (it->second.secVis & (1u << bit)) != 0;
    }

    void ChunkMeshStore::Select(const Frustum& frustum, float camX, float camY, float camZ, int cullRadius,
                                bool caveCulling, double now) {
        visible.clear();
        visibleSections.clear();
        if (chunks.empty() && lods.empty())
            return;
        if (caveCulling)
            UpdateCave(camX, camY, camZ, now);
        const int cullCx = static_cast<int>(std::floor(camX / 16.0f)),
                  cullCz = static_cast<int>(std::floor(camZ / 16.0f));
        for (const auto& entry : chunks) {
            const Chunk& g = entry.second;
            if (g.page < 0 || g.sections.empty())
                continue;
            if (cullRadius > 0) {
                const int ddx = g.cx - cullCx, ddz = g.cz - cullCz;
                if (std::abs(ddx) > cullRadius || std::abs(ddz) > cullRadius ||
                    ddx * ddx + ddz * ddz > (cullRadius + 1) * (cullRadius + 1))
                    continue;
            }
            const float x0 = g.cx * 16.0f, z0 = g.cz * 16.0f;
            const auto result = frustum.Test(x0, g.minY, z0, x0 + 16.0f, g.maxY, z0 + 16.0f);
            if (result == Frustum::Result::Outside)
                continue;
            const float dx = x0 + 8.0f - camX, dz = z0 + 8.0f - camZ;
            visible.push_back({dx * dx + dz * dz, &g, result == Frustum::Result::Inside});
        }

        std::sort(visible.begin(), visible.end(),
                  [](const Visible& x, const Visible& y) { return x.distance < y.distance; });
        const size_t realCount = visible.size();
        for (const auto& [gkey, group] : lodGroups) {
            const auto groupResult =
                frustum.Test(group.x0, group.minY, group.z0, group.x1, group.maxY, group.z1);
            if (groupResult == Frustum::Result::Outside)
                continue;
            for (const Chunk* g : group.members) {
                if (g->page < 0 || g->sections.empty())
                    continue;
                const float s = static_cast<float>(g->size);
                const float x0 = g->cx * s, z0 = g->cz * s;
                Frustum::Result result = groupResult;
                if (result != Frustum::Result::Inside) {
                    result = frustum.Test(x0, g->minY, z0, x0 + s, g->maxY, z0 + s);
                    if (result == Frustum::Result::Outside)
                        continue;
                }
                const float dx = x0 + s * 0.5f - camX, dz = z0 + s * 0.5f - camZ;
                visible.push_back({dx * dx + dz * dz, g, result == Frustum::Result::Inside});
            }
        }
        std::sort(visible.begin() + static_cast<std::ptrdiff_t>(realCount), visible.end(),
                  [](const Visible& x, const Visible& y) {
                      if (x.chunk->page != y.chunk->page)
                          return x.chunk->page < y.chunk->page;
                      return x.chunk->iOff[0] < y.chunk->iOff[0];
                  });
        for (Visible& item : visible) {
            const Chunk& g = *item.chunk;
            const float x0 = g.cx * static_cast<float>(g.size), z0 = g.cz * static_cast<float>(g.size);
            const float extent = static_cast<float>(g.size);
            item.secBegin = static_cast<uint32_t>(visibleSections.size());
            for (const Section& sec : g.sections) {
                if (g.secVis != ~0u) {
                    const int bit = sec.sy - g.visMin;
                    if (bit >= 0 && bit < 32 && !(g.secVis & (1u << bit)))
                        continue;
                }
                if (!item.fullyInside &&
                    !frustum.Visible(x0, sec.minY, z0, x0 + extent, sec.maxY, z0 + extent))
                    continue;
                visibleSections.push_back(&sec);
            }
            item.secEnd = static_cast<uint32_t>(visibleSections.size());
        }
    }

    void ChunkMeshStore::BuildCullRecords(RenderLayer layer, std::vector<CullRecord>& records,
                                          std::vector<CullBucket>& buckets) const {
        records.clear();
        buckets.clear();
        const size_t slot = static_cast<size_t>(layer);
        std::vector<std::pair<uint64_t, CullRecord>> keyed;
        const auto add = [&](const Chunk& g, bool lod) {
            if (g.page < 0 || !g.layerCount[slot])
                return;
            const float s = static_cast<float>(g.size);
            const float x0 = g.cx * s, z0 = g.cz * s;
            for (const Section& sec : g.sections) {
                if (!sec.count[slot])
                    continue;
                CullRecord r{};
                r.minX = x0;
                r.minY = sec.minY;
                r.minZ = z0;
                r.maxX = x0 + s;
                r.maxY = sec.maxY;
                r.maxZ = z0 + s;
                r.first = g.iOff[slot] + sec.start[slot];
                r.count = sec.count[slot];
                r.chunkX = g.cx;
                r.chunkZ = g.cz;
                if (g.secVis != ~0u) {
                    const int bit = sec.sy - g.visMin;
                    if (bit >= 0 && bit < 32 && !(g.secVis & (1u << bit)))
                        r.flags |= 1u;
                }
                if (lod)
                    r.flags |= 2u;
                keyed.push_back({(static_cast<uint64_t>(static_cast<uint32_t>(g.page)) << 32) | r.first, r});
            }
        };
        for (const auto& entry : chunks)
            add(entry.second, false);
        for (const auto& entry : lods)
            add(entry.second, true);
        std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        records.reserve(keyed.size());
        for (const auto& [key, r] : keyed) {
            const uint32_t page = static_cast<uint32_t>(key >> 32);
            if (buckets.empty() || buckets.back().page != page)
                buckets.push_back({page, static_cast<uint32_t>(records.size()), 0});
            buckets.back().count++;
            records.push_back(r);
        }
    }

    void ChunkMeshStore::CollectRuns(RenderLayer layer, bool farToNear, std::vector<Run>& out) {
        out.clear();
        const size_t slot = static_cast<size_t>(layer);
        const auto push = [&](uint32_t page, uint32_t start, uint32_t count) {
            if (!out.empty() && out.back().page == page && out.back().first + out.back().count == start)
                out.back().count += count;
            else
                out.push_back({page, start, count});
        };
        const auto emit = [&](const Visible& item) {
            const Chunk& g = *item.chunk;
            if (!g.layerCount[slot])
                return;
            for (uint32_t i = item.secBegin; i < item.secEnd; i++) {
                const Section& sec = *visibleSections[i];
                if (sec.count[slot])
                    push(static_cast<uint32_t>(g.page), g.iOff[slot] + sec.start[slot], sec.count[slot]);
            }
        };
        if (farToNear) {
            for (auto it = visible.rbegin(); it != visible.rend(); ++it)
                emit(*it);
            return;
        }
        constexpr float kNearSq = 40.0f * 40.0f;
        size_t i = 0;
        for (; i < visible.size() && visible[i].distance <= kNearSq; i++)
            emit(visible[i]);
        const size_t nearRuns = out.size();
        for (; i < visible.size(); i++)
            emit(visible[i]);
        if (out.size() - nearRuns < 2)
            return;
        std::sort(out.begin() + static_cast<std::ptrdiff_t>(nearRuns), out.end(), [](const Run& a, const Run& b) {
            return a.page != b.page ? a.page < b.page : a.first < b.first;
        });
        size_t w = nearRuns;
        for (size_t r = nearRuns; r < out.size(); r++) {
            if (w > nearRuns && out[w - 1].page == out[r].page && out[w - 1].first + out[w - 1].count == out[r].first)
                out[w - 1].count += out[r].count;
            else
                out[w++] = out[r];
        }
        out.resize(w);
    }

    void ChunkMeshStore::UpdateCave(float camX, float camY, float camZ, double now) {
        const int ccx = static_cast<int>(std::floor(camX / 16.0f)),
                  ccz = static_cast<int>(std::floor(camZ / 16.0f));
        const int csy = static_cast<int>(std::floor(camY / 16.0f));
        const bool camMoved = caveCam[0] != ccx || caveCam[1] != csy || caveCam[2] != ccz;
        if (!camMoved) {
            if (caveGeneration == generation)
                return;
            if (now - caveLastRun < 0.15)
                return;
        }
        caveLastRun = now;
        caveEpoch++;

        int minX = INT_MAX, maxX = INT_MIN, minZ = INT_MAX, maxZ = INT_MIN, minS = INT_MAX, maxS = INT_MIN;
        bool allData = true;
        for (auto& entry : chunks) {
            Chunk& g = entry.second;
            g.secVis = ~0u;
            if (g.conn.empty()) {
                allData = false;
                continue;
            }
            minX = std::min(minX, g.cx);
            maxX = std::max(maxX, g.cx);
            minZ = std::min(minZ, g.cz);
            maxZ = std::max(maxZ, g.cz);
            minS = std::min(minS, g.visMin);
            maxS = std::max(maxS, g.visMin + static_cast<int>(g.conn.size()) - 1);
        }
        caveGeneration = generation;
        caveCam[0] = ccx;
        caveCam[1] = csy;
        caveCam[2] = ccz;
        if (!allData || minX > maxX || minS > maxS)
            return;
        minS = std::min(minS, csy);
        maxS = std::max(maxS, csy);
        if (ccx < minX || ccx > maxX || ccz < minZ || ccz > maxZ)
            return;
        const int W = maxX - minX + 1, D = maxZ - minZ + 1, H = maxS - minS + 1;
        if (H > 32)
            return;

        static std::vector<Chunk*> grid;
        static std::vector<uint8_t> entered;
        static std::vector<uint32_t> reached;
        grid.assign(static_cast<size_t>(W) * D, nullptr);
        entered.assign(static_cast<size_t>(W) * D * H, 0);
        reached.assign(static_cast<size_t>(W) * D, 0);
        for (auto& entry : chunks)
            grid[static_cast<size_t>(entry.second.cz - minZ) * W + (entry.second.cx - minX)] = &entry.second;

        static const int kDx[6] = {0, 0, 0, 0, -1, 1};
        static const int kDy[6] = {-1, 1, 0, 0, 0, 0};
        static const int kDz[6] = {0, 0, -1, 1, 0, 0};

        const auto connected = [&](int x, int z, int s, int a, int b) {
            if (a == b)
                return false;
            if (a > b)
                std::swap(a, b);
            const Chunk* g = grid[static_cast<size_t>(z) * W + x];
            if (!g)
                return true;
            const int i = (s + minS) - g->visMin;
            if (i < 0 || i >= static_cast<int>(g->conn.size()))
                return true;
            return ((g->conn[static_cast<size_t>(i)] >> (a * (11 - a) / 2 + (b - a - 1))) & 1) != 0;
        };

        const int sx = ccx - minX, sz = ccz - minZ, ss = csy - minS;
        reached[static_cast<size_t>(sz) * W + sx] |= 1u << ss;
        struct Move {
            int x, z, s;
            uint8_t dir, dirs;
        };
        static std::vector<Move> moves;
        moves.clear();
        for (int d = 0; d < 6; d++)
            moves.push_back({sx, sz, ss, static_cast<uint8_t>(d), static_cast<uint8_t>(1u << d)});
        size_t head = 0;
        while (head < moves.size()) {
            const Move m = moves[head++];
            const int nx = m.x + kDx[m.dir], nz = m.z + kDz[m.dir], ns = m.s + kDy[m.dir];
            if (nx < 0 || nx >= W || nz < 0 || nz >= D || ns < 0 || ns >= H)
                continue;
            const int entry = m.dir ^ 1;
            uint8_t& done = entered[(static_cast<size_t>(nz) * W + nx) * H + ns];
            if (done & (1u << entry))
                continue;
            done |= static_cast<uint8_t>(1u << entry);
            reached[static_cast<size_t>(nz) * W + nx] |= 1u << ns;
            for (int d = 0; d < 6; d++) {
                if (m.dirs & (1u << (d ^ 1)))
                    continue;
                if (!connected(nx, nz, ns, entry, d))
                    continue;
                moves.push_back(
                    {nx, nz, ns, static_cast<uint8_t>(d), static_cast<uint8_t>(m.dirs | (1u << d))});
            }
        }
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) {
                const int x = sx + dx, z = sz + dz;
                if (x < 0 || x >= W || z < 0 || z >= D)
                    continue;
                for (int ds = -1; ds <= 1; ds++)
                    if (ss + ds >= 0 && ss + ds < H)
                        reached[static_cast<size_t>(z) * W + x] |= 1u << (ss + ds);
            }
        for (auto& entry : chunks) {
            Chunk& g = entry.second;
            const uint32_t col = reached[static_cast<size_t>(g.cz - minZ) * W + (g.cx - minX)];
            const int shift = g.visMin - minS;
            g.secVis = shift >= 0 ? (col >> shift) : (col << -shift);
        }
    }
}
