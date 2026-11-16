#include "client/ui/PanoramaBackdrop.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/systems/resources/TexturePack.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <array>
#include <iterator>

namespace perlin::ui {
    bool PanoramaBackdrop::Draw(int fbW, int fbH, bool wantBlurred) {
        const auto now = Clock::now();
        const uint64_t stamp =
            (texturePack.Generation() << 32) ^ serverPacks.Generation();
        if (stamp != packStamp) {
            packStamp = stamp;
            for (uint32_t& id : textures) {
                if (id)
                    renderer->UiDestroyTexture(id);
                id = 0;
            }

            if (!blurJob.valid())
                for (uint32_t& id : blurTextures) {
                    if (id)
                        renderer->UiDestroyTexture(id);
                    id = 0;
                }
            blurKey = -1;
            ready = false;
            retryAt = {};
            blurRetryAt = {};
        }
        if (!ready && now >= retryAt) {
            retryAt = now + std::chrono::milliseconds(150);
            int loaded = 0;
            for (int i = 0; i < 6; i++) {
                if (textures[i])
                    continue;
                Texture t;
                if (assets.Image("textures/ui/panorama_" + std::to_string(i), t) &&
                    t.width > 0)
                    textures[i] =
                        renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), true);
            }
            for (uint32_t id : textures)
                loaded += id != 0;
            ready = loaded == 6;
            if (ready && !clock.time_since_epoch().count())
                clock = now;
        }
        const bool wantBlur = wantBlurred && options.menuBlur > 0;
        if (!ready && !(wantBlur && blurTextures[0]))
            return false;
        const uint32_t* faceTextures = textures;

        if (options.menuBlur > 0 && PollBlurred(fbH, wantBlur) && wantBlur)
            faceTextures = blurTextures;
        else if (!ready)
            return false;
        const double seconds = std::chrono::duration<double>(now - clock).count();
        ui::PanoramaFace faces[6];
        ui::BuildPanorama(ui::PanoramaViewAt(seconds), fbW, fbH, faces);
        std::vector<UiVertex> vertices;
        for (int i = 0; i < 6; i++) {
            const ui::PanoramaFace& f = faces[i];
            if (f.indices.empty())
                continue;
            vertices.clear();
            vertices.reserve(f.vertices.size());
            for (const ui::PanoramaVertex& v : f.vertices)
                vertices.push_back({v.x, v.y, 0.0f, 0xFFFFFFFFu, v.u, v.v});
            const uint32_t mesh =
                renderer->UiCreateFrameMesh(vertices.data(), static_cast<uint32_t>(vertices.size()),
                                            f.indices.data(), static_cast<uint32_t>(f.indices.size()));
            if (!mesh)
                continue;
            UiDraw draw;
            draw.mesh = mesh;
            draw.texture = faceTextures[i];
            draw.premultiplied = true;
            renderer->UiSubmit(UiLayer::Gui, draw);
            renderer->UiDestroyMesh(mesh);
        }
        return true;
    }

    bool PanoramaBackdrop::PollBlurred(int fbH, bool wait) {
        const auto now = Clock::now();
        const auto take = [&] {
            std::vector<ui::PanoramaImage> faces = blurJob.get();
            if (faces.size() != 6) {
                blurRetryAt = now + std::chrono::milliseconds(150);
                return;
            }
            for (int i = 0; i < 6; i++) {
                if (blurTextures[i])
                    renderer->UiDestroyTexture(blurTextures[i]);
                const ui::PanoramaImage& img = faces[static_cast<size_t>(i)];
                blurTextures[i] = renderer->UiCreateTexture(img.width, img.height, img.rgba.data(), true);
            }

            blurKey = blurJobStamp == packStamp ? blurJobKey : -1;
            Log("ui", "blurred panorama ready: {}px faces, radius {}", faces[0].width, blurJobKey);
        };
        const bool needNow = wait && !blurTextures[0];
        if (blurJob.valid() &&
            (needNow || blurJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready))
            take();

        const int radius = ui::PanoramaBlurRadius(options.menuBlur, 512, fbH);
        if (!blurJob.valid() && radius != blurKey && now >= blurRetryAt) {
            blurJobKey = radius;
            blurJobStamp = packStamp;

            std::array<ui::PanoramaImage, 6> in;
            int size = 512;
            bool complete = true;
            for (int i = 0; i < 6 && complete; i++) {
                Texture t;
                if (!assets.Image("textures/ui/panorama_" + std::to_string(i), t) || t.width <= 0) {
                    complete = false;
                    break;
                }
                size = std::min(size, std::max(t.width, t.height));
                in[static_cast<size_t>(i)].width = t.width;
                in[static_cast<size_t>(i)].height = t.height;
                in[static_cast<size_t>(i)].rgba = std::move(t.pixels);
            }
            if (!complete) {
                blurRetryAt = now + std::chrono::milliseconds(150);
                return blurTextures[0] != 0;
            }
            blurJob = std::async(std::launch::async, [radius, size, in = std::move(in)]() mutable {
                ui::PanoramaImage src[6];
                for (size_t i = 0; i < 6; i++)
                    src[i] = std::move(in[i]);
                ui::PanoramaImage out[6];
                if (!ui::BlurPanorama(src, size, std::max(1, radius * size / 512), out))
                    return std::vector<ui::PanoramaImage>{};
                return std::vector<ui::PanoramaImage>(std::make_move_iterator(std::begin(out)),
                                                      std::make_move_iterator(std::end(out)));
            });
            if (needNow)
                take();
        }
        return blurTextures[0] != 0;
    }
}
