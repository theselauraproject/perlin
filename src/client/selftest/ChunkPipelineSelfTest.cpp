#include "client/selftest/Check.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/multiplayer/CustomBlocks.hpp"
#include "util/VarInt.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "world/level/chunk/ChunkCodec.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace perlin {
    struct ChunkPipelineTestAccess {
        static void StopWorker(BedrockSession& session) {
            session.StopChunkWorker();
            session.decodePool.reset();
        }
        static void PreparationWorkers(BedrockSession& session) {
            session.decodePool = std::make_unique<ThreadPool>(2, false, "chunk-test");
        }
        static void StartWorker(BedrockSession& session) { session.StartChunkWorker(); }
        static void Reset(BedrockSession& session) { session.ResetWorldForDimension(); }
        static net::PacketDispatcher::Result Dispatch(BedrockSession& session, uint32_t id,
                                                       const std::vector<uint8_t>& bytes) {
            return session.dispatcher.Dispatch(id, bytes.data(), bytes.size());
        }
        static size_t PendingBlobs(BedrockSession& session) {
            std::lock_guard lock(session.blobMutex);
            return session.blobWaits.size();
        }
        static void ExpireBlobs(BedrockSession& session) {
            {
                std::lock_guard lock(session.blobMutex);
                for (auto& [id, wait] : session.blobWaits) {
                    (void)id;
                    wait.asks = 2;
                    wait.askedAt = std::chrono::steady_clock::now() - std::chrono::seconds(4);
                }
            }
            session.RetryBlobWaits();
        }
        static std::pair<size_t, size_t> Changes(BedrockSession& session, int x, int z) {
            std::lock_guard lock(session.chunkCommitMutex);
            const auto found = session.chunkChanges.find({x, z});
            if (found == session.chunkChanges.end())
                return {};
            const auto changes = found->second.lock();
            if (!changes)
                return {};
            size_t cells = 0, entities = 0;
            for (const auto& [y, section] : changes->sections) {
                (void)y;
                cells += section.cells.size();
                entities += section.blockEntities.size();
            }
            return {cells, entities};
        }
        static size_t ChangeColumns(BedrockSession& session) {
            std::lock_guard lock(session.chunkCommitMutex);
            return session.chunkChanges.size();
        }
        static bool WaitForReaderBarrier(BedrockSession& session) {
            std::unique_lock lock(session.chunkMutex);
            return session.chunkCompletionCv.wait_for(lock, std::chrono::seconds(5),
                                                       [&] { return session.chunkBarrierWaiters != 0; });
        }
        static proto::ContainerSlotType ChestSlotType(BedrockSession& session) {
            std::lock_guard lock(session.statusMutex);
            return session.chestSlotType;
        }
        static bool InventoryOpen(BedrockSession& session) {
            std::lock_guard lock(session.statusMutex);
            return session.inventoryOpen;
        }
    };
}

namespace {
    using namespace perlin;

    std::vector<uint8_t> Subchunk(int y, int32_t block) {
        ChunkCodec::CellIds cells;
        cells.fill(block);
        std::vector<uint8_t> bytes;
        ChunkCodec::WriteSubChunk(bytes, y, cells);
        return bytes;
    }

    void Sign(std::vector<uint8_t>& bytes, int x, int y, int z, const std::string& text) {
        bytes.push_back(10);
        wire::WriteString(bytes, "");
        for (const auto& [name, value] : {std::pair{"x", x}, std::pair{"y", y}, std::pair{"z", z}}) {
            bytes.push_back(3);
            wire::WriteString(bytes, name);
            wire::WriteZigzag32(bytes, value);
        }
        bytes.push_back(8);
        wire::WriteString(bytes, "id");
        wire::WriteString(bytes, "Sign");
        bytes.push_back(8);
        wire::WriteString(bytes, "Text");
        wire::WriteString(bytes, text);
        bytes.push_back(0);
    }

    proto::LevelChunkPacket Column(int x, int z, const std::vector<uint8_t>& payload = {}, unsigned count = 0) {
        proto::LevelChunkPacket packet;
        packet.x = x;
        packet.z = z;
        packet.subChunkCount = count;
        packet.payload = payload;
        return packet;
    }

