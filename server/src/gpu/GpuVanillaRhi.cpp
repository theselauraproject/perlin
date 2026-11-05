// terrain on the gpu, stage by stage: density (the overworld's final_density graph), biome (the six climate functions where the ground is, then the nearest box of
// Java's placement table) and surface (blocks from the biome's Bedrock materials). the kernel is shaders/terrain_vanilla.slang, the cpu reference of every stage
// is src/world/vanilla. each stage is checked against that reference at start, on its own, and a stage that cannot run or disagrees falls back to the cpu while the
// stages before it stay on the gpu (the density stage is the exception: without it there is no gpu backend at all). if the device stops answering later, the
// whole backend falls back to the cpu
#include "gpu/GpuTerrain.hpp"
#include "gpu/VanillaGpuProgram.hpp"

#include "core/Log.hpp"
#include "world/Blocks.hpp"
#include "world/vanilla/VanillaTerrain.hpp"

#include <conduit/device.hpp>
#include <conduit/render_profile.hpp>
#include <conduit/shader_library.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <random>

namespace perlin::server {
    extern const uint8_t kTerrainShaderPack[];
    extern const size_t kTerrainShaderPackSize;

    namespace {
        // chunks per submit; the density buffer (393 KB a chunk) is sized for it
        constexpr uint32_t kBatch = 16;
        constexpr uint64_t kBlocksPerChunk = static_cast<uint64_t>(kHeight) * 256;
        constexpr uint64_t kCornersPerChunk = static_cast<uint64_t>(vanilla::GpuProgram::kCornersXz) *
                                              vanilla::GpuProgram::kCornersXz *
                                              vanilla::GpuProgram::kCornersY;
        // per column: the ground index and the biome
        constexpr uint64_t kColumnWords = 2;

        struct PushParams {
            uint32_t chunkCount, phase, orderBegin, orderCount, root, interpCount, columnBase, reserved1;
        };

        // what one submit brings back
        struct BatchOut {
            std::vector<float>
                density; // [chunk][x][z][y]; after the surface phase the bits are block ids instead
            std::vector<uint32_t> columns; // [chunk][x * 16 + z] -> ground index (as int), biome
        };

        class GpuVanillaBackend final : public TerrainBackend, public DensityBackend {
        public:
            ~GpuVanillaBackend() override {
                if (!device)
                    return;
                device->wait_idle();
                device->destroy(bindGroup);
                device->destroy(pipeline);
                device->destroy(pipelineLayout);
                device->destroy(bindLayout);
                device->destroy(shader);
                device->destroy(tableBuffer);
                device->destroy(chunkBuffer);
                device->destroy(scratchBuffer);
                device->destroy(densityBuffer);
                device->destroy(readback);
                device->destroy(readbackColumns);
                device->destroy(fence);
            }

            const char* Name() const override { return failed ? "vanilla/cpu (gpu failed)" : name.c_str(); }
            size_t PreferredBatch() const override { return failed ? 1 : kBatch; }
            // the device section of a batch (upload, dispatch, wait, readback) is one critical section; everything after it (block storage, the cpu stages, the cpu
            // fallback) runs outside it, so several workers keep the cpu busy while the next batch is on the gpu
            bool ThreadSafe() const override { return true; }
            bool FindSpawn(int& x, int& y, int& z) override { return cpu->FindSpawn(x, y, z); }
            // searching samples single points on the cpu's workers; the gpu is built for batches of whole chunks
            bool LocateBiome(int biome, int fromX, int fromZ, int radius, const std::atomic<bool>& cancel,
                             int& x, int& y, int& z) override {
                return cpu->LocateBiome(biome, fromX, fromZ, radius, cancel, x, y, z);
            }
            bool HasBiomes() const override { return true; }
            bool Decorates() const override { return cpu->Decorates(); }
            const json::Value* Spawns(int tableIndex) const override { return cpu->Spawns(tableIndex); }
            void Decorate(ChunkPos pos, const ChunkColumn* const region[9], ChunkColumn& out) const override {
                cpu->Decorate(pos, region, out);
            }

