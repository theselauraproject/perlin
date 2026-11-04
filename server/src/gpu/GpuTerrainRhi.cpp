#include "gpu/GpuTerrain.hpp"

#include "core/Log.hpp"
#include "world/Blocks.hpp"

#include <conduit/device.hpp>
#include <conduit/render_profile.hpp>
#include <conduit/shader_library.hpp>

#include <algorithm>
#include <cstring>

namespace perlin::server {
    extern const uint8_t kTerrainShaderPack[];
    extern const size_t kTerrainShaderPackSize;

    namespace {
        // chunks per dispatch; the cell buffer is sized for it
        constexpr uint32_t kBatch = 16;
        constexpr uint64_t kCellsPerChunk = static_cast<uint64_t>(kHeight) * 256;

        struct PushParams {
            uint32_t chunkCount, seedLo, seedHi, reserved;
        };

        class GpuFlatBackend final : public TerrainBackend {
        public:
            ~GpuFlatBackend() override {
                if (!device)
                    return;
                device->wait_idle();
                device->destroy(bindGroup);
                device->destroy(pipeline);
                device->destroy(pipelineLayout);
                device->destroy(bindLayout);
                device->destroy(shader);
                device->destroy(profileBuffer);
                device->destroy(chunkBuffer);
                device->destroy(cellBuffer);
                device->destroy(readback);
                device->destroy(fence);
            }

            const char* Name() const override { return name.c_str(); }
            size_t PreferredBatch() const override { return kBatch; }