    proto::SubChunkEntryEntry Entry(int yOffset, const std::vector<uint8_t>& payload, bool air = false) {
        proto::SubChunkEntryEntry entry;
        entry.dy = static_cast<int8_t>(yOffset);
        entry.result = air ? proto::SubChunkEntryEntryResult::SuccessAllAir
                           : proto::SubChunkEntryEntryResult::Success;
        entry.payload = payload;
        return entry;
    }

    proto::SubchunkPacket Reply(int x, int y, int z, const std::vector<uint8_t>& payload) {
        proto::SubchunkPacket packet;
        packet.origin = {x, y, z};
        packet.entries.push_back(Entry(0, payload));
        return packet;
    }

    proto::UpdateBlockPacket BlockUpdate(int x, int y, int z, uint32_t id, uint32_t layer = 0) {
        proto::UpdateBlockPacket packet;
        packet.position = {x, y, z};
        packet.blockRuntimeId = id;
        packet.layer = layer;
        return packet;
    }

    proto::BlockEntityDataPacket SignUpdate(int x, int y, int z, const std::string& text) {
        proto::BlockEntityDataPacket packet;
        packet.position = {x, y, z};
        Sign(packet.nbt.bytes, x, y, z, text);
        return packet;
    }

    struct Rig {
        selftest::Checker& check;
        BedrockSession session;
        int protocol;

        Rig(selftest::Checker& check, int protocol) : check(check), protocol(protocol) {
            ChunkPipelineTestAccess::StopWorker(session);
            ChunkPipelineTestAccess::PreparationWorkers(session);
            session.BeginReplayMode("chunk-pipeline", 7, protocol);
        }

        template <class Packet> bool Send(const Packet& packet, bool queued = false, bool recordChecks = true) {
            const net::context::ScopedSettings codec({protocol, -1});
            std::vector<uint8_t> bytes;
            const bool encoded = net::EncodeChecked(packet, bytes);
            if (recordChecks)
                check.Check(encoded, "chunk fixture encodes for the selected wire profile");
            if (!encoded)
                return false;
            if (queued) {
                session.InjectIncoming(Packet::kId, bytes);
                return true;
            }
            const auto result = ChunkPipelineTestAccess::Dispatch(session, Packet::kId, bytes);
            if (recordChecks)
                check.Check(result == net::PacketDispatcher::Result::Handled,
                            "real chunk packet dispatch accepts the fixture");
            return result == net::PacketDispatcher::Result::Handled;
        }

        void Blob(uint64_t hash, const std::vector<uint8_t>& payload) {
            proto::ClientCacheMissResponsePacket packet;
            packet.blobs.push_back({hash, payload});
            Send(packet);
        }

        uint16_t At(int x, int y, int z) { return session.NetLevel().GetBlock(x * 16, y * 16, z * 16); }
        static uint16_t Local(int32_t id) { return id ? BlockPalette::Instance().InternRuntime(id) : 0; }

        bool Text(int x, int y, int z, const std::string& expected) {
            BlockEntity entity;
            return session.BlockEntities().Get(x, y, z, entity) && entity.front.text == expected;
        }

        void PendingSlot(int x, int requestY, int z, uint64_t hash) {
            auto packet = Reply(x, requestY, z, {});
            packet.cacheEnabled = true;
            packet.entries[0].blobId = hash;
            Send(packet);
        }

        void PendingColumn(int x, int z, uint64_t hash, unsigned count) {
            auto packet = Column(x, z, {}, count);
            packet.cacheEnabled = true;
            packet.blobs.push_back(hash);
            Send(packet);
        }
    };

    struct DecodeGate {
        std::mutex mutex;
        std::condition_variable cv;
        unsigned entered = 0;
        unsigned released = 0;
    };
    DecodeGate* decodeGate = nullptr;

    int32_t BlockingResolver(const PersistentBlockState&) {
        auto& gate = *decodeGate;
        std::unique_lock lock(gate.mutex);
        const unsigned entry = ++gate.entered;
        gate.cv.notify_all();
        gate.cv.wait(lock, [&] { return gate.released >= entry; });
        return 77331;
    }

