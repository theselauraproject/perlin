#include "client/renderer/gpu/GpuChunkCull.hpp"

#include <cmath>
#include <cstring>

namespace perlin::gpu {

    bool GpuChunkCull::Handles(RenderLayer layer) {
        for (RenderLayer l : kLayers)
            if (l == layer)
                return true;
        return false;
    }

    bool GpuChunkCull::Init(Gpu& g) {
        gpu = &g;
        ready = false;
        if (!g.Caps().features.multi_draw_indirect)
            return false;
        const conduit::shader_library& library = g.Library();
        const conduit::program_info* info = library.find("chunk_cull");
        if (!info)
            return false;
        const conduit::shader_target target = conduit::shader_library::target_for(g.Api());
        const auto code = library.code(*info, conduit::shader_stage::compute, target);
        if (code.empty())
            return false;
        conduit::device& dev = g.Dev();
        conduit::shader_desc sd;
        sd.stage = conduit::shader_stage::compute;
        sd.format = conduit::shader_library::format_for(target);
        sd.code = code;
        sd.entry = conduit::shader_library::entry_for(conduit::shader_stage::compute, target);
        sd.name = "chunk_cull";
        shader = dev.create_shader(sd);
        const conduit::bind_layout_entry entries[] = {
            {0, conduit::binding_type::storage_buffer, conduit::shader_stage::compute},
            {1, conduit::binding_type::storage_buffer_rw, conduit::shader_stage::compute},
        };
        conduit::bind_layout_desc bl;
        bl.entries = entries;
        bl.name = "chunk cull bindings";
        bindLayout = dev.create_bind_layout(bl);
        conduit::pipeline_layout_desc pl;
        pl.sets[0] = bindLayout;
        pl.push_constant_bytes = sizeof(Params);
        pl.push_constant_stages = conduit::shader_stage::compute;
        pl.name = "chunk cull layout";
        pipelineLayout = dev.create_pipeline_layout(pl);
        conduit::compute_pipeline_desc cp;
        cp.compute = shader;
        cp.layout = pipelineLayout;
        cp.name = "chunk_cull";
        pipeline = dev.create_compute_pipeline(cp);
        ready = shader && bindLayout && pipelineLayout && pipeline && Grow(4096);
        return ready;
    }

    void GpuChunkCull::Shutdown() {
        if (!gpu)
            return;
        conduit::device& dev = gpu->Dev();
        dev.destroy(group);
        dev.destroy(recordBuffer);
        dev.destroy(commands);
        dev.destroy(pipeline);
        dev.destroy(pipelineLayout);
        dev.destroy(bindLayout);
        dev.destroy(shader);
        group = {};
        recordBuffer = commands = {};
        pipeline = {};
        pipelineLayout = {};
        bindLayout = {};
        shader = {};
        capacity = 0;
        ready = false;
        builtGeneration = builtCave = ~0ull;
    }

    bool GpuChunkCull::Grow(uint64_t wanted) {
        if (wanted <= capacity)
            return true;
        uint64_t size = capacity ? capacity : 4096;
        while (size < wanted)
            size *= 2;
        conduit::device& dev = gpu->Dev();
        dev.wait_idle();
        dev.destroy(group);
        dev.destroy(recordBuffer);
        dev.destroy(commands);
        conduit::buffer_desc b;
        b.size = size * sizeof(ChunkMeshStore::CullRecord);
        b.usage = conduit::buffer_usage::storage | conduit::buffer_usage::copy_dst;
        b.memory = conduit::memory_class::gpu_only;
        b.stride = sizeof(ChunkMeshStore::CullRecord);
        b.name = "chunk cull records";
        recordBuffer = dev.create_buffer(b);
        b.size = size * 5 * sizeof(uint32_t);
        b.usage = conduit::buffer_usage::storage | conduit::buffer_usage::indirect;
        b.stride = 4;
        b.name = "chunk cull commands";
        commands = dev.create_buffer(b);
        if (!recordBuffer || !commands)
            return false;
        const conduit::bind_group_entry entries[] = {
            {0, 0, {recordBuffer, 0, 0}, {}, {}},
            {1, 0, {commands, 0, 0}, {}, {}},
        };
        conduit::bind_group_desc bg;
        bg.layout = bindLayout;
        bg.entries = entries;
        bg.name = "chunk cull group";
        group = dev.create_bind_group(bg);
        capacity = size;
        return static_cast<bool>(group);
    }

