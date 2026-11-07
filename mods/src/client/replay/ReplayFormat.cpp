#include "client/replay/ReplayFormat.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>

#include <miniz.h>

#include "util/Paths.hpp"

namespace perlin::replay {
    namespace fs = std::filesystem;

    namespace {
        enum : uint32_t {
            kLogin = 1,
            kPlayStatus = 2,
            kServerToClientHandshake = 3,
            kClientToServerHandshake = 4,
            kDisconnect = 5,
            kResourcePacksInfo = 6,
            kResourcePackStack = 7,
            kResourcePackClientResponse = 8,
            kSetTime = 10,
            kStartGame = 11,
            kAddPlayer = 12,
            kAddActor = 13,
            kRemoveActor = 14,
            kAddItemActor = 15,
            kMoveActorAbsolute = 18,
            kMovePlayer = 19,
            kUpdateBlock = 21,
            kMobEffect = 28,
            kUpdateAttributes = 29,
            kMobEquipment = 31,
            kMobArmorEquipment = 32,
            kPlayerHotbar = 48,
            kInventoryContent = 49,
            kInventorySlot = 50,
            kSetActorData = 39,
            kSetActorMotion = 40,
            kSetSpawnPosition = 43,
            kChangeDimension = 61,
            kSetDifficulty = 60,
            kSetPlayerGameType = 62,
            kPlayerList = 63,
            kLevelChunk = 58,
            kGameRulesChanged = 72,
            kResourcePackDataInfo = 82,
            kResourcePackChunkData = 83,
            kUpdateBlockSynced = 110,
            kMoveActorDelta = 111,
            kNetworkChunkPublisher = 121,
            kNetworkSettings = 143,
            kSubChunk = 174,
            kUpdateSubChunkBlocks = 172,
        };

        bool ReadVarU(const std::vector<uint8_t>& b, size_t& pos, uint64_t& out) {
            out = 0;
            for (int shift = 0; shift < 70; shift += 7) {
                if (pos >= b.size())
                    return false;
                const uint8_t c = b[pos++];
                out |= static_cast<uint64_t>(c & 0x7F) << shift;
                if (!(c & 0x80))
                    return true;
            }
            return false;
        }
        bool ReadZigZag(const std::vector<uint8_t>& b, size_t& pos, uint64_t& out) {
            uint64_t v;
            if (!ReadVarU(b, pos, v))
                return false;
            out = (v >> 1) ^ (~(v & 1) + 1);
            return true;
        }
        void PutU32(std::vector<uint8_t>& o, uint32_t v) {
            for (int i = 0; i < 4; i++)
                o.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }
        uint32_t GetU32(const uint8_t* p) {
            return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
                   static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
        }
        int64_t NowTicks() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }
        constexpr size_t kChunkByteCap = 384u << 20;
        constexpr size_t kListByteCap = 24u << 20;
        constexpr size_t kBlockUpdateCap = 120000;

