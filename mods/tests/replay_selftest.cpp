#include "client/replay/ReplayController.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace perlin::replay;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::cout << "FAIL line " << __LINE__ << ": " #cond << std::endl;                                \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

int main(int argc, char** argv) {
    const fs::path base = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "perlin_replay_selftest";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);

    StateCache cache;
    cache.OnToClient(11, std::vector<uint8_t>(4, 1));
    cache.OnToClient(2, std::vector<uint8_t>{0, 0, 0, 3});
    cache.OnToClient(5, std::vector<uint8_t>{1});
    cache.OnToClient(58, std::vector<uint8_t>{0, 0, 0, 1});
    cache.OnToClient(58, std::vector<uint8_t>{2, 0, 0, 9});
    cache.OnToClient(58, std::vector<uint8_t>{0, 0, 0, 5});
    cache.OnToClient(13, std::vector<uint8_t>{2, 5, 1, 1});
    cache.OnToClient(13, std::vector<uint8_t>{4, 6, 1, 1});
    cache.OnToClient(14, std::vector<uint8_t>{2});
    std::vector<Record> snap = cache.Snapshot();
    CHECK(snap.size() == 5);
    size_t chunkCount = 0, entityCount = 0;
    for (const Record& r : snap) {
        chunkCount += r.id == 58;
        entityCount += r.id == 13;
        CHECK(r.id != 5);
    }
    CHECK(chunkCount == 2 && entityCount == 1);

    Metadata meta;
    meta.name = "selftest";
    meta.server = "127.0.0.1:19132";
    meta.startPose = {1, 2, 3, 4, 5, true};
    Recorder rec;
    std::string error;
    CHECK(rec.Start((base / "work").string(), meta, snap, error, [&] { return cache.Snapshot(); }, 100, 0));
    for (int i = 0; i < 10; i++) {
        rec.Push(Dir::ToClient, 9, std::vector<uint8_t>(3, static_cast<uint8_t>(i)));
        std::vector<uint8_t> input(32, 0);
        const float f[5] = {10, 20, static_cast<float>(30 + i), 40, 50};
        std::memcpy(input.data(), f, sizeof f);
        rec.Push(Dir::ToServer, 144, input);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    const std::string file = (base / "t.perlinreplay").string();
    CHECK(rec.Stop(file, {}, error));

    ReplayController c;
    CHECK(c.Open(file, error));
    CHECK(c.Meta().name == "selftest");
    CHECK(c.Meta().server == "127.0.0.1:19132");
    CHECK(c.DurationMs() >= 300);
    bool anySnapshotChunk = false;
    for (const ChunkInfo& ci : c.Meta().chunks)
        anySnapshotChunk |= ci.snapshot;
    CHECK(anySnapshotChunk);

    int injected = 0, chat = 0;
    c.Advance(
        1e9,
        [&](const Record& r) {
            injected++;
            chat += r.id == 9;
        },
        0);
    CHECK(chat == 10);
    CHECK(injected == 5 + 10);
    CHECK(c.RecordedPose().known && c.RecordedPose().x == 39.0f && c.RecordedPose().yaw == 20.0f);
    CHECK(c.AtEnd());

    c.Seek(c.DurationMs() - 20);
    CHECK(c.NeedsRestart());
    int afterSeek = 0, chunks = 0;
    c.Advance(
        0,
        [&](const Record& r) {
            afterSeek++;
            chunks += r.id == 58;
        },
        0);
    CHECK(chunks == 2);
    CHECK(afterSeek < injected + 20);

    c.AddPlaybackMarker("hello", 100);
    CHECK(c.SaveMarkers(error));
    ReplayController again;
    CHECK(again.Open(file, error));
    CHECK(again.Markers().size() == 1 && again.Markers()[0].name == "hello");

    Metadata quick;
    CHECK(Reader::ReadMetadata(file, quick, nullptr, error));
    CHECK(quick.chunks.size() == c.Meta().chunks.size());

    {
        const size_t chunkCount = c.Meta().chunks.size();
        c.Close();
        again.Close();
        const fs::path fake = base / "pack.zip";
        {
            std::ofstream f(fake, std::ios::binary);
            f << "PK-fake-pack-bytes";
        }
        std::vector<ArchiveAdd> adds;
        ArchiveAdd a;
        a.name = "packs/pack.zip";
        a.diskPath = fake.string();
        adds.push_back(a);
        ArchiveAdd idx;
        idx.name = "packs.json";
        idx.content = R"([{"file":"packs/pack.zip","uuid":"u-1","key":"k-1"}])";
        adds.push_back(idx);
        CHECK(AddArchiveFiles(file, adds, error));
        ReplayController withPacks;
        CHECK(withPacks.Open(file, error));
        const auto sp = withPacks.StoredPacks();
        CHECK(sp.size() == 1);
        if (sp.size() == 1) {
            CHECK(sp[0].uuid == "u-1" && sp[0].key == "k-1");
            CHECK(withPacks.ReadArchiveFile(sp[0].file) == "PK-fake-pack-bytes");
        }
        CHECK(withPacks.Meta().chunks.size() == chunkCount);
        withPacks.Close();
    }

    fs::remove_all(base, ec);
    if (failures == 0)
        std::cout << "replay_selftest ok" << std::endl;
    return failures == 0 ? 0 : 1;
}