            bool Init(const ServerConfig& config, std::string& note) {
                std::string error;
                cpu = vanilla::VanillaCpuBackend::Create(config, error);
                if (!cpu) {
                    note = error;
                    return false;
                }
                program = vanilla::GpuProgram::Build(cpu->FinalDensity(), cpu->Climate().Roots(), error);
                if (!program) {
                    note = "terrain graph does not fit the gpu kernel: " + error;
                    return false;
                }
                tables = vanilla::PackTables(*program, cpu->Surface());

                conduit::probe_options po;
                po.read_caps = false;
                po.allow_software = config.terrainBackend == "gpu";
                const conduit::probe probe = conduit::probe_system(po);
                conduit::shader_library library;
                std::string packError;
                if (!library.load(std::span<const uint8_t>(kTerrainShaderPack, kTerrainShaderPackSize),
                                  &packError)) {
                    note = "terrain shader pack: " + packError;
                    return false;
                }
                const conduit::program_info* info = library.find("terrain_vanilla");
                if (!info) {
                    note = "terrain shader pack has no terrain_vanilla program";
                    return false;
                }
                const conduit::candidate* pick = nullptr;
                for (const conduit::candidate& c : probe.candidates) {
                    if (!c.adapter.usable)
                        continue;
                    if (library
                            .code(*info, conduit::shader_stage::compute,
                                  conduit::shader_library::target_for(c.adapter.api))
                            .empty())
                        continue;
                    pick = &c;
                    break;
                }
                if (!pick) {
                    note = po.allow_software
                               ? "no graphics device with a compute shader for it"
                               : "no hardware graphics device (software devices are skipped in auto mode)";
                    return false;
                }
                conduit::instance_options io;
                io.app_name = "Perlin";
                instance = conduit::create_instance(pick->adapter.api, io);
                if (!instance) {
                    note = "cannot create a graphics instance";
                    return false;
                }
                conduit::device_options dopts;
                dopts.adapter_index = pick->adapter.index;
                dopts.allow_async_compute = false;
                device = instance->create_device(dopts);
                if (!device) {
                    note = "cannot create a device on " + pick->adapter.name;
                    return false;
                }
                // the kernel does its coordinate math in double precision, to match the cpu exactly
                if (!device->caps().features.shader_float64) {
                    note = pick->adapter.name + " has no 64-bit floats in shaders";
                    return false;
                }
                const conduit::api api = pick->adapter.api;
                const auto code =
                    library.code(*info, conduit::shader_stage::compute, conduit::shader_library::target_for(api));
                conduit::shader_desc sd;
                sd.stage = conduit::shader_stage::compute;
                sd.format = conduit::shader_library::format_for(conduit::shader_library::target_for(api));
                sd.code = code;
                sd.entry = conduit::shader_library::entry_for(conduit::shader_stage::compute,
                                                        conduit::shader_library::target_for(api));
                sd.name = "terrain_vanilla";
                shader = device->create_shader(sd);

                const conduit::bind_layout_entry entries[] = {
                    {0, conduit::binding_type::storage_buffer, conduit::shader_stage::compute},
                    {1, conduit::binding_type::storage_buffer, conduit::shader_stage::compute},
                    {2, conduit::binding_type::storage_buffer_rw, conduit::shader_stage::compute},
                    {3, conduit::binding_type::storage_buffer_rw, conduit::shader_stage::compute},
                };
                conduit::bind_layout_desc bl;
                bl.entries = entries;
                bl.name = "vanilla terrain bindings";
                bindLayout = device->create_bind_layout(bl);
                conduit::pipeline_layout_desc pl;
                pl.sets[0] = bindLayout;
                pl.push_constant_bytes = sizeof(PushParams);
                pl.push_constant_stages = conduit::shader_stage::compute;
                pl.name = "vanilla terrain layout";
                pipelineLayout = device->create_pipeline_layout(pl);
                conduit::compute_pipeline_desc cp;
                cp.compute = shader;
                cp.layout = pipelineLayout;
                cp.name = "terrain_vanilla";
                pipeline = device->create_compute_pipeline(cp);

                interpCount = static_cast<uint32_t>(program->interpolatedInputs.size());
                columnBase =
                    static_cast<uint32_t>(kBatch * kCornersPerChunk * std::max<uint32_t>(1, interpCount));
                conduit::buffer_desc b;
                b.size = static_cast<uint64_t>(tables.size()) * 4;
                b.usage = conduit::buffer_usage::storage;
                b.memory = conduit::memory_class::upload;
                b.stride = 4;
                b.name = "vanilla tables";
                tableBuffer = device->create_buffer(b);
                b.size = static_cast<uint64_t>(kBatch) * 8;
                b.stride = 8;
                b.name = "vanilla chunks";
                chunkBuffer = device->create_buffer(b);
                // corners first, then the per-column results
                b.size = (static_cast<uint64_t>(columnBase) + kBatch * 256 * kColumnWords) * 4;
                b.usage = conduit::buffer_usage::storage | conduit::buffer_usage::copy_src;
                b.memory = conduit::memory_class::gpu_only;
                b.stride = 4;
                b.name = "vanilla scratch";
                scratchBuffer = device->create_buffer(b);
                b.size = static_cast<uint64_t>(kBatch) * kBlocksPerChunk * 4;
                b.name = "vanilla density";
                densityBuffer = device->create_buffer(b);
                b.usage = conduit::buffer_usage::copy_dst;
                b.memory = conduit::memory_class::readback;
                b.stride = 0;
                b.name = "vanilla readback";
                readback = device->create_buffer(b);
                b.size = static_cast<uint64_t>(kBatch) * 256 * kColumnWords * 4;
                b.name = "vanilla column readback";
                readbackColumns = device->create_buffer(b);
                fence = device->create_fence(0);

                uint8_t* t = device->map(tableBuffer);
                if (!t || !device->map(chunkBuffer) || !device->map(readback) ||
                    !device->map(readbackColumns) || !pipeline || !fence || !scratchBuffer ||
                    !densityBuffer) {
                    note = "terrain pipeline or buffers could not be created on " + pick->adapter.name;
                    return false;
                }
                std::memcpy(t, tables.data(), tables.size() * 4);
                device->flush(tableBuffer, 0, tables.size() * 4);

                const conduit::bind_group_entry groupEntries[] = {
                    {0, 0, {tableBuffer, 0, 0}, {}, {}},
                    {1, 0, {chunkBuffer, 0, 0}, {}, {}},
                    {2, 0, {scratchBuffer, 0, 0}, {}, {}},
                    {3, 0, {densityBuffer, 0, 0}, {}, {}},
                };
                conduit::bind_group_desc bg;
                bg.layout = bindLayout;
                bg.entries = groupEntries;
                bg.name = "vanilla terrain group";
                bindGroup = device->create_bind_group(bg);
                deviceName = std::string(conduit::api_name(api)) + ", " + pick->adapter.name;
                return true;
            }