    void CheckResetDuringDecode(selftest::Checker& check) {
        Rig rig(check, 2193);
        rig.Send(Column(0, 0));
        // A real persistent-palette resolver pauses parsing, before preparation
        // or publication. The second old packet is already in the same queue
        // before the worker swaps that queue into its private batch.
        std::vector<uint8_t> persistent{9, 1, 0xfc, 0, 10, 0, 8};
        wire::WriteString(persistent, "name");
        wire::WriteString(persistent, "minecraft:stone");
        persistent.push_back(0);
        DecodeGate gate;
        decodeGate = &gate;
        SetPersistentResolver(&BlockingResolver);
        rig.Send(Reply(0, -4, 0, persistent), true);
        rig.Send(Column(0, 0, Subchunk(-4, 91), 1), true);
        ChunkPipelineTestAccess::StartWorker(rig.session);
        bool entered = false;
        {
            std::unique_lock lock(gate.mutex);
            entered = gate.cv.wait_for(lock, std::chrono::seconds(5), [&] { return gate.entered >= 1; });
        }
        check.Check(entered, "the real queued decoder reaches the persistent-palette synchronization point");
        std::promise<void> readerFinished;
        auto read = readerFinished.get_future();
        std::thread reader([&] {
            proto::OpenSignPacket packet;
            packet.position = {0, -64, 0};
            packet.isFront = true;
            rig.Send(packet, true, false);
            readerFinished.set_value();
        });
        check.Check(ChunkPipelineTestAccess::WaitForReaderBarrier(rig.session),
                    "the real dependent reader waits for the captured chunk dispatch prefix");
        std::promise<void> resetFinished;
        auto finished = resetFinished.get_future();
        std::thread reset([&] {
            ChunkPipelineTestAccess::Reset(rig.session);
            rig.session.NetLevel().EnsureChunk(0, 0);
            rig.session.NetLevel().SetBlock(0, -64, 0, 5012);
            resetFinished.set_value();
        });
        check.Check(finished.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
                    "world reset completes while the old decoder is still paused outside commit locks");
        check.Check(read.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
                    "reset cancels the dependent reader without waiting for the paused decoder");
        {
            std::lock_guard lock(gate.mutex);
            gate.released = ~0u;
        }
        gate.cv.notify_all();
        reset.join();
        reader.join();
        rig.session.WaitChunkWorkIdle();
        ChunkPipelineTestAccess::StopWorker(rig.session);
        InstallPersistentChunkResolver();
        decodeGate = nullptr;
        check.Check(rig.At(0, -4, 0) == 5012,
                    "neither the in-progress old decode nor the next old local-batch packet repopulates the reset world");
        check.Check(rig.session.BlockEntities().Size() == 0,
                    "discarded old chunk work publishes no block entities after reset");
        BedrockSession::ServerForm stale;
        check.Check(!rig.session.TakeServerForm(stale), "a cancelled old sign reader does not recreate a reset form");
    }