            bool Init(const ServerConfig& config, std::string& note) {
                seed = config.seed;
                const FlatCpuBackend reference(config);
                profile.assign(reference.Profile().begin(), reference.Profile().end());

                conduit::probe probe;
                conduit::probe_options po;
                po.read_caps = false;
                po.allow_software = config.terrainBackend == "gpu";
                probe = conduit::probe_system(po);
                const conduit::candidate* pick = nullptr;
                conduit::shader_library library;
                std::string packError;
                if (!library.load(std::span<const uint8_t>(kTerrainShaderPack, kTerrainShaderPackSize),
                                  &packError)) {
                    note = "terrain shader pack: " + packError;
                    return false;
                }
                const conduit::program_info* program = library.find("terrain_flat");
                if (!program) {
                    note = "terrain shader pack has no terrain_flat program";
                    return false;
                }
                // the best adapter whose api the pack carries a compute shader for
                for (const conduit::candidate& c : probe.candidates) {
                    if (!c.adapter.usable)
                        continue;
                    if (library
                            .code(*program, conduit::shader_stage::compute,
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
                const conduit::api api = pick->adapter.api;
                const auto code =
                    library.code(*program, conduit::shader_stage::compute, conduit::shader_library::target_for(api));
                conduit::shader_desc sd;
                sd.stage = conduit::shader_stage::compute;
                sd.format = conduit::shader_library::format_for(conduit::shader_library::target_for(api));
                sd.code = code;
                sd.entry = conduit::shader_library::entry_for(conduit::shader_stage::compute,
                                                        conduit::shader_library::target_for(api));
                sd.name = "terrain_flat";
                shader = device->create_shader(sd);

                const conduit::bind_layout_entry entries[] = {
                    {0, conduit::binding_type::storage_buffer, conduit::shader_stage::compute},
                    {1, conduit::binding_type::storage_buffer, conduit::shader_stage::compute},
                    {2, conduit::binding_type::storage_buffer_rw, conduit::shader_stage::compute},
                };
                conduit::bind_layout_desc bl;
                bl.entries = entries;
                bl.name = "terrain bindings";
                bindLayout = device->create_bind_layout(bl);
                conduit::pipeline_layout_desc pl;
                pl.sets[0] = bindLayout;
                pl.push_constant_bytes = sizeof(PushParams);
                pl.push_constant_stages = conduit::shader_stage::compute;
                pl.name = "terrain layout";
                pipelineLayout = device->create_pipeline_layout(pl);
                conduit::compute_pipeline_desc cp;
                cp.compute = shader;
                cp.layout = pipelineLayout;
                cp.name = "terrain_flat";
                pipeline = device->create_compute_pipeline(cp);

                conduit::buffer_desc b;
                b.size = static_cast<uint64_t>(profile.size()) * 4;
                b.usage = conduit::buffer_usage::storage;
                b.memory = conduit::memory_class::upload;
                b.stride = 4;
                b.name = "terrain profile";
                profileBuffer = device->create_buffer(b);
                b.size = static_cast<uint64_t>(kBatch) * 8;
                b.stride = 8;
                b.name = "terrain chunks";
                chunkBuffer = device->create_buffer(b);
                b.size = static_cast<uint64_t>(kBatch) * kCellsPerChunk * 4;
                b.usage = conduit::buffer_usage::storage | conduit::buffer_usage::copy_src;
                b.memory = conduit::memory_class::gpu_only;
                b.stride = 4;
                b.name = "terrain cells";
                cellBuffer = device->create_buffer(b);
                b.usage = conduit::buffer_usage::copy_dst;
                b.memory = conduit::memory_class::readback;
                b.stride = 0;
                b.name = "terrain readback";
                readback = device->create_buffer(b);
                fence = device->create_fence(0);

                uint8_t* p = device->map(profileBuffer);
                if (!p || !device->map(chunkBuffer) || !device->map(readback) || !pipeline || !fence) {
                    note = "terrain pipeline or buffers could not be created on " + pick->adapter.name;
                    return false;
                }
                std::vector<uint32_t> table(profile.begin(), profile.end());
                std::memcpy(p, table.data(), table.size() * 4);
                device->flush(profileBuffer, 0, table.size() * 4);

                const conduit::bind_group_entry groupEntries[] = {
                    {0, 0, {profileBuffer, 0, 0}, {}, {}},
                    {1, 0, {chunkBuffer, 0, 0}, {}, {}},
                    {2, 0, {cellBuffer, 0, 0}, {}, {}},
                };
                conduit::bind_group_desc bg;
                bg.layout = bindLayout;
                bg.entries = groupEntries;
                bg.name = "terrain group";
                bindGroup = device->create_bind_group(bg);

                name = std::string("flat/gpu (") + conduit::api_name(api) + ", " + pick->adapter.name + ")";
                return true;
            }

            void Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) override {
                for (size_t first = 0; first < positions.size(); first += kBatch) {
                    const size_t n = std::min<size_t>(kBatch, positions.size() - first);
                    Run(positions.subspan(first, n), out.subspan(first, n));
                }
            }

        private:
            void Run(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) {
                const uint32_t count = static_cast<uint32_t>(positions.size());
                uint8_t* chunkMem = device->map(chunkBuffer);
                for (uint32_t i = 0; i < count; i++) {
                    const int32_t xz[2] = {positions[i].x, positions[i].z};
                    std::memcpy(chunkMem + i * 8, xz, 8);
                }
                device->flush(chunkBuffer, 0, static_cast<uint64_t>(count) * 8);

                conduit::command_list* cmd = device->acquire_command_list(conduit::queue_type::graphics);
                const uint64_t bytes = static_cast<uint64_t>(count) * kCellsPerChunk * 4;
                cmd->set_pipeline(pipeline);
                cmd->set_bind_group(0, bindGroup);
                PushParams push{count, static_cast<uint32_t>(seed), static_cast<uint32_t>(seed >> 32), 0};
                cmd->push_constants(&push, sizeof push);
                const uint32_t groups =
                    static_cast<uint32_t>((static_cast<uint64_t>(count) * kCellsPerChunk + 63) / 64);
                cmd->dispatch(groups, 1, 1);
                conduit::buffer_barrier toCopy;
                toCopy.buffer = cellBuffer;
                toCopy.before = conduit::state::shader_write;
                toCopy.after = conduit::state::copy_src;
                cmd->barrier(toCopy);
                cmd->copy_buffer(readback, 0, cellBuffer, 0, bytes);
                const conduit::fence_op signal{fence, ++fenceValue};
                conduit::submit_desc sd;
                conduit::command_list* lists[] = {cmd};
                sd.lists = lists;
                sd.signals = std::span<const conduit::fence_op>(&signal, 1);
                device->submit(conduit::queue_type::graphics, sd);
                device->wait_fence(fence, fenceValue);
                device->invalidate(readback, 0, bytes);

                const uint32_t* cells = reinterpret_cast<const uint32_t*>(device->map(readback));
                std::array<uint16_t, kCells> section;
                for (uint32_t n = 0; n < count; n++) {
                    const uint32_t* chunk = cells + static_cast<uint64_t>(n) * kCellsPerChunk;
                    for (int sec = 0; sec < kSections; sec++) {
                        // the gpu lays a chunk out y-major; the wire wants x outermost and y innermost
                        for (int y = 0; y < 16; y++)
                            for (int z = 0; z < 16; z++)
                                for (int x = 0; x < 16; x++)
                                    section[static_cast<size_t>(CellIndex(x, y, z))] =
                                        static_cast<uint16_t>(chunk[(sec * 16 + y) * 256 + z * 16 + x]);
                        out[n].SetSection(sec, section.data());
                    }
                }
            }

            std::string name = "flat/gpu";
            uint64_t seed = 0;
            std::vector<uint16_t> profile;
            std::unique_ptr<conduit::instance> instance;
            std::unique_ptr<conduit::device> device;
            conduit::shader_handle shader;
            conduit::bind_layout_handle bindLayout;
            conduit::bind_group_handle bindGroup;
            conduit::pipeline_layout_handle pipelineLayout;
            conduit::pipeline_handle pipeline;
            conduit::buffer_handle profileBuffer, chunkBuffer, cellBuffer, readback;
            conduit::fence_handle fence;
            uint64_t fenceValue = 0;
        };
    }

    std::unique_ptr<TerrainBackend> CreateGpuTerrainBackend(const ServerConfig& config, std::string& note) {
        auto backend = std::make_unique<GpuFlatBackend>();
        if (!backend->Init(config, note))
            return nullptr;
        note = backend->Name();
        return backend;
    }
}
