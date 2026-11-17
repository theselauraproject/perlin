#include "client/features/worldview/ViewEnvironment.hpp"
#include "client/systems/player/BlockCollision.hpp"
#include "util/Log.hpp"
#include "util/Env.hpp"
#include "client/systems/preview/PreviewWorld.hpp"

namespace perlin::worldview {
    namespace {
        float SegmentBoxEntry(const glm::vec3& a, const glm::vec3& b, const glm::vec3& lo,
                              const glm::vec3& hi) {
            float enter = 0.0f, leave = 1.0f;
            for (int i = 0; i < 3; ++i) {
                const float d = b[i] - a[i];
                if (d == 0.0f) {
                    if (a[i] < lo[i] || a[i] > hi[i])
                        return 2.0f;
                    continue;
                }
                float t0 = (lo[i] - a[i]) / d, t1 = (hi[i] - a[i]) / d;
                if (t0 > t1)
                    std::swap(t0, t1);
                enter = std::max(enter, t0);
                leave = std::min(leave, t1);
                if (enter > leave)
                    return 2.0f;
            }
            return enter;
        }

        std::optional<glm::vec3> CameraClip(const BlockPalette& palette, const Level& level, const LevelCollision& col, glm::vec3 from,
                                            glm::vec3 to) {
            glm::ivec3 cell(static_cast<int>(std::floor(from.x)), static_cast<int>(std::floor(from.y)),
                            static_cast<int>(std::floor(from.z)));
            const glm::ivec3 last(static_cast<int>(std::floor(to.x)), static_cast<int>(std::floor(to.y)),
                                  static_cast<int>(std::floor(to.z)));
            const glm::vec3 d = to - from;
            glm::ivec3 step;
            glm::vec3 next, delta;
            for (int i = 0; i < 3; ++i) {
                step[i] = d[i] > 0.0f ? 1 : (d[i] < 0.0f ? -1 : 0);
                delta[i] = step[i] != 0 ? std::abs(1.0f / d[i]) : std::numeric_limits<float>::infinity();
                const float edge =
                    step[i] > 0 ? static_cast<float>(cell[i] + 1) : static_cast<float>(cell[i]);
                next[i] = step[i] != 0 ? (edge - from[i]) / d[i] : std::numeric_limits<float>::infinity();
            }
            for (int visited = 0; visited < 200; ++visited) {
                physics::BlockShape shape = col.Query(cell.x, cell.y, cell.z);
                bool loaded = false;
                const std::string& name =
                    palette.Name(level.GetBlock(cell.x, cell.y, cell.z, loaded));
                if (name.ends_with("copper_grate"))
                    shape.boxes.clear();
                else if (name == "minecraft:soul_sand" || name == "minecraft:mud")
                    shape.boxes = {{0, 0, 0, 1, 1, 1}};
                float best = 2.0f;
                for (const physics::Box& b : shape.boxes) {
                    const glm::vec3 lo(static_cast<float>(cell.x + b.x0), static_cast<float>(cell.y + b.y0),
                                       static_cast<float>(cell.z + b.z0));
                    const glm::vec3 hi(static_cast<float>(cell.x + b.x1), static_cast<float>(cell.y + b.y1),
                                       static_cast<float>(cell.z + b.z1));
                    best = std::min(best, SegmentBoxEntry(from, to, lo, hi));
                }
                if (best <= 1.0f)
                    return from + d * best;
                if (cell == last)
                    break;
                const int axis = next.x < next.y ? (next.x < next.z ? 0 : 2) : (next.y < next.z ? 1 : 2);
                if (next[axis] > 1.0f)
                    break;
                cell[axis] += step[axis];
                next[axis] += delta[axis];
            }
            return std::nullopt;
        }
    }


    ViewEnvironment::ViewEnvironment(Deps d)
        : modHost(d.modHost), blockPalette(d.blockPalette), lightCache(d.lightCache), replayStudio(d.replayStudio),
          levelRenderer(d.levelRenderer), camera(d.camera), loop(d.loop), debugOverlay(d.debugOverlay),
          replayCtl(d.replayCtl), session(d.session), dimensionChanged(std::move(d.dimensionChanged)) {}