    void CheckBarrierPrefixAndStop(selftest::Checker& check) {
        for (const bool stop : {false, true}) {
            Rig rig(check, 2193);
            rig.Send(Column(0, 0));
            std::vector<uint8_t> first{9, 1, 0, 0, 10, 0, 8};
            wire::WriteString(first, "name");
            wire::WriteString(first, "minecraft:stone");
            first.push_back(0);
            auto later = first;
            Sign(first, 0, 0, 0, "prefix-text");
            Sign(later, 0, 0, 0, "later-text");
            DecodeGate gate;
            decodeGate = &gate;
            SetPersistentResolver(&BlockingResolver);
            rig.Send(Reply(0, 0, 0, first), true);
            ChunkPipelineTestAccess::StartWorker(rig.session);
            {
                std::unique_lock lock(gate.mutex);
                check.Check(gate.cv.wait_for(lock, std::chrono::seconds(5), [&] { return gate.entered >= 1; }),
                            "the prefix fixture pauses its first real chunk decode");
            }
            std::promise<void> readerFinished;
            auto read = readerFinished.get_future();
            std::thread::id handledThread;
            rig.session.On<proto::OpenSignPacket>([&](const auto&) { handledThread = std::this_thread::get_id(); });
            std::thread reader([&] {
                proto::OpenSignPacket packet;
                packet.position = {0, 0, 0};
                packet.isFront = true;
                rig.Send(packet, true, false);
                readerFinished.set_value();
            });
            check.Check(ChunkPipelineTestAccess::WaitForReaderBarrier(rig.session),
                        "the reader captures its watermark before later queued work is added");
            const auto receiverThread = reader.get_id();
            std::thread stopper;
            if (stop) {
                stopper = std::thread([&] { ChunkPipelineTestAccess::StopWorker(rig.session); });
                check.Check(read.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
                            "stopping chunk work cancels a waiting receiver even while the decoder is paused");
            } else {
                rig.Send(Reply(0, 0, 0, later), true);
                {
                    std::lock_guard lock(gate.mutex);
                    gate.released = 1;
                }
                gate.cv.notify_all();
                {
                    std::unique_lock lock(gate.mutex);
                    check.Check(gate.cv.wait_for(lock, std::chrono::seconds(5), [&] { return gate.entered >= 2; }),
                                "later queued work remains paused after the captured prefix completes");
                }
                check.Check(read.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
                            "a later queued decode does not extend an earlier reader's watermark wait");
            }
            {
                std::lock_guard lock(gate.mutex);
                gate.released = ~0u;
            }
            gate.cv.notify_all();
            reader.join();
            if (stopper.joinable())
                stopper.join();
            else {
                rig.session.WaitChunkWorkIdle();
                ChunkPipelineTestAccess::StopWorker(rig.session);
            }
            InstallPersistentChunkResolver();
            decodeGate = nullptr;
            BedrockSession::ServerForm form;
            if (stop) {
                check.Check(!rig.session.TakeServerForm(form) && handledThread == std::thread::id{},
                            "stop cancellation cannot dispatch a stale reader after wakeup");
            } else {
                check.Check(rig.session.TakeServerForm(form) && form.json.find("prefix-text") != std::string::npos &&
                                handledThread == receiverThread,
                            "the dependent handler uses completed prefix state on its original receiver thread");
            }
        }
    }

    void CheckReentrantReaderCancellation(selftest::Checker& check) {
        Rig rig(check, 2193);
        std::atomic<bool> entered{false}, returned{false};
        rig.session.SetSentTap([&](uint32_t id, const std::vector<uint8_t>&) {
            if (id != proto::ClientCacheBlobStatusPacket::kId || entered.exchange(true))
                return;
            proto::ContainerOpenPacket open;
            open.windowId = static_cast<proto::WindowID>(7);
            open.windowType = proto::WindowType::Container;
            open.coordinates = {0, 0, 0};
            rig.Send(open, true, false);
            returned = true;
        });
        auto packet = Column(0, 0, {}, 1);
        packet.cacheEnabled = true;
        packet.blobs.push_back(UINT64_C(0x43504c54fffffffe));
        rig.Send(packet, true);
        ChunkPipelineTestAccess::StartWorker(rig.session);
        rig.session.WaitChunkWorkIdle();
        ChunkPipelineTestAccess::StopWorker(rig.session);
        rig.session.SetSentTap({});
        check.Check(entered && returned && !ChunkPipelineTestAccess::InventoryOpen(rig.session),
                    "a worker-self world reader is cancelled observably without waiting or publishing partial UI");
    }
}