    const std::vector<GpuChunkCull::Bucket>& GpuChunkCull::Buckets(RenderLayer layer) const {
        static const std::vector<Bucket> none;
        for (size_t i = 0; i < kLayers.size(); i++)
            if (kLayers[i] == layer)
                return buckets[i];
        return none;
    }

    bool GpuChunkCull::Prepare(const ChunkMeshStore& meshes, const Frustum& frustum, float camX, float camZ, int cullRadius) {
        if (!ready)
            return false;
        if (uploadPending)
            builtGeneration = ~0ull;
        if (builtGeneration != meshes.MeshEpoch() || builtCave != meshes.CaveEpoch()) {
            records.clear();
            std::vector<ChunkMeshStore::CullRecord> layerRecords;
            std::vector<ChunkMeshStore::CullBucket> layerBuckets;
            for (size_t i = 0; i < kLayers.size(); i++) {
                meshes.BuildCullRecords(kLayers[i], layerRecords, layerBuckets);
                buckets[i].clear();
                const uint32_t base = static_cast<uint32_t>(records.size());
                for (const ChunkMeshStore::CullBucket& b : layerBuckets)
                    buckets[i].push_back({b.page, base + b.first, b.count});
                records.insert(records.end(), layerRecords.begin(), layerRecords.end());
            }
            if (!Grow(std::max<uint64_t>(records.size(), 1))) {
                ready = false;
                return false;
            }
            uploadBytes = records.size() * sizeof(ChunkMeshStore::CullRecord);
            uploadPending = uploadBytes > 0;
            if (uploadPending) {
                upload = gpu->Ring(uploadBytes, 16);
                if (!upload.data)
                    return false;
                std::memcpy(upload.data, records.data(), uploadBytes);
            }
            builtGeneration = meshes.MeshEpoch();
            builtCave = meshes.CaveEpoch();
        }
        std::memcpy(params.planes, frustum.planes, sizeof(params.planes));
        params.camera[0] = camX;
        params.camera[2] = camZ;
        params.recordCount = static_cast<uint32_t>(records.size());
        params.cullRadius = cullRadius;
        params.cameraChunkX = static_cast<int32_t>(std::floor(camX / 16.0f));
        params.cameraChunkZ = static_cast<int32_t>(std::floor(camZ / 16.0f));
        dispatchPending = !records.empty();
        return true;
    }

    void GpuChunkCull::Record(conduit::command_list& list) {
        if (!ready || !dispatchPending)
            return;
        dispatchPending = false;
        if (uploadPending) {
            uploadPending = false;
            conduit::buffer_barrier toCopy;
            toCopy.buffer = recordBuffer;
            toCopy.before = conduit::state::shader_read_other;
            toCopy.after = conduit::state::copy_dst;
            list.barrier(toCopy);
            list.copy_buffer(recordBuffer, 0, upload.buffer, upload.offset, uploadBytes);
            conduit::buffer_barrier toRead;
            toRead.buffer = recordBuffer;
            toRead.before = conduit::state::copy_dst;
            toRead.after = conduit::state::shader_read_other;
            list.barrier(toRead);
        }
        conduit::buffer_barrier toWrite;
        toWrite.buffer = commands;
        toWrite.before = conduit::state::indirect_argument;
        toWrite.after = conduit::state::shader_write;
        list.barrier(toWrite);
        list.set_pipeline(pipeline);
        list.set_bind_group(0, group);
        list.push_constants(&params, sizeof params);
        list.dispatch((params.recordCount + 63) / 64, 1, 1);
        conduit::buffer_barrier toIndirect;
        toIndirect.buffer = commands;
        toIndirect.before = conduit::state::shader_write;
        toIndirect.after = conduit::state::indirect_argument;
        list.barrier(toIndirect);
    }
}
