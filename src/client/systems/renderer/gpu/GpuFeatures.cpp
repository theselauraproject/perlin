#include "client/systems/renderer/gpu/GpuFeatures.hpp"
#include "client/systems/renderer/gpu/SafeMode.hpp"

#include "util/Log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace perlin::gpu {

    namespace {
        constexpr const char* kEnv[GpuFeatures::kFeatureCount] = {
            "GPU_CULL", "OCCLUSION", "FACE_CULL", "GPU_SHADOW_CULL", "GPU_ENTITY_CULL", "GPU_UI"};
        constexpr const char* kNames[GpuFeatures::kFeatureCount] = {"gpu_cull",    "occlusion",   "face_cull",
                                                                     "shadow_cull", "entity_cull", "gpu_ui"};

        bool EnvSet(const std::string& name) {
            const char* v = std::getenv(name.c_str());
            return v && *v && std::strcmp(v, "0") != 0;
        }

        std::string Lower(std::string s) {
            for (char& c : s)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            return s;
        }
    }

    const char* GpuFeatures::Name(Feature f) {
        return kNames[f];
    }

    const char* GpuFeatures::LevelName(Level level) {
        switch (level) {
        case Level::Basic:
            return "basic";
        case Level::MultiDraw:
            return "multi-draw";
        case Level::GpuDriven:
            return "gpu-driven";
        case Level::GpuDrivenOcclusion:
            return "gpu-driven with occlusion culling";
        }
        return "basic";
    }

    bool GpuFeatures::Software(const Profile& p) {
        if (p.type == conduit::adapter_type::cpu)
            return true;
        const std::string n = Lower(p.name);
        for (const char* s : {"llvmpipe", "lavapipe", "softpipe", "swiftshader", "microsoft basic render", "warp"})
            if (n.find(s) != std::string::npos)
                return true;
        return false;
    }

    bool GpuFeatures::TileBased(const Profile& p) {
        switch (p.vendor) {
        case 0x13B5:
        case 0x5143:
        case 0x1010:
        case 0x144D:
        case 0x106B:
            return true;
        default:
            break;
        }
        return p.api == conduit::api::metal || p.api == conduit::api::gles;
    }

    GpuFeatures::Level GpuFeatures::Choose(const Profile& p, std::array<bool, kFeatureCount>& out) {
        out.fill(false);
        if (TileBased(p) && !Software(p))
            return Level::Basic;
        const bool software = Software(p);
        const bool driven = p.gpuDriven && p.compute && p.multiDraw && p.safeMode == 0;
        out[GpuUi] = p.multiDraw && p.firstInstance && p.safeMode < 2;
        if (!driven)
            return out[GpuUi] ? Level::MultiDraw : Level::Basic;
        out[GpuCull] = true;
        out[FaceCull] = true;
        out[ShadowCull] = true;
        out[EntityCull] = p.firstInstance && !software;
        const bool desktop = p.type == conduit::adapter_type::discrete ||
                             (p.type == conduit::adapter_type::integrated && !TileBased(p) &&
                              (p.vendor == 0x1002 || p.vendor == 0x10DE));
        out[Occlusion] = desktop && !software && !TileBased(p);
        return out[Occlusion] ? Level::GpuDrivenOcclusion : Level::GpuDriven;
    }

    void GpuFeatures::ApplyEnvironment() {
        for (int f = 0; f < kFeatureCount; f++) {
            if (EnvSet(std::string("PERLIN_NO_") + kEnv[f]))
                state[f] = false;
            else if (EnvSet(std::string("PERLIN_") + kEnv[f]))
                state[f] = true;
        }
    }

    void GpuFeatures::Init(const conduit::adapter_info& adapter, const conduit::device_features& features,
                           const std::string& dataDir) {
        Profile p;
        p.type = adapter.type;
        p.api = adapter.api;
        p.vendor = adapter.vendor_id;
        p.name = adapter.name;
        p.gpuDriven = features.gpu_driven || (features.multi_draw_indirect && features.draw_indirect_count);
        p.multiDraw = features.multi_draw_indirect;
        p.firstInstance = features.draw_indirect_first_instance;
        {
            int hangs = 0;
            std::ifstream in(std::filesystem::path(dataDir) / "gpu-hang.txt");
            in >> hangs;
            if (hangs > 0 && !EnvSet("PERLIN_GPU_RESET")) {
                SafeModeLevel() = hangs;
                p.safeMode = hangs;
            }
        }
        level = Choose(p, state);
        ApplyEnvironment();
        std::string summary;
        for (int f = 0; f < kFeatureCount; f++)
            summary += std::string(" ") + kNames[f] + "=" + (state[f] ? "on" : "off");
        Log("render", "feature level: {} on {}{}{}", LevelName(level), adapter.name,
            p.safeMode ? " (safe mode after a graphics driver reset)" : "", summary);
    }

    void GpuFeatures::GpuBegin(conduit::device& dev, conduit::command_list& list, uint32_t slot, uint32_t slots) {
        frameTimed = false;
        if (!everyFrame || !dev.caps().features.timestamp_queries)
            return;
        slots = std::max(1u, slots);
        if (!queries) {
            conduit::query_set_desc qd;
            qd.count = kQueriesPerSlot * slots;
            qd.name = "gpu pass times";
            queries = dev.create_query_set(qd);
            if (!queries)
                return;
            readback.assign(slots, {});
            pending.assign(slots, {});
            for (auto& b : readback) {
                conduit::buffer_desc bd;
                bd.size = kQueriesPerSlot * sizeof(uint64_t);
                bd.usage = conduit::buffer_usage::copy_dst;
                bd.memory = conduit::memory_class::readback;
                bd.name = "gpu pass times";
                b = dev.create_buffer(bd);
            }
            period = dev.timestamp_period_ns();
        }
        curSlot = slot % static_cast<uint32_t>(readback.size());
        Pending& p = pending[curSlot];
        if (p.valid && readback[curSlot]) {
            dev.invalidate(readback[curSlot], 0, kQueriesPerSlot * sizeof(uint64_t));
            if (const uint8_t* data = dev.map(readback[curSlot])) {
                uint64_t t[kQueriesPerSlot];
                std::memcpy(t, data, p.used * sizeof(uint64_t));
                const uint32_t last = p.used - 1;
                if (t[last] > t[0]) {
                    lastGpuMs = static_cast<double>(t[last] - t[0]) * period * 1e-6;
                    if (p.used > 2) {
                        AddPass("setup", static_cast<double>(t[1] - t[0]) * period * 1e-6);
                        for (uint32_t i = 1; i < last; i++)
                            if (t[i + 1] >= t[i])
                                AddPass(p.labels[i - 1], static_cast<double>(t[i + 1] - t[i]) * period * 1e-6);
                        if (++passFrames >= 240) {
                            std::string line = "gpu ms:";
                            char buf[64];
                            for (const auto& [label, total] : passTotals) {
                                std::snprintf(buf, sizeof(buf), " %s=%.3f", label, total / passFrames);
                                line += buf;
                            }
                            Log("profile", "{}", line);
                            passTotals.clear();
                            passFrames = 0;
                        }
                    }
                }
            }
        }
        p.valid = false;
        list.write_timestamp(queries, curSlot * kQueriesPerSlot);
        used = 1;
        frameLabels.clear();
        frameTimed = true;
    }

    void GpuFeatures::AddPass(const char* label, double ms) {
        for (auto& [l, total] : passTotals)
            if (l == label || std::strcmp(l, label) == 0) {
                total += ms;
                return;
            }
        passTotals.emplace_back(label, ms);
    }

    void GpuFeatures::Mark(conduit::command_list& list, const char* label) {
        if (!frameTimed || used + 1 >= kQueriesPerSlot)
            return;
        list.write_timestamp(queries, curSlot * kQueriesPerSlot + used);
        used++;
        frameLabels.push_back(label);
    }

    void GpuFeatures::GpuEnd(conduit::command_list& list) {
        if (!frameTimed)
            return;
        list.write_timestamp(queries, curSlot * kQueriesPerSlot + used);
        used++;
        list.resolve_queries(queries, curSlot * kQueriesPerSlot, used, readback[curSlot], 0);
        Pending& p = pending[curSlot];
        p.valid = true;
        p.used = used;
        p.labels = frameLabels;
    }

    void GpuFeatures::Release(conduit::device& dev) {
        for (auto& b : readback)
            if (b)
                dev.destroy(b);
        readback.clear();
        pending.clear();
        if (queries)
            dev.destroy(queries);
        queries = {};
    }

}
