#include "client/systems/renderer/gpu/RenderContext_rhi.hpp"
#include "util/Env.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <cstring>

namespace perlin {
    namespace {
        constexpr uint32_t kSkinSlotBits = 20;
        constexpr uint32_t kSkinSlotMask = (1u << kSkinSlotBits) - 1;
        constexpr uint64_t kSkinOutMin = 4ull << 20;
    }

    bool RenderContext_rhi::EnsureSkinning() {
        if (skinning.pipeline)
            return true;
        if (skinning.failed)
            return false;
        skinning.failed = true;
        static const bool off = PERLIN_ENV("PERLIN_NO_GPU_SKINNING") != nullptr;
        if (off || !gpu.RingStorage())
            return false;
        const conduit::shader_library& library = gpu.Library();
        const conduit::program_info* info = library.find("entity_skin");
        if (!info)
            return false;
        const conduit::shader_target target = gpu.ShaderTarget();
        const auto code = library.code(*info, conduit::shader_stage::compute, target);
        if (code.empty())
            return false;
        conduit::device& dev = gpu.Dev();
        conduit::shader_desc sd;
        sd.stage = conduit::shader_stage::compute;
        sd.format = conduit::shader_library::format_for(target);
        sd.code = code;
        sd.entry = conduit::shader_library::entry_for(conduit::shader_stage::compute, target);
        sd.name = "entity_skin";
        skinning.shader = dev.create_shader(sd);
        const conduit::bind_layout_entry entries[] = {
            {0, conduit::binding_type::storage_buffer, conduit::shader_stage::compute},
            {1, conduit::binding_type::storage_buffer, conduit::shader_stage::compute},
            {2, conduit::binding_type::storage_buffer_rw, conduit::shader_stage::compute},
        };
        conduit::bind_layout_desc bl;
        bl.entries = entries;
        bl.name = "entity skin bindings";
        skinning.bindLayout = dev.create_bind_layout(bl);
        conduit::pipeline_layout_desc pl;
        pl.sets[0] = skinning.bindLayout;
        pl.push_constant_bytes = sizeof(SkinParams);
        pl.push_constant_stages = conduit::shader_stage::compute;
        pl.name = "entity skin layout";
        skinning.layout = dev.create_pipeline_layout(pl);
        conduit::compute_pipeline_desc cp;
        cp.compute = skinning.shader;
        cp.layout = skinning.layout;
        cp.name = "entity_skin";
        skinning.pipeline = skinning.shader && skinning.bindLayout && skinning.layout
                                ? dev.create_compute_pipeline(cp)
                                : conduit::pipeline_handle{};
        skinning.failed = !skinning.pipeline;
        if (skinning.failed)
            Log("render", "gpu entity skinning is unavailable: entities are posed on the cpu");
        else
            Log("render", "entities are posed on the gpu");
        return !skinning.failed;
    }

    uint32_t RenderContext_rhi::UiCreateSkin(const SkinVertex* vertices, uint32_t vertexCount) {
        if (!initialized || !vertices || vertexCount < 4 || !EnsureSkinning())
            return 0;
        conduit::buffer_desc bd;
        bd.size = static_cast<uint64_t>(vertexCount) * sizeof(SkinVertex);
        bd.usage = conduit::buffer_usage::storage | conduit::buffer_usage::copy_dst;
        bd.memory = conduit::memory_class::gpu_only;
        bd.stride = sizeof(SkinVertex);
        bd.name = "entity skin";
        const conduit::buffer_handle buffer = gpu.Dev().create_buffer(bd);
        if (!buffer)
            return 0;
        gpu.WriteBuffer(buffer, conduit::state::shader_read, 0, vertices, bd.size);
        uint32_t slot;
        if (!skinning.freeSlots.empty()) {
            slot = skinning.freeSlots.back();
            skinning.freeSlots.pop_back();
        } else {
            if (skinning.skins.size() >= kSkinSlotMask) {
                gpu.Dev().destroy(buffer);
                return 0;
            }
            slot = static_cast<uint32_t>(skinning.skins.size());
            skinning.skins.emplace_back();
        }
        SkinGpu& rec = skinning.skins[slot];
        rec.buffer = buffer;
        rec.count = vertexCount & ~3u;
        rec.used = true;
        return ((rec.generation & 0xfffu) << kSkinSlotBits) | (slot + 1);
    }

