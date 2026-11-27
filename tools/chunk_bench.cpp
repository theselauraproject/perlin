#include "world/level/Level.hpp"
#include "world/level/chunk/ChunkCodec.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// CPU-only benchmark of the real chunk decoder and world commit paths. No time
// threshold is a correctness assertion. Run the same executable options on old
// and new builds, alternating their order, on an otherwise idle machine.
namespace {
    using namespace perlin;
    using Clock = std::chrono::steady_clock;
    using Cells = std::array<uint16_t, ChunkCodec::kCells>;

    struct Options {
        std::string workload = "all";
        std::string fixture = "mixed";
        unsigned sections = 96;
        unsigned iterations = 32;
        unsigned workers = 1;
        unsigned sampleRate = 16;
        bool verifyOnly = false;
    };

    struct Fixture {
        Cells cells{};
        Cells liquid{};
        bool hasLiquid = false;
        std::vector<uint8_t> wire;
        SubChunkData parsed;
    };

    bool ReadUnsigned(const char* text, unsigned& value) {
        const std::string_view s(text);
        const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
        return result.ec == std::errc{} && result.ptr == s.data() + s.size() && value != 0;
    }

    bool ParseOptions(int argc, char** argv, Options& options) {
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            if (arg == "--verify") {
                options.verifyOnly = true;
                continue;
            }
            if (arg == "--help") {
                std::puts("chunk_bench [--workload all|expand|decode|commit|reader] "
                          "[--fixture mixed|uniform|two|packed3|packed5|wide|waterlogged] "
                          "[--sections N] [--iterations N] [--workers N] "
                          "[--reader-sample-rate N] [--verify]");
                std::exit(0);
            }
            if (++i == argc)
                return false;
            if (arg == "--workload")
                options.workload = argv[i];
            else if (arg == "--fixture")
                options.fixture = argv[i];
            else if (arg == "--sections") {
                if (!ReadUnsigned(argv[i], options.sections))
                    return false;
            } else if (arg == "--iterations") {
                if (!ReadUnsigned(argv[i], options.iterations))
                    return false;
            } else if (arg == "--workers") {
                if (!ReadUnsigned(argv[i], options.workers))
                    return false;
            } else if (arg == "--reader-sample-rate") {
                if (!ReadUnsigned(argv[i], options.sampleRate))
                    return false;
            } else {
                return false;
            }
        }
        const bool validWorkload = options.workload == "all" || options.workload == "expand" ||
                                   options.workload == "decode" || options.workload == "commit" ||
                                   options.workload == "reader";
        const bool validFixture = options.fixture == "mixed" || options.fixture == "uniform" ||
                                  options.fixture == "two" || options.fixture == "packed3" ||
                                  options.fixture == "packed5" || options.fixture == "wide" ||
                                  options.fixture == "waterlogged";
        return validWorkload && validFixture && options.workers <= options.sections &&
               options.workers <= 64 && options.sections <= 65536 && options.iterations <= 1000000;
    }

    uint64_t HashValue(uint64_t hash, uint16_t value) {
        return (hash ^ value) * UINT64_C(1099511628211);
    }

    template <class Array> uint64_t Samples(const Array& cells) {
        uint64_t hash = UINT64_C(1469598103934665603);
        for (size_t i = 0; i < cells.size(); i += 257)
            hash = HashValue(hash, static_cast<uint16_t>(cells[i]));
        return HashValue(hash, static_cast<uint16_t>(cells.back()));
    }

    bool BuildFixture(std::string_view kind, unsigned variant, Fixture& fixture) {
        unsigned distinct = 1;
        if (kind == "two")
            distinct = 2;
        else if (kind == "packed3")
            distinct = 5;
        else if (kind == "packed5")
            distinct = 23;
        else if (kind == "wide")
            distinct = 300;
        else if (kind == "waterlogged") {
            distinct = 19;
            fixture.hasLiquid = true;
        }
        ChunkCodec::CellIds ids{};
        ChunkCodec::CellIds liquidIds{};
        for (unsigned x = 0; x < 16; ++x) {
            for (unsigned z = 0; z < 16; ++z) {
                for (unsigned y = 0; y < 16; ++y) {
                    const unsigned cell = x * 256 + z * 16 + y;
                    const unsigned slot = (cell * 131 + (x * z + y) * 17) % distinct;
                    const uint16_t id = static_cast<uint16_t>(1 + slot * 7 + variant * 3001);
                    fixture.cells[cell] = id;
                    ids[cell] = id;
                    if (fixture.hasLiquid && ((x + z + y) % 7 == 0)) {
                        fixture.liquid[cell] = static_cast<uint16_t>(4000 + variant);
                        liquidIds[cell] = fixture.liquid[cell];
                    }
                }
            }
        }
        ChunkCodec::WriteSubChunk(fixture.wire, -4, ids);
        if (fixture.hasLiquid) {
            fixture.wire[1] = 2;
            ChunkCodec::WriteStorage(fixture.wire, liquidIds);
        }
        size_t consumed = 0;
        if (!ChunkCodec::ParseSubChunk(fixture.wire.data(), fixture.wire.size(), fixture.parsed, consumed) ||
            consumed != fixture.wire.size() || fixture.parsed.storages.size() != (fixture.hasLiquid ? 2u : 1u))
            return false;
        ChunkCodec::CellIds expanded{};
        if (!ChunkCodec::Expand(fixture.parsed.storages[0], expanded) || expanded != ids)
            return false;
        return !fixture.hasLiquid ||
               (ChunkCodec::Expand(fixture.parsed.storages[1], expanded) && expanded == liquidIds);
    }

    using Corpus = std::vector<std::array<Fixture, 2>>;

    bool BuildCorpus(const Options& options, Corpus& corpus) {
        const std::array<std::string_view, 6> kinds{"uniform", "two", "packed3", "packed5", "wide", "waterlogged"};
        const size_t count = options.fixture == "mixed" ? kinds.size() : 1;
        corpus.resize(count);
        for (size_t i = 0; i < count; ++i)
            for (unsigned variant = 0; variant < 2; ++variant)
                if (!BuildFixture(options.fixture == "mixed" ? kinds[i] : options.fixture, variant, corpus[i][variant]))
                    return false;
        return true;
    }

    const Fixture& At(const Corpus& corpus, unsigned section, unsigned iteration) {
        return corpus[section % corpus.size()][(section + iteration) & 1u];
    }

    uint64_t Percentile(const std::vector<uint64_t>& sorted, unsigned numerator) {
        return sorted.empty() ? 0 : sorted[(sorted.size() - 1) * numerator / 100];
    }

    bool CheckWorld(Level& level, const Corpus& corpus, const Options& options, uint64_t& checksum) {
        bool correct = true;
        checksum = UINT64_C(1469598103934665603);
        std::lock_guard lock(level.mutex);
        for (unsigned section = 0; section < options.sections; ++section) {
            const auto& expected = At(corpus, section, options.iterations - 1);
            const int cx = static_cast<int>(section / Chunk::SECTION_COUNT);
            const int absY = static_cast<int>(section % Chunk::SECTION_COUNT) + Chunk::MIN_Y / 16;
            const auto found = level.chunks.find({cx, 0});
            if (found == level.chunks.end())
                return false;
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    for (int y = 0; y < 16; ++y) {
                        const size_t cell = static_cast<size_t>(x * 256 + z * 16 + y);
                        const uint16_t block = found->second.Get(x, absY * 16 + y, z);
                        const uint16_t liquid = found->second.GetLiquid(x, absY * 16 + y, z);
                        correct &= block == expected.cells[cell] && liquid == expected.liquid[cell];
                        checksum = HashValue(HashValue(checksum, block), liquid);
                    }
                }
            }
        }
        return correct;
    }

    bool Run(const std::string& workload, const Corpus& corpus, const Options& options) {
        const bool commits = workload == "commit" || workload == "reader";
        const bool readers = workload == "reader";
        Level level;
        if (commits)
            for (unsigned section = 0; section < options.sections; section += Chunk::SECTION_COUNT)
                level.EnsureChunk(static_cast<int>(section / Chunk::SECTION_COUNT), 0);
        std::barrier start(static_cast<ptrdiff_t>(options.workers + 1 + (readers ? 1 : 0)));
        std::atomic<unsigned> active{options.workers};
        std::atomic<bool> failed{false};
        std::vector<uint64_t> checksums(options.workers);
        std::vector<std::thread> workers;
        std::vector<uint64_t> latencies;
        uint64_t readCount = 0;
        uint64_t readerChecksum = 0;
        workers.reserve(options.workers);
        for (unsigned worker = 0; worker < options.workers; ++worker) {
            workers.emplace_back([&, worker] {
                uint64_t checksum = 0;
                ChunkCodec::CellIndices indices{};
                ChunkCodec::CellIds decoded{};
                start.arrive_and_wait();
                for (unsigned iteration = 0; iteration < options.iterations; ++iteration) {
                    for (unsigned section = worker; section < options.sections; section += options.workers) {
                        const Fixture& fixture = At(corpus, section, iteration);
                        if (commits) {
                            const int cx = static_cast<int>(section / Chunk::SECTION_COUNT);
                            const int absY = static_cast<int>(section % Chunk::SECTION_COUNT) + Chunk::MIN_Y / 16;
                            level.SetSubChunk(cx, 0, absY, fixture.cells.data(),
                                              fixture.hasLiquid ? fixture.liquid.data() : nullptr);
                        } else if (workload == "expand") {
                            for (const auto& storage : fixture.parsed.storages) {
                                if (!ChunkCodec::ExpandIndices(storage, indices))
                                    failed = true;
                                checksum += Samples(indices);
                            }
                        } else {
                            SubChunkData parsed;
                            size_t consumed = 0;
                            if (!ChunkCodec::ParseSubChunk(fixture.wire.data(), fixture.wire.size(), parsed, consumed) ||
                                consumed != fixture.wire.size()) {
                                failed = true;
                                continue;
                            }
                            for (const auto& storage : parsed.storages) {
                                if (!ChunkCodec::Expand(storage, decoded))
                                    failed = true;
                                checksum += Samples(decoded);
                            }
                        }
                    }
                }
                checksums[worker] = checksum;
                active.fetch_sub(1, std::memory_order_release);
            });
        }
        std::thread reader;
        if (readers) {
            reader = std::thread([&] {
                latencies.reserve(262144);
                start.arrive_and_wait();
                while (active.load(std::memory_order_acquire) != 0) {
                    const unsigned section = static_cast<unsigned>(readCount % options.sections);
                    const int cx = static_cast<int>(section / Chunk::SECTION_COUNT);
                    const int y = static_cast<int>(section % Chunk::SECTION_COUNT) * 16 + Chunk::MIN_Y + 7;
                    if (readCount % options.sampleRate == 0) {
                        const auto began = Clock::now();
                        readerChecksum += level.GetBlock(cx * 16 + 3, y, 9);
                        const auto ended = Clock::now();
                        if (latencies.size() < 2000000)
                            latencies.push_back(static_cast<uint64_t>(
                                std::chrono::duration_cast<std::chrono::nanoseconds>(ended - began).count()));
                    } else {
                        readerChecksum += level.GetBlock(cx * 16 + 3, y, 9);
                    }
                    ++readCount;
                }
            });
        }
        const auto began = Clock::now();
        start.arrive_and_wait();
        for (auto& worker : workers)
            worker.join();
        const auto ended = Clock::now();
        if (reader.joinable())
            reader.join();
        uint64_t checksum = 0;
        for (uint64_t value : checksums)
            checksum += value;
        if (commits && !CheckWorld(level, corpus, options, checksum))
            failed = true;
        std::ranges::sort(latencies);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(ended - began).count();
        const uint64_t operations = static_cast<uint64_t>(options.sections) * options.iterations;
        const double nsPerSection = static_cast<double>(elapsed) / static_cast<double>(operations);
        std::printf("{\"workload\":\"%s\",\"fixture\":\"%s\",\"sections\":%u,\"iterations\":%u,"
                    "\"workers\":%u,\"operations\":%llu,\"elapsed_ns\":%lld,\"ns_per_section\":%.3f,"
                    "\"checksum\":\"%016llx\",\"correct\":%s,\"reader_calls\":%llu,\"reader_samples\":%zu,"
                    "\"reader_sample_rate\":%u,\"reader_p50_ns\":%llu,\"reader_p95_ns\":%llu,"
                    "\"reader_p99_ns\":%llu,\"reader_max_ns\":%llu,\"reader_checksum\":\"%016llx\"}\n",
                    workload.c_str(), options.fixture.c_str(), options.sections, options.iterations,
                    options.workers, static_cast<unsigned long long>(operations), static_cast<long long>(elapsed),
                    nsPerSection, static_cast<unsigned long long>(checksum), failed ? "false" : "true",
                    static_cast<unsigned long long>(readCount), latencies.size(), options.sampleRate,
                    static_cast<unsigned long long>(Percentile(latencies, 50)),
                    static_cast<unsigned long long>(Percentile(latencies, 95)),
                    static_cast<unsigned long long>(Percentile(latencies, 99)),
                    static_cast<unsigned long long>(latencies.empty() ? 0 : latencies.back()),
                    static_cast<unsigned long long>(readerChecksum));
        return !failed.load();
    }
}

int main(int argc, char** argv) {
    Options options;
    if (!ParseOptions(argc, argv, options)) {
        std::fputs("Invalid chunk benchmark options; use --help.\n", stderr);
        return 2;
    }
    Corpus corpus;
    if (!BuildCorpus(options, corpus)) {
        std::fputs("Chunk fixture failed its full decode check.\n", stderr);
        return 1;
    }
    if (options.verifyOnly) {
        options.sections = static_cast<unsigned>(corpus.size() * 2);
        options.iterations = 2;
        options.workers = 1;
        return Run("expand", corpus, options) && Run("decode", corpus, options) && Run("commit", corpus, options) ? 0 : 1;
    }
    if (options.workload != "all")
        return Run(options.workload, corpus, options) ? 0 : 1;
    for (const std::string workload : {"expand", "decode", "commit", "reader"})
        if (!Run(workload, corpus, options))
            return 1;
    return 0;
}
