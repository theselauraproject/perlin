#include "core/Log.hpp"
#include "gpu/GpuTerrain.hpp"
#include "world/Generator.hpp"
#include "world/vanilla/VanillaTerrain.hpp"

namespace perlin::server {

    bool BackendsAgree(TerrainBackend& candidate, TerrainBackend& reference, std::string& detail) {
        // a spread of positions: origin, negatives, and far out where float precision would show
        const std::vector<ChunkPos> probe = {{0, 0},   {-1, -1},      {1, -3},
                                             {37, 12}, {-2048, 2047}, {100000, -100000}};
        std::vector<ChunkColumn> a(probe.size()), b(probe.size());
        candidate.Generate(probe, a);
        reference.Generate(probe, b);
        for (size_t n = 0; n < probe.size(); n++) {
            for (int sec = 0; sec < kSections; sec++) {
                for (int i = 0; i < 4096; i += 1) {
                    const int x = i >> 8, z = (i >> 4) & 15, y = i & 15;
                    if (a[n].Get(x, kMinY + sec * 16 + y, z) != b[n].Get(x, kMinY + sec * 16 + y, z)) {
                        detail = "chunk (" + std::to_string(probe[n].x) + "," + std::to_string(probe[n].z) +
                                 ") differs in section " + std::to_string(sec);
                        return false;
                    }
                }
            }
        }
        return true;
    }

    BackendChoice CreateTerrainBackend(const ServerConfig& config) {
        BackendChoice out;
        if (config.generator == "vanilla") {
            // the gpu path is the default; "cpu" forces the reference, and anything the gpu cannot do falls back to it with the reason in the note
            std::string error;
            if (config.terrainBackend != "cpu") {
                std::string note;
                if (std::unique_ptr<TerrainBackend> gpu = CreateGpuVanillaBackend(config, note)) {
                    out.backend = std::move(gpu);
                    out.note = note;
                    return out;
                }
                if (config.terrainBackend == "gpu") {
                    out.note = "gpu terrain requested but unavailable: " + note;
                    return out;
                }
                out.backend = vanilla::VanillaCpuBackend::Create(config, error);
                out.note = out.backend ? "java terrain port on the cpu: " + note : error;
                return out;
            }
            out.backend = vanilla::VanillaCpuBackend::Create(config, error);
            out.note = out.backend ? "java terrain port on the cpu, forced by config" : error;
            return out;
        }
        auto cpu = std::make_unique<FlatCpuBackend>(config);
        if (config.terrainBackend == "cpu") {
            out.backend = std::move(cpu);
            out.note = "forced by config";
            return out;
        }
        // a flat world is a table lookup: the cpu fills a section in one store, so the gpu (dispatch, readback, copy-out) only loses.
        // auto leaves it to terrain that does real work per block; "gpu" forces it for testing
        if (config.terrainBackend == "auto" && config.generator == "flat") {
            out.backend = std::move(cpu);
            out.note = "flat terrain is faster on the cpu; gpu terrain is for noise generators";
            return out;
        }
        std::string note;
        std::unique_ptr<TerrainBackend> gpu = CreateGpuTerrainBackend(config, note);
        if (gpu) {
            std::string detail;
            if (BackendsAgree(*gpu, *cpu, detail)) {
                out.backend = std::move(gpu);
                out.note = "matches the cpu reference";
                return out;
            }
            note = "gpu output differs from the cpu reference (" + detail + ")";
        }
        if (config.terrainBackend == "gpu") {
            out.note = "gpu terrain requested but unavailable: " + note;
            return out;
        }
        out.backend = std::move(cpu);
        out.note = note;
        return out;
    }
}