            // checks each stage against the cpu reference on a few chunks and settles which stages run on the gpu; false when even the density stage fails
            bool Validate(std::string& note) {
                const std::vector<ChunkPos> probe = {
                    {0, 0}, {-1, -1}, {37, 12}, {-2048, 2047}, {100000, -100000}};
                const size_t chunks = probe.size();

                // stage 1, density: bit for bit against the graph (chunk 0 in full, the rest sampled)
                BatchOut d;
                if (!Dispatch(probe, 1, false, d)) {
                    note = "the device did not finish the density probe";
                    return false;
                }
                std::mt19937 rng(0x5EED);
                for (size_t n = 0; n < chunks; n++) {
                    vanilla::DfCacheScope cache;
                    const int bx = probe[n].x * 16, bz = probe[n].z * 16;
                    const bool full = n == 0;
                    const int samples = full ? 16 * 16 * kHeight : 4096;
                    for (int s = 0; s < samples; s++) {
                        int x, z, y;
                        if (full) {
                            x = s / (16 * kHeight);
                            z = (s / kHeight) & 15;
                            y = s % kHeight;
                        } else {
                            x = static_cast<int>(rng() & 15);
                            z = static_cast<int>(rng() & 15);
                            y = static_cast<int>(rng() % kHeight);
                        }
                        const float want = cpu->Density(bx + x, kMinY + y, bz + z);
                        const float got =
                            d.density[n * kBlocksPerChunk + static_cast<size_t>((x * 16 + z) * kHeight + y)];
                        if (std::memcmp(&want, &got, sizeof want) != 0 && !(want != want && got != got)) {
                            note = "density differs at (" + std::to_string(bx + x) + "," +
                                   std::to_string(kMinY + y) + "," + std::to_string(bz + z) + "): cpu " +
                                   std::to_string(want) + ", gpu " + std::to_string(got);
                            return false;
                        }
                    }
                }

                // stage 2, biome: the ground height and biome of every column, from the densities the gpu itself produced
                std::string biomeNote, surfaceNote;
                BatchOut c;
                gpuBiome = Dispatch(probe, 2, true, c);
                if (!gpuBiome)
                    biomeNote = "the device did not finish the biome probe";
                std::vector<uint8_t> expectedBiome(chunks * 256);
                std::vector<int> expectedTop(chunks * 256);
                for (size_t n = 0; n < chunks && gpuBiome; n++) {
                    cpu->BiomesOf(c.density.data() + n * kBlocksPerChunk, probe[n].x, probe[n].z,
                                  expectedBiome.data() + n * 256);
                    for (int col = 0; col < 256; col++) {
                        const size_t at = n * 256 + static_cast<size_t>(col);
                        expectedTop[at] = vanilla::VanillaSurface::TopOf(c.density.data() + at * kHeight);
                        if (static_cast<int>(c.columns[at * 2]) != expectedTop[at] ||
                            c.columns[at * 2 + 1] != expectedBiome[at]) {
                            gpuBiome = false;
                            biomeNote = "biome differs in chunk (" + std::to_string(probe[n].x) + "," +
                                        std::to_string(probe[n].z) + ") column " + std::to_string(col) +
                                        ": cpu " + vanilla::kBiomes[expectedBiome[at]].java +
                                        ", gpu biome index " + std::to_string(c.columns[at * 2 + 1]);
                            break;
                        }
                    }
                }

                // stage 3, surface: every block of the probe chunks
                gpuSurface = false;
                if (gpuBiome) {
                    BatchOut s;
                    gpuSurface = Dispatch(probe, 3, false, s);
                    if (!gpuSurface)
                        surfaceNote = "the device did not finish the surface probe";
                    for (size_t n = 0; n < chunks && gpuSurface; n++)
                        for (int col = 0; col < 256 && gpuSurface; col++) {
                            const size_t at = n * 256 + static_cast<size_t>(col);
                            const vanilla::SurfaceMaterial& m = cpu->Surface().Materials()[expectedBiome[at]];
                            for (int y = 0; y < kHeight; y++) {
                                const uint16_t want = cpu->Surface().BlockAt(
                                    m, y, expectedTop[at],
                                    c.density[at * kHeight + static_cast<size_t>(y)] > 0.0f);
                                uint32_t got;
                                std::memcpy(&got, &s.density[at * kHeight + static_cast<size_t>(y)], 4);
                                if (got != want) {
                                    gpuSurface = false;
                                    surfaceNote = "surface differs in chunk (" + std::to_string(probe[n].x) +
                                                  "," + std::to_string(probe[n].z) + ") column " +
                                                  std::to_string(col) + " y " + std::to_string(kMinY + y) +
                                                  ": cpu block " + std::to_string(want) + ", gpu " +
                                                  std::to_string(got);
                                    break;
                                }
                            }
                        }
                }

                std::string on = "density", off;
                if (gpuBiome)
                    on += ", biome";
                else
                    off += "biome, surface";
                if (gpuSurface)
                    on += ", surface";
                else if (gpuBiome)
                    off += "surface";
                name = "vanilla/gpu (" + deviceName + "; gpu: " + on + (off.empty() ? "" : "; cpu: " + off) +
                       ")";
                note = name + ", each gpu stage identical to the cpu reference on the probe chunks";
                if (!biomeNote.empty())
                    note += "; biome stage on the cpu: " + biomeNote;
                else if (!surfaceNote.empty())
                    note += "; surface stage on the cpu: " + surfaceNote;
                return true;
            }