    void ViewEnvironment::UpdatePreviewWorld(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& live = f.live;
        auto& worldTicks = f.worldTicks;
        auto& preview = loop->preview;
        auto& previewLevel = loop->previewLevel;
        auto& previewTicks = loop->previewTicks;
        auto& previewReadyAt = loop->previewReadyAt;
        worldTicks = preview ? previewTicks : frame::kNoonTick;
        if (live)
            live->GetWorldTicks(worldTicks);
        if (replayCtl.Playing()) {
            float forcedTicks = 0;
            if (replayStudio.TimeOfDayAt(replayCtl.TimeMs(), forcedTicks))
                worldTicks = forcedTicks;
        }
        modHost.ModClientTime(worldTicks);
        if (preview && !previewLevel) {
        if (lastNote == std::chrono::steady_clock::time_point{})
                lastNote = frameStart;
            if (frameStart - lastNote > std::chrono::seconds(3)) {
                lastNote = frameStart;
                Log("preview", "waiting: ready={} complete={}", levelRenderer->Blocks().Ready(),
                    levelRenderer->Blocks().Complete());
            }
        }
        if (preview && !previewLevel && levelRenderer->Blocks().Ready()) {
            auto built = std::make_unique<Level>();
            if (BuildPreviewWorld(*built, 7)) {
                previewLevel = std::move(built);
                previewReadyAt = frameStart;
                Log("preview", "world built");
            }
        }

        if (preview && previewLevel && PERLIN_ENV("PERLIN_PREVIEW_EDIT")) {
            if (!edited && frameStart - previewReadyAt > std::chrono::seconds(4)) {
                edited = true;
                const std::string list = PERLIN_ENV("PERLIN_PREVIEW_EDIT");
                size_t start = 0;
                while (start < list.size()) {
                    size_t end = list.find(';', start);
                    if (end == std::string::npos)
                        end = list.size();
                    int dx = 0, dy = 0, dz = 0;
                    char name[128] = {0};
                    if (std::sscanf(list.substr(start, end - start).c_str(), "%d,%d,%d,%127s", &dx, &dy, &dz,
                                    name) == 4) {
                        BlockPalette& palette = blockPalette;
                        std::string full = name;
                        std::vector<std::pair<std::string, BlockPalette::StateValue>> states;
                        if (const size_t lb = full.find('['); lb != std::string::npos) {
                            std::string body = full.substr(lb + 1, full.size() - lb - 2);
                            full = full.substr(0, lb);
                            size_t q = 0;
                            while (q < body.size()) {
                                size_t c = body.find(',', q);
                                if (c == std::string::npos)
                                    c = body.size();
                                const std::string kv = body.substr(q, c - q);
                                const size_t eq = kv.find('=');
                                if (eq != std::string::npos) {
                                    BlockPalette::StateValue sv;
                                    const std::string val = kv.substr(eq + 1);
                                    if (val == "true" || val == "false") {
                                        sv.kind = BlockPalette::StateValue::Bool;
                                        sv.i = val == "true";
                                    } else if (!val.empty() &&
                                               (std::isdigit(static_cast<unsigned char>(val[0])) ||
                                                val[0] == '-')) {
                                        sv.kind = BlockPalette::StateValue::Int;
                                        sv.i = std::atoi(val.c_str());
                                    } else {
                                        sv.kind = BlockPalette::StateValue::String;
                                        sv.s = val;
                                    }
                                    states.emplace_back(kv.substr(0, eq), sv);
                                }
                                q = c + 1;
                            }
                            std::ranges::sort(states,
                                              [](const auto& a, const auto& b) { return a.first < b.first; });
                        }
                        const uint16_t id = palette.Intern(BlockPalette::HashState(full, states));
                        previewLevel->SetBlock(static_cast<int>(std::floor(camera.x)) + dx,
                                               static_cast<int>(std::floor(camera.y)) + dy,
                                               static_cast<int>(std::floor(camera.z)) + dz, id);
                    }
                    start = end + 1;
                }
                Log("preview", "edits applied");
            }
        }
    }

    void ViewEnvironment::UpdateThirdPersonDistance(BedrockSession* live, float dt) {
        constexpr float desired = 4.0f;
        if (camera.mode == PerspectiveMode::FirstPerson) {
            camera.thirdPersonDistance = -1.0f;
            camera.avoidanceVelocity = 0.0f;
            return;
        }
        float target = desired;
        if (live) {
            const LevelCollision col(live->NetLevel(), &levelRenderer->Blocks());
            const glm::vec3 pivot(camera.x, camera.y, camera.z);
            const glm::vec3 dir =
                camera.mode == PerspectiveMode::ThirdPersonBack ? -camera.Direction() : camera.Direction();
            const glm::vec3 wanted = pivot + dir * desired;
            const float nearClip = camera.nearPlane;
            float best = std::numeric_limits<float>::max();
            for (int i = 0; i < 8; ++i) {
                const glm::vec3 o(static_cast<float>(((2 * i) & 2) - 1) * 0.1f,
                                  static_cast<float>((i & 2) - 1) * 0.1f,
                                  static_cast<float>(((i >> 1) & 2) - 1) * 0.1f);
                if (const auto hit = CameraClip(blockPalette, live->NetLevel(), col, pivot + o, wanted + o))
                    best = std::min(std::max(nearClip, glm::length(pivot - *hit) - nearClip), best);
            }
            target = desired < best ? desired : std::max(best, 0.0f);
        }
        camera.thirdPersonDistance = target;
        camera.avoidanceVelocity = 0.0f;
        if (target >= 0.0f)
            return;
        if (camera.thirdPersonDistance > target) {
            camera.thirdPersonDistance = target;
            camera.avoidanceVelocity = 0.0f;
        }
        constexpr float k = 4.0f;
        const float x0 = camera.thirdPersonDistance - target;
        const float a = x0 * k;
        const float b = camera.avoidanceVelocity + a;
        const float c = -k * dt;
        const float e = std::exp(c);
        camera.avoidanceVelocity = ((c * b + b) - a) * e;
        camera.thirdPersonDistance = ((dt * b) + x0) * e + target;
    }