    bool RenderContext_rhi::UiSkinValid(uint32_t skin) const {
        const uint32_t slot = skin & kSkinSlotMask;
        if (slot == 0 || slot > skinning.skins.size())
            return false;
        const SkinGpu& rec = skinning.skins[slot - 1];
        return rec.used && (rec.generation & 0xfffu) == (skin >> kSkinSlotBits);
    }

    void RenderContext_rhi::UiDestroySkin(uint32_t skin) {
        if (!UiSkinValid(skin))
            return;
        SkinGpu& rec = skinning.skins[(skin & kSkinSlotMask) - 1];
        if (initialized)
            gpu.Dev().destroy(rec.buffer);
        rec.buffer = {};
        rec.used = false;
        rec.generation++;
        skinning.freeSlots.push_back((skin & kSkinSlotMask) - 1);
    }

    void RenderContext_rhi::BeginSkinFrame() {
        skinning.jobs.clear();
        skinning.used = 0;
        if (skinning.wanted <= skinning.capacity || !skinning.pipeline)
            return;
        uint64_t size = std::max(skinning.capacity, kSkinOutMin);
        while (size < skinning.wanted)
            size *= 2;
        if (skinning.out)
            gpu.Dev().destroy(skinning.out);
        conduit::buffer_desc bd;
        bd.size = size;
        bd.usage = conduit::buffer_usage::storage | conduit::buffer_usage::vertex;
        bd.memory = conduit::memory_class::gpu_only;
        bd.stride = 4;
        bd.name = "skinned entity vertices";
        skinning.out = gpu.Dev().create_buffer(bd);
        skinning.capacity = skinning.out ? size : 0;
        skinning.wanted = 0;
    }

    uint32_t RenderContext_rhi::UiCreateSkinnedFrameMesh(uint32_t skin, const SkinDraw& draw) {
        if (!initialized || !frameOpen || PackRunning() || !draw.bones || !UiSkinValid(skin) || !skinning.pipeline)
            return 0;
        const SkinGpu& rec = skinning.skins[(skin & kSkinSlotMask) - 1];
        const uint32_t count = rec.count;
        if (!count)
            return 0;
        const uint64_t bytes = static_cast<uint64_t>(count) * sizeof(UiVertex);
        if (!skinning.out || skinning.used + bytes > skinning.capacity) {
            skinning.wanted = std::max(skinning.wanted, (skinning.used + bytes) * 2);
            return 0;
        }
        const uint64_t align = std::max<uint64_t>(256, gpu.Caps().limits.storage_buffer_alignment);
        const uint64_t bonesBytes = static_cast<uint64_t>(std::max(1u, draw.boneCount)) * 64;
        const gpu::RingSlice bones = gpu.Ring(bonesBytes, align);
        const uint32_t indexCount = count / 4 * 6;
        const gpu::RingSlice indices = gpu.Ring(static_cast<uint64_t>(indexCount) * sizeof(uint32_t), 4);
        if (!bones.data || !indices.data)
            return 0;
        std::memcpy(bones.data, draw.bones, static_cast<size_t>(draw.boneCount) * 64);
        if (skinning.quadIndices.size() < indexCount) {
            const size_t quads = std::max<size_t>(indexCount / 6, skinning.quadIndices.size() / 6 * 2);
            skinning.quadIndices.resize(quads * 6);
            for (size_t q = 0; q < quads; q++) {
                uint32_t* o = &skinning.quadIndices[q * 6];
                const uint32_t b = static_cast<uint32_t>(q * 4);
                o[0] = b;
                o[1] = b + 1;
                o[2] = b + 2;
                o[3] = b;
                o[4] = b + 2;
                o[5] = b + 3;
            }
        }
        std::memcpy(indices.data, skinning.quadIndices.data(), static_cast<size_t>(indexCount) * sizeof(uint32_t));

        SkinJob job;
        job.skin = rec.buffer;
        job.bones = bones;
        job.bonesBytes = bonesBytes;
        SkinParams& p = job.params;
        for (int c = 0; c < 3; c++) {
            for (int r = 0; r < 3; r++)
                p.rot[c][r] = draw.rot[c * 3 + r];
            p.rot[c][3] = 0.0f;
        }
        p.light[0] = draw.light[0];
        p.light[1] = draw.light[1];
        p.light[2] = draw.light[2];
        p.light[3] = draw.shade ? 1.0f : 0.0f;
        std::memcpy(p.tint, draw.tint, sizeof(p.tint));
        p.uv[0] = draw.uvScale[0];
        p.uv[1] = draw.uvScale[1];
        p.uv[2] = draw.uvOffset[0];
        p.uv[3] = draw.uvOffset[1];
        p.count = count;
        p.flat = draw.flat ? 1u : 0u;
        p.outBase = static_cast<uint32_t>(skinning.used / 4);
        p.pad = 0;

        UiMeshGpu mesh;
        mesh.iCount = indexCount;
        mesh.transient = true;
        mesh.skinned = true;
        std::memcpy(mesh.bounds, draw.bounds, sizeof(mesh.bounds));
        mesh.tv.buffer = skinning.out;
        mesh.tv.offset = skinning.used;
        mesh.ti = indices;
        const uint32_t id = StoreUiMesh(mesh);
        if (!id)
            return 0;
        frameUiMeshes.push_back(id);
        skinning.jobs.push_back(job);
        skinning.used += (bytes + 767) / 768 * 768;
        return id;
    }

