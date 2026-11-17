#pragma once
#include "client/systems/config/Options.hpp"
#include "client/systems/gfx/RenderContext.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/systems/resources/TexturePack.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/ui/TitlePanorama.hpp"

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <vector>

namespace perlin::ui {
    class PanoramaBackdrop {
    public:
        PanoramaBackdrop(const Options& options, std::unique_ptr<RenderContext>& renderer, UiAssets& assets,
                         TexturePack& texturePack, ServerPacks& serverPacks)
            : options(options), renderer(renderer), assets(assets), texturePack(texturePack),
              serverPacks(serverPacks) {}
        PanoramaBackdrop(const PanoramaBackdrop&) = delete;
        PanoramaBackdrop& operator=(const PanoramaBackdrop&) = delete;

        bool Draw(int fbW, int fbH, bool wantBlurred = false);
        bool Ready() const { return ready; }

    private:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        bool PollBlurred(int fbH, bool wait);

        const Options& options;
        std::unique_ptr<RenderContext>& renderer;
        UiAssets& assets;
        TexturePack& texturePack;
        ServerPacks& serverPacks;
        uint32_t textures[6] = {};
        uint32_t blurTextures[6] = {};
        int blurKey = -1, blurJobKey = -1;
        uint64_t blurJobStamp = 0;
        std::future<std::vector<PanoramaImage>> blurJob;
        TimePoint blurRetryAt;
        bool ready = false;
        uint64_t packStamp = 0;
        TimePoint clock, retryAt;
    };
}
