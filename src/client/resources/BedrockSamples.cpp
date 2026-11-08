#include "client/resources/BedrockSamples.hpp"
#include "client/resources/SampleDownload.hpp"
#include "client/resources/AssetJobs.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include "util/Log.hpp"

namespace fs = std::filesystem;

namespace perlin {
    namespace {
        bool Complete(const fs::path& repo) {
            return fs::exists(repo / "resource_pack/blocks.json") &&
                   fs::exists(repo / "resource_pack/textures/terrain_texture.json") &&
                   fs::exists(repo / "metadata/vanilladata_modules/mojang-blocks.json") &&
                   fs::exists(repo / "behavior_pack/blocks") &&
                   fs::exists(repo / "resource_pack/ui/hud_screen.json") &&
                   fs::exists(repo / "resource_pack/textures/ui/hotbar_0.png");
        }

        constexpr const char* kMarker = ".ready4";
    }

    bool BedrockSamples::Available(const std::string& targetDir) {
        // Extra files that failed to download are retried on the next launches (see DownloadSamples).
        std::error_code ec;
        const fs::path skipped = fs::path(targetDir) / "repo" / ".extras-skipped";
        if (fs::exists(skipped, ec)) {
            std::ifstream in(skipped);
            int n = 0;
            if (!(in >> n))
                n = 1;
            if (n < 3)
                return false;
        }
        return fs::exists(fs::path(targetDir) / kMarker) && Complete(fs::path(targetDir) / "repo");
    }

    bool BedrockSamples::Ensure(const std::string& targetDir) {
#ifdef __EMSCRIPTEN__
        assets::Job& job = assets::Samples();
        if (Complete(fs::path(targetDir) / "repo")) {
            std::ofstream((fs::path(targetDir) / kMarker).string()).close();
            job.stage = assets::Stage::Done;
            return true;
        }
        job.stage = assets::Stage::Failed;
        return false;
#else
        assets::Job& job = assets::Samples();
        if (Available(targetDir)) {
            job.stage = assets::Stage::Done;
            return true;
        }
        job.stage = assets::Stage::Checking;
        for (int attempt = 0; attempt < 3; attempt++) {
            if (DownloadSamples(targetDir) && Complete(fs::path(targetDir) / "repo")) {
                std::ofstream((fs::path(targetDir) / kMarker).string()).close();
                Log("samples", "ready at {}", (fs::path(targetDir) / "repo").string());
                job.stage = assets::Stage::Done;
                return true;
            }
            Log("samples", "download incomplete (attempt {})", attempt + 1);
        }
        job.stage = assets::Stage::Failed;
        return false;
#endif
    }
}