            void Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) override {
                for (size_t first = 0; first < positions.size(); first += kBatch) {
                    if (failed) {
                        cpu->Generate(positions.subspan(first), out.subspan(first));
                        return;
                    }
                    const size_t n = std::min<size_t>(kBatch, positions.size() - first);
                    if (!Batch(positions.subspan(first, n), out.subspan(first, n))) {
                        // the device stopped answering: the cpu takes over from here, nothing else changes
                        if (!failed.exchange(true))
                            LogWarn("gpu terrain stopped responding; generating on the cpu from now on");
                        cpu->Generate(positions.subspan(first), out.subspan(first));
                        return;
                    }
                }
            }

            void Density(std::span<const ChunkPos> positions, std::vector<float>& out) override {
                out.resize(positions.size() * kBlocksPerChunk);
                for (size_t first = 0; first < positions.size() && !failed; first += kBatch) {
                    const size_t n = std::min<size_t>(kBatch, positions.size() - first);
                    BatchOut b;
                    if (!Dispatch(positions.subspan(first, n), 1, false, b)) {
                        failed = true;
                        return;
                    }
                    std::memcpy(out.data() + first * kBlocksPerChunk, b.density.data(),
                                n * kBlocksPerChunk * 4);
                }
            }

        private:
            // one submit's worth of chunks through every stage that runs on the gpu, then the cpu stages
            bool Batch(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) {
                const bool surfaceStage = gpuSurface && !cpu->Aquifers();
                const int last = surfaceStage ? 3 : (gpuBiome ? 2 : 1);
                BatchOut b;
                // the columns come back whenever the biome stage ran on the gpu: the features need each column's biome
                if (!Dispatch(positions, last, gpuBiome, b))
                    return false;
                std::vector<uint16_t> blocks;
                std::array<uint8_t, 256> biomes;
                for (size_t i = 0; i < positions.size(); i++) {
                    const float* density = b.density.data() + i * kBlocksPerChunk;
                    if (surfaceStage) {
                        blocks.resize(kBlocksPerChunk);
                        const uint32_t* ids = reinterpret_cast<const uint32_t*>(density);
                        for (size_t k = 0; k < kBlocksPerChunk; k++)
                            blocks[k] = static_cast<uint16_t>(ids[k]);
                        // the surface stage runs only after the biome stage, so the columns are here
                        for (size_t col = 0; col < 256; col++)
                            biomes[col] = static_cast<uint8_t>(b.columns[(i * 256 + col) * 2 + 1]);
                        cpu->Features().Apply(positions[i].x, positions[i].z, biomes.data(), blocks.data());
                        vanilla::VanillaSurface::Store(blocks.data(), out[i]);
                        vanilla::VanillaSurface::StoreBiomes(biomes.data(), out[i]);
                        continue;
                    }
                    if (gpuBiome) {
                        for (size_t col = 0; col < 256; col++)
                            biomes[col] = static_cast<uint8_t>(b.columns[(i * 256 + col) * 2 + 1]);
                    } else {
                        cpu->BiomesOf(density, positions[i].x, positions[i].z, biomes.data());
                    }
                    cpu->Surface().Fill(density, biomes.data(), out[i], cpu->Decorates() ? nullptr : &cpu->Features(), positions[i].x,
                                        positions[i].z, cpu->Stages());
                }
                return true;
            }