        struct ZipHolder {
            mz_zip_archive za{};
            bool open = false;
            ~ZipHolder() {
                if (open)
                    mz_zip_reader_end(&za);
            }
        };
        bool ExtractAll(mz_zip_archive& za, const std::string& name, std::vector<uint8_t>& out) {
            const int idx = mz_zip_reader_locate_file(&za, name.c_str(), nullptr, 0);
            if (idx < 0)
                return false;
            size_t size = 0;
            void* mem = mz_zip_reader_extract_to_heap(&za, static_cast<mz_uint>(idx), &size, 0);
            if (!mem)
                return false;
            out.assign(static_cast<uint8_t*>(mem), static_cast<uint8_t*>(mem) + size);
            mz_free(mem);
            return true;
        }
        ui::Json PoseJson(const Pose& p) {
            ui::Json o = ui::Json::MakeObject();
            o.Set("x", ui::Json(static_cast<double>(p.x)));
            o.Set("y", ui::Json(static_cast<double>(p.y)));
            o.Set("z", ui::Json(static_cast<double>(p.z)));
            o.Set("yaw", ui::Json(static_cast<double>(p.yaw)));
            o.Set("pitch", ui::Json(static_cast<double>(p.pitch)));
            o.Set("known", ui::Json(p.known));
            return o;
        }
    }

    std::string ReplayDirectory() {
        return paths::DataDir() + "/replays";
    }

    std::string SafeFileName(const std::string& name) {
        std::string out;
        for (unsigned char c : name) {
            if (c < 32 || std::strchr("<>:\"/\\|?*", c))
                out += '_';
            else
                out += static_cast<char>(c);
        }
        while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
            out.pop_back();
        if (out.empty())
            out = "replay";
        if (out.size() > 80)
            out.resize(80);
        return out;
    }

    ui::Json Metadata::ToJson() const {
        ui::Json j = ui::Json::MakeObject();
        j.Set("format", ui::Json("perlinreplay"));
        j.Set("version", ui::Json(version));
        j.Set("name", ui::Json(name));
        j.Set("server", ui::Json(server));
        j.Set("gameVersion", ui::Json(gameVersion));
        j.Set("playerName", ui::Json(playerName));
        j.Set("perlinVersion", ui::Json(perlinVersion));
        j.Set("protocol", ui::Json(protocol));
        j.Set("createdUnix", ui::Json(static_cast<double>(createdUnix)));
        j.Set("durationMs", ui::Json(static_cast<double>(durationMs)));
        j.Set("localRuntimeId", ui::Json(static_cast<double>(localRuntimeId)));
        j.Set("startPose", PoseJson(startPose));
        ui::Json cs = ui::Json::MakeArray();
        for (const ChunkInfo& c : chunks) {
            ui::Json o = ui::Json::MakeObject();
            o.Set("file", ui::Json(c.file));
            o.Set("start", ui::Json(static_cast<double>(c.startMs)));
            o.Set("end", ui::Json(static_cast<double>(c.endMs)));
            o.Set("records", ui::Json(static_cast<double>(c.records)));
            if (c.snapshot)
                o.Set("snapshot", ui::Json(true));
            cs.Push(std::move(o));
        }
        j.Set("chunks", std::move(cs));
        ui::Json ms = ui::Json::MakeArray();
        for (const Marker& m : markers) {
            ui::Json o = ui::Json::MakeObject();
            o.Set("time", ui::Json(static_cast<double>(m.timeMs)));
            o.Set("name", ui::Json(m.name));
            o.Set("color", ui::Json(static_cast<double>(m.color)));
            ms.Push(std::move(o));
        }
        j.Set("markers", std::move(ms));
        return j;
    }

    bool Metadata::FromJson(const ui::Json& j, Metadata& out, std::string& error) {
        if (!j.IsObject() || !j.Find("format") || j.Find("format")->StringOr("") != "perlinreplay") {
            error = "not a Perlin replay";
            return false;
        }
        Metadata m;
        auto str = [&](const char* k) {
            const ui::Json* v = j.Find(k);
            return v ? v->StringOr("") : std::string();
        };
        auto num = [&](const char* k) {
            const ui::Json* v = j.Find(k);
            return v ? v->NumberOr(0) : 0.0;
        };
        m.version = static_cast<int>(num("version"));
        if (m.version < 1 || m.version > kFormatVersion) {
            error = "this replay was made by a newer version of Perlin (format " + std::to_string(m.version) +
                    ")";
            return false;
        }
        m.name = str("name");
        m.server = str("server");
        m.gameVersion = str("gameVersion");
        m.playerName = str("playerName");
        m.perlinVersion = str("perlinVersion");
        m.protocol = static_cast<int>(num("protocol"));
        m.createdUnix = static_cast<int64_t>(num("createdUnix"));
        m.durationMs = static_cast<uint32_t>(num("durationMs"));
        m.localRuntimeId = static_cast<uint64_t>(num("localRuntimeId"));
        if (const ui::Json* p = j.Find("startPose")) {
            auto pn = [&](const char* k) {
                const ui::Json* v = p->Find(k);
                return v ? static_cast<float>(v->NumberOr(0)) : 0.0f;
            };
            m.startPose = {pn("x"),   pn("y"),     pn("z"),
                           pn("yaw"), pn("pitch"), p->Find("known") && p->Find("known")->BoolOr(false)};
        }
        if (const ui::Json* cs = j.Find("chunks"); cs && cs->IsArray())
            for (const ui::Json& c : cs->array) {
                ChunkInfo ci;
                ci.file = c.Find("file") ? c.Find("file")->StringOr("") : "";
                ci.startMs = c.Find("start") ? static_cast<uint32_t>(c.Find("start")->NumberOr(0)) : 0;
                ci.endMs = c.Find("end") ? static_cast<uint32_t>(c.Find("end")->NumberOr(0)) : 0;
                ci.records = c.Find("records") ? static_cast<uint32_t>(c.Find("records")->NumberOr(0)) : 0;
                ci.snapshot = c.Find("snapshot") && c.Find("snapshot")->BoolOr(false);
                m.chunks.push_back(std::move(ci));
            }
        if (const ui::Json* ms = j.Find("markers"); ms && ms->IsArray())
            for (const ui::Json& c : ms->array) {
                Marker mk;
                mk.timeMs = c.Find("time") ? static_cast<uint32_t>(c.Find("time")->NumberOr(0)) : 0;
                mk.name = c.Find("name") ? c.Find("name")->StringOr("") : "";
                mk.color =
                    c.Find("color") ? static_cast<uint32_t>(c.Find("color")->NumberOr(0xFFFFFF)) : 0xFFFFFF;
                m.markers.push_back(std::move(mk));
            }
        out = std::move(m);
        return true;
    }

    void EncodeRecord(const Record& r, std::vector<uint8_t>& out) {
        out.push_back(static_cast<uint8_t>(r.dir));
        PutU32(out, r.timeMs);
        PutU32(out, r.id);
        PutU32(out, static_cast<uint32_t>(r.body.size()));
        out.insert(out.end(), r.body.begin(), r.body.end());
    }

    size_t DecodeRecord(const uint8_t* data, size_t size, Record& out) {
        if (size < 13)
            return 0;
        const uint32_t len = GetU32(data + 9);
        if (size < 13 + static_cast<size_t>(len))
            return 0;
        out.dir = data[0] == 2 ? Dir::Snapshot : (data[0] ? Dir::ToServer : Dir::ToClient);
        out.timeMs = GetU32(data + 1);
        out.id = GetU32(data + 5);
        out.body.assign(data + 13, data + 13 + len);
        return 13 + static_cast<size_t>(len);
    }

    bool IsHandshakePacket(uint32_t id) {
        switch (id) {
        case kLogin:
        case kServerToClientHandshake:
        case kClientToServerHandshake:
        case kDisconnect:
        case kResourcePacksInfo:
        case kResourcePackStack:
        case kResourcePackClientResponse:
        case kResourcePackDataInfo:
        case kResourcePackChunkData:
        case kNetworkSettings:
            return true;
        default:
            return false;
        }
    }

    void StateCache::Reset() {
        std::lock_guard<std::mutex> lk(mutex);
        seq = 0;
        joined = false;
        chunkBytes = joinBytes = listBytes = 0;
        join.clear();
        chunks.clear();
        blockUpdates.clear();
        playerLists.clear();
        entities.clear();
        latest.clear();
        singletons.clear();
    }

    void StateCache::DropEntity(uint64_t id) {
        entities.erase(id);
        for (auto it = latest.begin(); it != latest.end();) {
            if (it->second.runtime == id && it->second.unique == 1)
                it = latest.erase(it);
            else
                ++it;
        }
    }

    size_t StateCache::Bytes() const {
        std::lock_guard<std::mutex> lk(mutex);
        return chunkBytes + joinBytes + listBytes;
    }

    void StateCache::OnToClient(uint32_t id, const std::vector<uint8_t>& body) {
        if (IsHandshakePacket(id))
            return;
        std::lock_guard<std::mutex> lk(mutex);
        Entry e;
        e.seq = ++seq;
        e.id = id;
        e.body = body;
        if (!joined) {
            joinBytes += body.size();
            join.push_back(std::move(e));
            if (id == kPlayStatus && body.size() >= 4 && body[3] == 3)
                joined = true;
            else if (id == kLevelChunk || id == kSubChunk || id == kNetworkChunkPublisher)
                joined = true;
            return;
        }
        size_t pos = 0;
        switch (id) {
        case kChangeDimension:
            chunks.clear();
            chunkBytes = 0;
            blockUpdates.clear();
            entities.clear();
            latest.clear();
            for (size_t i = 0; i < join.size();) {
                if (join[i].id == kLevelChunk || join[i].id == kSubChunk) {
                    joinBytes -= join[i].body.size();
                    join.erase(join.begin() + static_cast<long>(i));
                } else
                    i++;
            }
            singletons[id] = std::move(e);
            return;
        case kLevelChunk: {
            uint64_t x = 0, z = 0, dim = 0;
            if (!ReadZigZag(body, pos, x) || !ReadZigZag(body, pos, z) || !ReadVarU(body, pos, dim))
                return;
            const uint64_t key = (static_cast<uint64_t>(dim & 0xFF) << 56) ^ (x * 0x9E3779B97F4A7C15ull) ^
                                 (z * 0xC2B2AE3D27D4EB4Full);
            auto it = chunks.find(key);
            if (it != chunks.end())
                chunkBytes -= it->second.body.size();
            chunkBytes += body.size();
            chunks[key] = std::move(e);
            break;
        }
        case kSubChunk: {
            const uint64_t key = std::hash<std::string_view>{}(std::string_view(
                                     reinterpret_cast<const char*>(body.data()), body.size())) |
                                 1ull;
            auto it = chunks.find(key);
            if (it != chunks.end())
                chunkBytes -= it->second.body.size();
            chunkBytes += body.size();
            chunks[key] = std::move(e);
            break;
        }
        case kUpdateBlock:
        case kUpdateBlockSynced:
        case kUpdateSubChunkBlocks:
            blockUpdates.push_back(std::move(e));
            if (blockUpdates.size() > kBlockUpdateCap)
                blockUpdates.erase(blockUpdates.begin(),
                                   blockUpdates.begin() +
                                       static_cast<long>(blockUpdates.size() - kBlockUpdateCap));
            return;
        case kPlayerList:
            listBytes += body.size();
            playerLists.push_back(std::move(e));
            while (listBytes > kListByteCap && playerLists.size() > 1) {
                listBytes -= playerLists.front().body.size();
                playerLists.erase(playerLists.begin());
            }
            return;
        case kAddActor:
        case kAddItemActor: {
            uint64_t unique = 0, runtime = 0;
            if (!ReadZigZag(body, pos, unique) || !ReadVarU(body, pos, runtime))
                return;
            e.unique = unique;
            e.runtime = runtime;
            entities[runtime] = std::move(e);
            return;
        }
        case kAddPlayer: {
            pos = 16;
            uint64_t nameLen = 0, runtime = 0;
            if (body.size() < 17 || !ReadVarU(body, pos, nameLen))
                return;
            pos += static_cast<size_t>(nameLen);
            if (!ReadVarU(body, pos, runtime))
                return;
            e.unique = runtime;
            e.runtime = runtime;
            entities[runtime] = std::move(e);
            return;
        }
        case kRemoveActor: {
            uint64_t unique = 0;
            if (!ReadZigZag(body, pos, unique))
                return;
            std::vector<uint64_t> doomed;
            for (const auto& [rt, en] : entities)
                if (en.unique == unique || rt == unique)
                    doomed.push_back(rt);
            for (uint64_t rt : doomed)
                DropEntity(rt);
            return;
        }
        case kSetActorData:
        case kMobEffect:
        case kUpdateAttributes:
        case kMobEquipment:
        case kMobArmorEquipment:
        case kMoveActorAbsolute:
        case kMoveActorDelta:
        case kMovePlayer:
        case kSetActorMotion: {
            uint64_t runtime = 0;
            if (!ReadVarU(body, pos, runtime))
                return;
            e.runtime = runtime;
            e.unique = 1;
            uint64_t sub = 0;
            if (id == kMobEffect && pos + 1 < body.size())
                sub = body[pos + 1];
            latest[{id, runtime * 131 + sub}] = std::move(e);
            return;
        }
        case kPlayerHotbar:
        case kInventoryContent:
        case kInventorySlot: {
            uint64_t window = 0;
            if (!ReadVarU(body, pos, window))
                return;
            uint64_t slot = 0;
            if (id == kInventorySlot)
                ReadVarU(body, pos, slot);
            e.unique = 0;
            latest[{id, window * 4096 + slot}] = std::move(e);
            return;
        }
        case kSetTime:
        case kSetSpawnPosition:
        case kSetDifficulty:
        case kSetPlayerGameType:
        case kGameRulesChanged:
        case kNetworkChunkPublisher:
        case kPlayStatus:
            singletons[id] = std::move(e);
            return;
        default:
            return;
        }
        while (chunkBytes > kChunkByteCap && !chunks.empty()) {
            auto oldest = chunks.begin();
            for (auto it = chunks.begin(); it != chunks.end(); ++it)
                if (it->second.seq < oldest->second.seq)
                    oldest = it;
            chunkBytes -= oldest->second.body.size();
            chunks.erase(oldest);
        }
    }

    std::vector<Record> StateCache::Snapshot() const {
        std::lock_guard<std::mutex> lk(mutex);
        std::vector<const Entry*> ordered;
        auto add = [&](const Entry& e) { ordered.push_back(&e); };
        for (const Entry& e : join)
            add(e);
        if (auto it = singletons.find(kChangeDimension); it != singletons.end())
            add(it->second);
        for (const auto& [id, e] : singletons)
            if (id != kChangeDimension)
                add(e);
        for (const Entry& e : playerLists)
            add(e);
        std::vector<const Entry*> chunkOrder;
        chunkOrder.reserve(chunks.size());
        for (const auto& [k, e] : chunks)
            chunkOrder.push_back(&e);
        std::sort(chunkOrder.begin(), chunkOrder.end(),
                  [](const Entry* a, const Entry* b) { return a->seq < b->seq; });
        for (const Entry* e : chunkOrder)
            add(*e);
        for (const Entry& e : blockUpdates)
            add(e);
        std::vector<const Entry*> ents;
        ents.reserve(entities.size());
        for (const auto& [k, e] : entities)
            ents.push_back(&e);
        std::sort(ents.begin(), ents.end(), [](const Entry* a, const Entry* b) { return a->seq < b->seq; });
        for (const Entry* e : ents)
            add(*e);
        std::vector<const Entry*> lat;
        lat.reserve(latest.size());
        for (const auto& [k, e] : latest)
            lat.push_back(&e);
        std::sort(lat.begin(), lat.end(), [](const Entry* a, const Entry* b) { return a->seq < b->seq; });
        for (const Entry* e : lat)
            add(*e);
        std::vector<Record> out;
        out.reserve(ordered.size());
        for (const Entry* e : ordered) {
            Record r;
            r.dir = Dir::ToClient;
            r.timeMs = 0;
            r.id = e->id;
            r.body = e->body;
            out.push_back(std::move(r));
        }
        return out;
    }

    Recorder::~Recorder() {
        if (active)
            Abort();
    }

    uint32_t Recorder::ElapsedMs() const {
        std::lock_guard<std::mutex> lk(mutex);
        return active ? static_cast<uint32_t>(NowTicks() - startTick) : meta.durationMs;
    }

    bool Recorder::Start(const std::string& workDir, Metadata m, const std::vector<Record>& snapshot,
                         std::string& error, SnapshotProvider prov, uint32_t period, size_t maxBytes) {
        std::lock_guard<std::mutex> lk(mutex);
        if (active)
            return false;
        std::error_code ec;
        fs::remove_all(workDir, ec);
        fs::create_directories(workDir, ec);
        if (ec) {
            error = "cannot create " + workDir + ": " + ec.message();
            return false;
        }
        dir = workDir;
        meta = std::move(m);
        meta.chunks.clear();
        meta.markers.clear();
        chunkInfos.clear();
        buffer.clear();
        bufferRecords = 0;
        bufferStartMs = lastMs = 0;
        bytesTotal = 0;
        provider = std::move(prov);
        periodicMs = period;
        maxSnapshotBytes = maxBytes;
        nextSnapshotMs = period;
        nextChunkSnapshot = false;
        startTick = NowTicks();
        {
            std::ofstream pm(dir + "/pending.json", std::ios::binary);
            const std::string text = meta.ToJson().Dump(2);
            pm.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        for (const Record& r : snapshot) {
            EncodeRecord(r, buffer);
            bufferRecords++;
        }
        FlushLocked();
        active = true;
        return true;
    }

    void Recorder::FlushLocked() {
        if (buffer.empty() && bufferRecords == 0)
            return;
        ChunkInfo ci;
        ci.file = "chunk_" + std::to_string(chunkInfos.size()) + ".bin";
        ci.startMs = bufferStartMs;
        ci.endMs = lastMs;
        ci.records = bufferRecords;
        ci.snapshot = nextChunkSnapshot;
        nextChunkSnapshot = false;
        std::ofstream out(dir + "/" + ci.file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        bytesTotal += buffer.size();
        chunkInfos.push_back(std::move(ci));
        buffer.clear();
        bufferRecords = 0;
        bufferStartMs = lastMs;
    }

    void Recorder::Push(Dir d, uint32_t id, const std::vector<uint8_t>& body) {
        if (d == Dir::ToClient && IsHandshakePacket(id))
            return;
        std::lock_guard<std::mutex> lk(mutex);
        if (!active)
            return;
        Record r;
        r.dir = d;
        r.timeMs = static_cast<uint32_t>(NowTicks() - startTick);
        r.id = id;
        r.body = body;
        lastMs = std::max(lastMs, r.timeMs);
        if (provider && periodicMs && r.timeMs >= nextSnapshotMs) {
            nextSnapshotMs = r.timeMs + periodicMs;
            std::vector<Record> snap = provider();
            size_t bytes = 0;
            for (const Record& s : snap)
                bytes += s.body.size();
            if (!snap.empty() && (!maxSnapshotBytes || bytes <= maxSnapshotBytes)) {
                FlushLocked();
                bufferStartMs = r.timeMs;
                for (Record& s : snap) {
                    s.dir = Dir::Snapshot;
                    s.timeMs = r.timeMs;
                    EncodeRecord(s, buffer);
                    bufferRecords++;
                }
                nextChunkSnapshot = true;
                FlushLocked();
            }
        }
        if (bufferRecords == 0)
            bufferStartMs = r.timeMs;
        EncodeRecord(r, buffer);
        bufferRecords++;
        if (buffer.size() > (4u << 20) || r.timeMs - bufferStartMs > 15000)
            FlushLocked();
    }

    void Recorder::AddMarker(const std::string& name) {
        std::lock_guard<std::mutex> lk(mutex);
        if (!active)
            return;
        Marker m;
        m.timeMs = static_cast<uint32_t>(NowTicks() - startTick);
        m.name = name;
        meta.markers.push_back(std::move(m));
    }

    void Recorder::Abort() {
        std::lock_guard<std::mutex> lk(mutex);
        active = false;
        buffer.clear();
        std::error_code ec;
        if (!dir.empty())
            fs::remove_all(dir, ec);
    }

    bool PackArchive(const std::string& dir, const Metadata& meta, const std::vector<uint8_t>& thumbnail,
                     const std::string& outPath, std::string& error) {
        const std::string tmp = outPath + ".part";
        std::error_code ec;
        fs::create_directories(fs::path(outPath).parent_path(), ec);
        mz_zip_archive za{};
        if (!mz_zip_writer_init_file(&za, tmp.c_str(), 0)) {
            error = "cannot write " + tmp;
            return false;
        }
        bool ok = true;
        const std::string metaText = meta.ToJson().Dump(2);
        ok = ok &&
             mz_zip_writer_add_mem(&za, "metadata.json", metaText.data(), metaText.size(), MZ_BEST_SPEED);
        for (const ChunkInfo& c : meta.chunks)
            ok = ok && mz_zip_writer_add_file(&za, c.file.c_str(), (dir + "/" + c.file).c_str(), nullptr, 0,
                                              MZ_DEFAULT_LEVEL);
        if (!thumbnail.empty())
            ok = ok && mz_zip_writer_add_mem(&za, "thumbnail.png", thumbnail.data(), thumbnail.size(),
                                             MZ_NO_COMPRESSION);
        ok = ok && mz_zip_writer_finalize_archive(&za);
        mz_zip_writer_end(&za);
        if (!ok) {
            fs::remove(tmp, ec);
            error = "failed to write the replay archive";
            return false;
        }
        fs::remove(outPath, ec);
        fs::rename(tmp, outPath, ec);
        if (ec) {
            error = "cannot move replay into place: " + ec.message();
            return false;
        }
        return true;
    }

    bool Recorder::Stop(const std::string& outPath, const std::vector<uint8_t>& thumbnail,
                        std::string& error) {
        std::lock_guard<std::mutex> lk(mutex);
        if (!active) {
            error = "not recording";
            return false;
        }
        active = false;
        lastMs = std::max(lastMs, static_cast<uint32_t>(NowTicks() - startTick));
        FlushLocked();
        meta.chunks = chunkInfos;
        meta.durationMs = lastMs;
        std::string packError;
        if (!PackArchive(dir, meta, thumbnail, outPath, packError)) {
            error = packError;
            return false;
        }
        std::error_code ec;
        fs::remove_all(dir, ec);
        return true;
    }

    bool Reader::ReadMetadata(const std::string& path, Metadata& out, std::vector<uint8_t>* thumbnail,
                              std::string& error) {
        ZipHolder zh;
        if (!mz_zip_reader_init_file(&zh.za, path.c_str(), 0)) {
            error = "not a valid replay file";
            return false;
        }
        zh.open = true;
        std::vector<uint8_t> text;
        if (!ExtractAll(zh.za, "metadata.json", text)) {
            error = "metadata.json missing";
            return false;
        }
        ui::Json j;
        if (!ui::Json::Parse(std::string_view(reinterpret_cast<const char*>(text.data()), text.size()), j,
                             error))
            return false;
        if (!Metadata::FromJson(j, out, error))
            return false;
        if (thumbnail && !ExtractAll(zh.za, "thumbnail.png", *thumbnail))
            thumbnail->clear();
        return true;
    }

    bool Reader::Open(const std::string& p, std::string& error) {
        path = p;
        auto holder = std::make_shared<ZipHolder>();
        if (!mz_zip_reader_init_file(&holder->za, p.c_str(), 0)) {
            error = "not a valid replay file";
            return false;
        }
        holder->open = true;
        std::vector<uint8_t> text;
        if (!ExtractAll(holder->za, "metadata.json", text)) {
            error = "metadata.json missing";
            return false;
        }
        ui::Json j;
        if (!ui::Json::Parse(std::string_view(reinterpret_cast<const char*>(text.data()), text.size()), j,
                             error))
            return false;
        if (!Metadata::FromJson(j, meta, error))
            return false;
        zip = holder;
        return true;
    }

    bool Reader::LoadChunk(size_t index, std::vector<Record>& out, std::string& error) {
        out.clear();
        if (index >= meta.chunks.size() || !zip) {
            error = "chunk out of range";
            return false;
        }
        auto* holder = static_cast<ZipHolder*>(zip.get());
        std::vector<uint8_t> raw;
        if (!ExtractAll(holder->za, meta.chunks[index].file, raw)) {
            error = "missing " + meta.chunks[index].file;
            return false;
        }
        size_t pos = 0;
        while (pos < raw.size()) {
            Record r;
            const size_t n = DecodeRecord(raw.data() + pos, raw.size() - pos, r);
            if (!n) {
                error = "truncated chunk " + meta.chunks[index].file;
                return !out.empty();
            }
            pos += n;
            out.push_back(std::move(r));
        }
        return true;
    }

    std::string Reader::ReadFile(const std::string& name) {
        if (!zip)
            return {};
        auto* holder = static_cast<ZipHolder*>(zip.get());
        std::vector<uint8_t> data;
        if (!ExtractAll(holder->za, name, data))
            return {};
        return std::string(data.begin(), data.end());
    }

    bool UpdateArchiveFile(const std::string& path, const std::string& name, const std::string& content,
                           std::string& error) {
        ZipHolder src;
        if (!mz_zip_reader_init_file(&src.za, path.c_str(), 0)) {
            error = "cannot open " + path;
            return false;
        }
        src.open = true;
        const std::string tmp = path + ".tmp";
        mz_zip_archive dst{};
        if (!mz_zip_writer_init_file(&dst, tmp.c_str(), 0)) {
            error = "cannot write " + tmp;
            return false;
        }
        bool ok = true;
        const mz_uint count = mz_zip_reader_get_num_files(&src.za);
        for (mz_uint i = 0; i < count && ok; i++) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&src.za, i, &st))
                continue;
            if (name == st.m_filename)
                continue;
            ok = mz_zip_writer_add_from_zip_reader(&dst, &src.za, i);
        }
        if (ok && !content.empty())
            ok = mz_zip_writer_add_mem(&dst, name.c_str(), content.data(), content.size(), MZ_DEFAULT_LEVEL);
        ok = ok && mz_zip_writer_finalize_archive(&dst);
        mz_zip_writer_end(&dst);
        if (!ok) {
            std::error_code ec;
            fs::remove(tmp, ec);
            error = "failed to rewrite the archive";
            return false;
        }
        src.open = false;
        mz_zip_reader_end(&src.za);
        std::error_code ec;
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
        return true;
    }

    bool AddArchiveFiles(const std::string& path, const std::vector<ArchiveAdd>& files, std::string& error) {
        ZipHolder src;
        if (!mz_zip_reader_init_file(&src.za, path.c_str(), 0)) {
            error = "cannot open " + path;
            return false;
        }
        src.open = true;
        const std::string tmp = path + ".tmp";
        mz_zip_archive dst{};
        if (!mz_zip_writer_init_file(&dst, tmp.c_str(), 0)) {
            error = "cannot write " + tmp;
            return false;
        }
        bool ok = true;
        const mz_uint count = mz_zip_reader_get_num_files(&src.za);
        for (mz_uint i = 0; i < count && ok; i++) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&src.za, i, &st))
                continue;
            bool replaced = false;
            for (const ArchiveAdd& f : files)
                replaced = replaced || f.name == st.m_filename;
            if (!replaced)
                ok = mz_zip_writer_add_from_zip_reader(&dst, &src.za, i);
        }
        for (const ArchiveAdd& f : files) {
            if (!ok)
                break;
            if (!f.diskPath.empty())
                ok = mz_zip_writer_add_file(&dst, f.name.c_str(), f.diskPath.c_str(), nullptr, 0,
                                            MZ_NO_COMPRESSION);
            else
                ok = mz_zip_writer_add_mem(&dst, f.name.c_str(), f.content.data(), f.content.size(),
                                           MZ_DEFAULT_LEVEL);
        }
        ok = ok && mz_zip_writer_finalize_archive(&dst);
        mz_zip_writer_end(&dst);
        std::error_code ec;
        if (!ok) {
            fs::remove(tmp, ec);
            error = "failed to rewrite the archive";
            return false;
        }
        src.open = false;
        mz_zip_reader_end(&src.za);
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
        return true;
    }
}