    void RenderContext_rhi::RecordSkins(conduit::command_list& list) {
        if (skinning.jobs.empty() || !skinning.pipeline || !skinning.out)
            return;
        list.barrier(conduit::buffer_barrier{skinning.out, conduit::state::vertex_buffer, conduit::state::shader_write});
        list.set_pipeline(skinning.pipeline);
        for (const SkinJob& job : skinning.jobs) {
            conduit::bind_group_entry entries[3] = {};
            entries[0].binding = 0;
            entries[0].buffer = {job.skin, 0, 0};
            entries[1].binding = 1;
            entries[1].buffer = {job.bones.buffer, job.bones.offset, job.bonesBytes};
            entries[2].binding = 2;
            entries[2].buffer = {skinning.out, 0, 0};
            conduit::bind_group_desc bg;
            bg.layout = skinning.bindLayout;
            bg.entries = entries;
            bg.per_frame = true;
            bg.name = "entity skin group";
            const conduit::bind_group_handle group = gpu.Dev().create_bind_group(bg);
            if (!group)
                continue;
            list.set_bind_group(0, group);
            list.push_constants(&job.params, sizeof(job.params));
            list.dispatch((job.params.count + 63) / 64, 1, 1);
        }
        list.barrier(conduit::buffer_barrier{skinning.out, conduit::state::shader_write, conduit::state::vertex_buffer});
        skinning.jobs.clear();
    }

    void RenderContext_rhi::DestroySkinning() {
        if (!initialized)
            return;
        auto& dev = gpu.Dev();
        for (SkinGpu& rec : skinning.skins)
            if (rec.used)
                dev.destroy(rec.buffer);
        dev.destroy(skinning.pipeline);
        dev.destroy(skinning.layout);
        dev.destroy(skinning.bindLayout);
        dev.destroy(skinning.shader);
        dev.destroy(skinning.out);
        std::vector<SkinGpu> keep = std::move(skinning.skins);
        skinning = Skinning{};
        for (SkinGpu& rec : keep) {
            rec.buffer = {};
            rec.used = false;
            rec.generation++;
        }
        skinning.skins = std::move(keep);
        for (uint32_t i = 0; i < skinning.skins.size(); i++)
            skinning.freeSlots.push_back(i);
    }
}