            // runs phases 0..lastPhase for up to kBatch chunks and reads back the density buffer (and the column results when asked); false when the device hangs
            bool Dispatch(std::span<const ChunkPos> positions, int lastPhase, bool wantColumns,
                          BatchOut& out) {
                std::lock_guard<std::mutex> lock(deviceMutex);
                if (failed)
                    return false;
                const uint32_t count = static_cast<uint32_t>(positions.size());
                uint8_t* chunkMem = device->map(chunkBuffer);
                for (uint32_t i = 0; i < count; i++) {
                    const int32_t xz[2] = {positions[i].x, positions[i].z};
                    std::memcpy(chunkMem + i * 8, xz, 8);
                }
                device->flush(chunkBuffer, 0, static_cast<uint64_t>(count) * 8);

                conduit::command_list* cmd = device->acquire_command_list(conduit::queue_type::graphics);
                cmd->set_pipeline(pipeline);
                cmd->set_bind_group(0, bindGroup);
                const auto run = [&](uint32_t phase, uint32_t begin, uint32_t number, uint64_t threads) {
                    PushParams push{count, phase, begin, number, program->root, interpCount, columnBase, 0};
                    cmd->push_constants(&push, sizeof push);
                    cmd->dispatch(static_cast<uint32_t>((threads + 63) / 64), 1, 1);
                    // the next phase reads what this one wrote
                    conduit::buffer_barrier scratch;
                    scratch.buffer = scratchBuffer;
                    scratch.before = conduit::state::shader_write;
                    scratch.after = conduit::state::shader_write;
                    cmd->barrier(scratch);
                    conduit::buffer_barrier dens = scratch;
                    dens.buffer = densityBuffer;
                    cmd->barrier(dens);
                };
                run(0, 0, program->innerCount, static_cast<uint64_t>(count) * kCornersPerChunk);
                run(1, program->innerCount, program->outerCount,
                    static_cast<uint64_t>(count) * kBlocksPerChunk);
                if (lastPhase >= 2)
                    run(2, 0, 0, static_cast<uint64_t>(count) * 256);
                if (lastPhase >= 3)
                    run(3, 0, 0, static_cast<uint64_t>(count) * kBlocksPerChunk);

                const uint64_t bytes = static_cast<uint64_t>(count) * kBlocksPerChunk * 4;
                const uint64_t columnBytes = static_cast<uint64_t>(count) * 256 * kColumnWords * 4;
                conduit::buffer_barrier toCopy;
                toCopy.buffer = densityBuffer;
                toCopy.before = conduit::state::shader_write;
                toCopy.after = conduit::state::copy_src;
                cmd->barrier(toCopy);
                cmd->copy_buffer(readback, 0, densityBuffer, 0, bytes);
                if (wantColumns) {
                    conduit::buffer_barrier cols;
                    cols.buffer = scratchBuffer;
                    cols.before = conduit::state::shader_write;
                    cols.after = conduit::state::copy_src;
                    cmd->barrier(cols);
                    cmd->copy_buffer(readbackColumns, 0, scratchBuffer, static_cast<uint64_t>(columnBase) * 4,
                                    columnBytes);
                }
                const conduit::fence_op signal{fence, ++fenceValue};
                conduit::submit_desc sd;
                conduit::command_list* lists[] = {cmd};
                sd.lists = lists;
                sd.signals = std::span<const conduit::fence_op>(&signal, 1);
                device->submit(conduit::queue_type::graphics, sd);
                // a wedged or lost device must not hang the server: a generous bound, then the caller hands over to the cpu
                if (!device->wait_fence(fence, fenceValue, 30ull * 1000 * 1000 * 1000)) {
                    failed = true;
                    return false;
                }
                device->invalidate(readback, 0, bytes);
                out.density.resize(static_cast<size_t>(count) * kBlocksPerChunk);
                std::memcpy(out.density.data(), device->map(readback), bytes);
                if (wantColumns) {
                    device->invalidate(readbackColumns, 0, columnBytes);
                    out.columns.resize(static_cast<size_t>(count) * 256 * kColumnWords);
                    std::memcpy(out.columns.data(), device->map(readbackColumns), columnBytes);
                }
                return true;
            }