    void ViewEnvironment::SampleEyeEnvironment(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& live = f.live;
        auto& underwater = f.underwater;
        auto& eyeInLava = f.eyeInLava;
        auto& eyeBiome = f.eyeBiome;
        auto& previewLevel = loop->previewLevel;
        auto& handLightAt = loop->handLightAt;
        auto& handSky = loop->handSky;
        auto& handBlock = loop->handBlock;
        bool inPortalNow = false;
        if (live) {
            const glm::vec3 e = camera.EyePosition();
            const int bx = static_cast<int>(std::floor(e.x)), by = static_cast<int>(std::floor(e.y)),
                      bz = static_cast<int>(std::floor(e.z));
            const LevelCollision col(live->NetLevel(), &levelRenderer->Blocks());
            const physics::BlockShape cell = col.Query(bx, by, bz);
            if (cell.fluid == 1) {
                const float top = col.Query(bx, by + 1, bz).fluid == 1 ? 1.0f : cell.fluidLevel;
                underwater = e.y < static_cast<float>(by) + top;
                int k = 0;
                while (k < 64 && col.Query(bx, by + k + 1, bz).fluid == 1)
                    k++;
                const physics::BlockShape topCell = col.Query(bx, by + k, bz);
                waterSurfaceAbove = static_cast<float>(by + k) + (k == 0 ? top : topCell.fluidLevel) - e.y;
            }
            if (cell.fluid == 2) {
                const float top = col.Query(bx, by + 1, bz).fluid == 2 ? 1.0f : cell.fluidLevel;
                eyeInLava = e.y < static_cast<float>(by) + top;
            }
            eyeBiome = levelRenderer->Tints().NameForId(live->NetLevel().GetBiome(bx, by, bz));
            {
                const int fy = static_cast<int>(std::floor(camera.y - 1.62f));
                const int fx = static_cast<int>(std::floor(e.x)), fz = static_cast<int>(std::floor(e.z));
                inPortalNow = cell.portal || col.Query(fx, fy, fz).portal;
            }
        }

        headUnderwater = underwater;
        headInLava = eyeInLava;
        {
            const auto nowPortal = std::chrono::steady_clock::now();
        if (lastPortalAt == std::chrono::steady_clock::time_point{})
                lastPortalAt = nowPortal;
            const float portalDt =
                std::min(0.1f, std::chrono::duration<float>(nowPortal - lastPortalAt).count());
            lastPortalAt = nowPortal;
            if (inPortalNow && (live || previewLevel))
                portalTime = std::min(1.0f, portalTime + portalDt * 0.25f);
            else
                portalTime = std::max(0.0f, portalTime - portalDt);
        }
        if (live && session) {
            const int dim = live->DimensionId();
            if (lastDimId != -1 && dim != lastDimId) {
                dimensionChanged();
            }
            lastDimId = dim;
        }
        if (!underwater)
            waterSurfaceAbove = 0.0f;
        if ((live || previewLevel) && frameStart - handLightAt > std::chrono::milliseconds(200)) {
            handLightAt = frameStart;
            const glm::vec3 e = camera.EyePosition();
            const int bx = static_cast<int>(std::floor(e.x)), by = static_cast<int>(std::floor(e.y)),
                      bz = static_cast<int>(std::floor(e.z));
            Level& lightLevel = live ? live->NetLevel() : *previewLevel;
            const uint8_t packed = lightCache.At(lightLevel, bx, by, bz, frameStart);
            handSky = ChunkLighting::Result::Sky(packed);
            handBlock = ChunkLighting::Result::Block(packed);
            debugOverlay.state.sky = handSky;
            debugOverlay.state.block = handBlock;
        }
    }
}