namespace perlin::selftest {
    int ChunkPipeline() {
        Checker check("chunk_pipeline_test");
#ifdef _WIN32
        _putenv_s("PERLIN_NO_BLOB_CACHE", "1");
#else
        setenv("PERLIN_NO_BLOB_CACHE", "1", 1);
#endif
        auto& palette = BlockPalette::Instance();
        const bool wasSequential = palette.Sequential();
        palette.SetSequential(false);
        palette.LearnName(131, "minecraft:barrel");
        const auto airRuntimeId = static_cast<uint32_t>(BlockPalette::AirHash());
        check.Check(palette.InternRuntime(BlockPalette::AirHash()) == 0 && palette.InternRuntime(0) != 0,
                    "hashed palette fixtures use the air hash rather than runtime zero for empty liquid");
        for (const int protocol : {944, 2193, kBedrockProtocol}) {
            Rig rig(check, protocol);
            rig.Send(Column(0, 0));
            proto::SubchunkPacket ordered;
            ordered.origin = {0, -4, 0};
            ordered.entries = {Entry(0, Subchunk(-4, 101)), Entry(0, {}, true),
                               Entry(1, Subchunk(-3, 102)), Entry(1, {}, true)};
            rig.Send(ordered);
            check.Check(rig.At(0, -4, 0) == 0 && rig.At(0, -3, 0) == 0,
                        "success followed by all-air stays in packet order with parallel preparation");
            std::ranges::reverse(ordered.entries);
            rig.Send(ordered);
            check.Check(rig.At(0, -4, 0) == Rig::Local(101) && rig.At(0, -3, 0) == Rig::Local(102),
                        "all-air followed by success also retains packet order");

            const uint64_t hash = UINT64_C(0x43504c5400000000) + static_cast<uint64_t>(protocol) * 16;
            rig.PendingSlot(0, -4, 0, hash + 1);
            check.Check(ChunkPipelineTestAccess::PendingBlobs(rig.session) == 1,
                        "a missing slot blob retains its original receive ticket");
            auto newer = Subchunk(-4, 103);
            Sign(newer, 0, -64, 0, "newer-slot");
            rig.Send(Reply(0, -4, 0, newer));
            auto older = Subchunk(-4, 104);
            Sign(older, 0, -64, 0, "older-slot");
            rig.Blob(hash + 1, older);
            check.Check(rig.At(0, -4, 0) == Rig::Local(103) && rig.Text(0, -64, 0, "newer-slot"),
                        "late slot blobs cannot overwrite newer blocks or block entities");
            check.Check(ChunkPipelineTestAccess::PendingBlobs(rig.session) == 0,
                        "obsolete completed blob waits are removed");

            rig.PendingSlot(0, -4, 0, hash + 2);
            newer = Subchunk(-3, 105);
            Sign(newer, 0, -48, 0, "newer-target");
            rig.Send(Reply(0, -3, 0, newer));
            older = Subchunk(-3, 106);
            Sign(older, 0, -48, 0, "redirected-old");
            rig.Blob(hash + 2, older);
            check.Check(rig.At(0, -3, 0) == Rig::Local(105) && rig.Text(0, -48, 0, "newer-target"),
                        "delayed payload Y overrides cannot overwrite a newer destination section");

            rig.PendingColumn(2, 0, hash + 3, 2);
            newer = Subchunk(-4, 107);
            Sign(newer, 32, -64, 0, "newer-in-column");
            // Request key differs deliberately: publication must stamp payload Y.
            rig.Send(Reply(2, -5, 0, newer));
            auto full = Subchunk(-4, 108);
            const auto upper = Subchunk(-3, 109);
            full.insert(full.end(), upper.begin(), upper.end());
            Sign(full, 32, -64, 0, "older-in-column");
            Sign(full, 32, -48, 0, "untouched-column");
            rig.Blob(hash + 3, full);
            check.Check(rig.At(2, -4, 0) == Rig::Local(107) && rig.At(2, -3, 0) == Rig::Local(109),
                        "old column blobs fill untouched sections while retaining redirected newer writes");
            check.Check(rig.Text(32, -64, 0, "newer-in-column") &&
                            rig.Text(32, -48, 0, "untouched-column"),
                        "cached column block entities are filtered by each section's receive revision");

            rig.PendingColumn(3, 0, hash + 4, 1);
            rig.Send(Column(3, 0, Subchunk(-4, 110), 1));
            rig.Blob(hash + 4, Subchunk(-4, 111));
            check.Check(rig.At(3, -4, 0) == Rig::Local(110),
                        "a late cached column cannot erase a newer whole-column replacement");

            rig.PendingColumn(4, 0, hash + 5, 2);
            rig.Send(Reply(4, -4, 0, Subchunk(-4, 112)));
            ChunkPipelineTestAccess::ExpireBlobs(rig.session);
            check.Check(rig.session.PendingSubChunkRequests() > 0 && rig.At(4, -4, 0) == Rig::Local(112),
                        "an exhausted column cache wait requests missing sections without clearing newer data");

            ChunkCodec::CellIds biomeCells;
            for (size_t cell = 0; cell < biomeCells.size(); ++cell)
                biomeCells[cell] = static_cast<int32_t>((cell * 7) % 23);
            std::vector<uint8_t> biomeBytes;
            ChunkCodec::WriteStorage(biomeBytes, biomeCells);
            auto biomeColumn = Column(5, 0, biomeBytes);
            biomeColumn.highestSubchunkCount = 0;
            rig.Send(biomeColumn);
            check.Check(rig.session.NetLevel().GetBiome(83, -57, 9) == biomeCells[3 * 256 + 9 * 16 + 7],
                        "packed biome layers use the validated decoder with the same network axis order");

            rig.Send(Column(6, 0));
            rig.PendingSlot(6, -1, 0, hash + 6);
            rig.Send(BlockUpdate(96, 0, 0, 121));
            rig.Send(BlockUpdate(96, 0, 0, 122));
            rig.Send(BlockUpdate(96, 0, 0, 123, 1));
            rig.Send(BlockUpdate(96, 0, 0, airRuntimeId, 1));
            rig.Send(BlockUpdate(96, 0, 0, 124, 1));
            rig.Send(BlockUpdate(100, 0, 0, airRuntimeId, 1));
            proto::UpdateBlockSyncedPacket synced;
            synced.position = {97, 0, 0};
            synced.blockRuntimeId = 126;
            rig.Send(synced);
            proto::UpdateSubchunkBlocksPacket edits;
            edits.x = 6;
            proto::BlockUpdate cell;
            cell.position = {98, 0, 0};
            cell.runtimeId = 127;
            edits.extra.push_back(cell);
            cell.runtimeId = 128;
            edits.blocks.push_back(cell);
            rig.Send(edits);
            rig.Send(SignUpdate(96, 0, 0, "cell-current"));
            check.Check(ChunkPipelineTestAccess::Changes(rig.session, 6, 0) == std::pair<size_t, size_t>{6, 1},
                        "pending edits coalesce repeated cells while keeping block and liquid layers separate");
            auto deferred = Subchunk(0, 120);
            deferred[1] = 2;
            ChunkCodec::CellIds water;
            water.fill(125);
            ChunkCodec::WriteStorage(deferred, water);
            Sign(deferred, 96, 0, 0, "cell-old");
            Sign(deferred, 99, 0, 0, "neighbor-old");
            rig.Blob(hash + 6, deferred);
            check.Check(rig.At(6, 0, 0) == Rig::Local(122) &&
                            rig.session.NetLevel().GetLiquid(96, 0, 0) == Rig::Local(124) &&
                            rig.session.NetLevel().GetBlock(97, 0, 0) == Rig::Local(126) &&
                            rig.session.NetLevel().GetBlock(98, 0, 0) == Rig::Local(128) &&
                            rig.session.NetLevel().GetLiquid(98, 0, 0) == Rig::Local(127),
                        "all three authoritative block-update packets survive a delayed section base");
            check.Check(rig.session.NetLevel().GetBlock(99, 0, 0) == Rig::Local(120) &&
                            rig.session.NetLevel().GetLiquid(99, 0, 0) == Rig::Local(125),
                        "overlaying newer single-cell edits preserves untouched base blocks and waterlogging");
            check.Check(rig.session.NetLevel().GetBlock(100, 0, 0) == Rig::Local(120) &&
                            rig.session.NetLevel().GetLiquid(100, 0, 0) == 0,
                        "an authoritative air edit clears exactly one cell of a delayed waterlogged base");
            check.Check(rig.Text(96, 0, 0, "cell-current") && rig.Text(99, 0, 0, "neighbor-old"),
                        "a newer BE position suppresses only that old tail entry, preserving untouched entries");

            rig.PendingColumn(8, 0, hash + 7, 1);
            rig.Send(BlockUpdate(128, 0, 0, 141));
            rig.Send(BlockUpdate(128, 0, 0, 142, 1));
            rig.Send(SignUpdate(128, 0, 0, "column-cell-current"));
            deferred = Subchunk(0, 140);
            Sign(deferred, 128, 0, 0, "column-cell-old");
            rig.Blob(hash + 7, deferred);
            check.Check(rig.At(8, 0, 0) == Rig::Local(141) &&
                            rig.session.NetLevel().GetBlock(129, 0, 0) == Rig::Local(140) &&
                            rig.session.NetLevel().GetLiquid(128, 0, 0) == Rig::Local(142) &&
                            rig.Text(128, 0, 0, "column-cell-current"),
                        "single-cell overlays also preserve an otherwise unloaded cached column base");

            rig.Send(Column(9, 0));
            rig.PendingSlot(9, 0, 0, hash + 8);
            rig.Send(BlockUpdate(144, 0, 0, 151));
            rig.Send(SignUpdate(144, 0, 0, "before-new-base"));
            rig.PendingSlot(9, 0, 0, hash + 9);
            deferred = Subchunk(0, 152);
            Sign(deferred, 144, 0, 0, "new-base");
            rig.Blob(hash + 9, deferred);
            rig.Blob(hash + 8, Subchunk(0, 150));
            check.Check(rig.At(9, 0, 0) == Rig::Local(152) && rig.Text(144, 0, 0, "new-base"),
                        "edits older than a newer base ticket are not replayed over that authoritative base");
            rig.session.PumpRequests(0);
            check.Check(ChunkPipelineTestAccess::ChangeColumns(rig.session) == 0,
                        "completed blob contexts release their coalesced history instead of retaining it permanently");

            rig.Send(Column(7, 0));
            auto queuedBase = Subchunk(0, 130);
            Sign(queuedBase, 112, 0, 0, "queued-base");
            rig.Send(Reply(7, 0, 0, queuedBase), true);
            rig.Send(BlockUpdate(112, 0, 0, 131), true);
            rig.Send(BlockUpdate(112, 0, 0, 132, 1), true);
            rig.Send(SignUpdate(112, 0, 0, "queued-current"), true);
            ChunkPipelineTestAccess::StartWorker(rig.session);
            proto::ContainerOpenPacket openContainer;
            openContainer.windowId = static_cast<proto::WindowID>(7);
            openContainer.windowType = proto::WindowType::Container;
            openContainer.coordinates = {112, 0, 0};
            rig.Send(openContainer, true);
            check.Check(ChunkPipelineTestAccess::ChestSlotType(rig.session) == proto::ContainerSlotType::Barrel,
                        "ContainerOpen selects its transaction slot type after earlier queued block updates");
            proto::OpenSignPacket openSign;
            openSign.position = {112, 0, 0};
            openSign.isFront = true;
            rig.Send(openSign, true);
            proto::LevelEventPacket cracking;
            cracking.event = proto::LevelEventPacketEvent::BlockStartBreak;
            cracking.position = {112, 0, 0};
            cracking.data = 1000;
            rig.Send(cracking, true);
            rig.session.WaitChunkWorkIdle();
            ChunkPipelineTestAccess::StopWorker(rig.session);
            check.Check(rig.At(7, 0, 0) == Rig::Local(131) &&
                            rig.session.NetLevel().GetLiquid(112, 0, 0) == Rig::Local(132) &&
                            rig.Text(112, 0, 0, "queued-current"),
                        "the actual receive queue preserves direct section, block, liquid and BE packet order");
            BedrockSession::ServerForm form;
            check.Check(rig.session.TakeServerForm(form) && form.json.find("queued-current") != std::string::npos,
                        "OpenSign observes earlier queued block-entity data");
            const auto cracks = rig.session.Cracks();
            check.Check(std::ranges::any_of(cracks, [](const auto& crack) { return crack.x == 112 && crack.y == 0 && crack.z == 0; }),
                        "a crack event observes the preceding queued air-to-solid update");
        }
        CheckResetDuringDecode(check);
        CheckBarrierPrefixAndStop(check);
        CheckReentrantReaderCancellation(check);
        palette.SetSequential(wasSequential);
        return check.Finish();
    }
}