            std::string name = "vanilla/gpu";
            std::string deviceName;
            std::atomic<bool> failed{false};
            std::mutex deviceMutex;
            bool gpuBiome = false, gpuSurface = false;
            // the reference, the fallback for every stage, and the owner of the biome and block rules
            std::unique_ptr<vanilla::VanillaCpuBackend> cpu;
            std::unique_ptr<vanilla::GpuProgram> program;
            std::vector<uint32_t> tables;
            uint32_t interpCount = 0, columnBase = 0;
            std::unique_ptr<conduit::instance> instance;
            std::unique_ptr<conduit::device> device;
            conduit::shader_handle shader;
            conduit::bind_layout_handle bindLayout;
            conduit::bind_group_handle bindGroup;
            conduit::pipeline_layout_handle pipelineLayout;
            conduit::pipeline_handle pipeline;
            conduit::buffer_handle tableBuffer, chunkBuffer, scratchBuffer, densityBuffer, readback,
                readbackColumns;
            conduit::fence_handle fence;
            uint64_t fenceValue = 0;
        };
    }

    std::unique_ptr<TerrainBackend> CreateGpuVanillaBackend(const ServerConfig& config, std::string& note) {
        auto backend = std::make_unique<GpuVanillaBackend>();
        if (!backend->Init(config, note))
            return nullptr;
        const auto started = std::chrono::steady_clock::now();
        std::string result;
        if (!backend->Validate(result)) {
            note = "gpu density differs from the cpu reference (" + result + ")";
            return nullptr;
        }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        note = result + " (" + std::to_string(static_cast<int>(ms)) + " ms check)";
        return backend;
    }
}
