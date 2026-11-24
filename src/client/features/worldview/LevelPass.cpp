#include "client/features/worldview/LevelPass.hpp"
#include "client/systems/diagnostics/FrameProfiler.hpp"
#include "util/Clock.hpp"
#include "util/Log.hpp"
#include "util/Env.hpp"

namespace perlin::worldview {
    using detail::Clock;

    LevelPass::LevelPass(Deps d)
        : uiAssets(d.uiAssets), modHost(d.modHost), replayStudio(d.replayStudio), options(d.options), renderer(d.renderer), levelRenderer(d.levelRenderer), hudState(d.hudState), handRenderer(d.handRenderer), entityRenderer(d.entityRenderer), fgPacer(d.fgPacer), camera(d.camera), player(d.player), cameraDetached(d.cameraDetached), loop(d.loop), previewEntitiesSpawned(d.previewEntitiesSpawned), replayCtl(d.replayCtl), session(d.session), deathOpen(std::move(d.deathOpen)), requestQuit(std::move(d.requestQuit)) {}

    void LevelPass::RenderLevel(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& live = f.live;
        auto& frustum = f.frustum;
        auto& params = f.params;
        auto& previewLevel = loop->previewLevel;
        auto& previewShot = loop->previewShot;
        auto& previewWait = loop->previewWait;
        auto& previewShotFrames = loop->previewShotFrames;
        auto& previewReadyAt = loop->previewReadyAt;
        auto& previewLastMeshed = loop->previewLastMeshed;
        auto& frames = loop->frames;
        frame::Profiler::Get().AddToAverage(
            frame::Profiler::Logic,
            std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count());
        fgPacer.PreRenderMs(std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count());
        if (fgPacer.Pending() && !replayStudio.Exporting())
            frames += fgPacer.Pump(FrameGenPacer::Stage::BeforeRender, !options.vsync, Clock::now(),
                                   [&](int i, int n, double phase) { return renderer->PresentGenerated(i, n, phase); });
        renderer->BeginFrame(params);
        if (live) {
            {
                frame::ProfileScope p(frame::Profiler::LevelUpdate);
                levelRenderer->Update(&live->NetLevel(), camera.x, camera.z, frustum);
            }
            {
                frame::ProfileScope p(frame::Profiler::LevelRender);
                levelRenderer->Render(frustum, camera.x, camera.z);
            }
        } else if (previewLevel) {
            {
                frame::ProfileScope p(frame::Profiler::LevelUpdate);
                levelRenderer->Update(previewLevel.get(), camera.x, camera.z, frustum);
            }
            {
                frame::ProfileScope p(frame::Profiler::LevelRender);
                levelRenderer->Render(frustum, camera.x, camera.z);
            }
            if (!previewShot.empty() && previewShotFrames < 0) {
                const size_t meshed = levelRenderer->MeshedChunks();
                if (meshed != previewLastMeshed) {
                    previewLastMeshed = meshed;
                    previewReadyAt = frameStart;
                }
                if (std::chrono::duration<double>(frameStart - previewReadyAt).count() > previewWait) {
                    renderer->RequestScreenshot(previewShot);
                    previewShotFrames = 0;
                }
            } else if (previewShotFrames >= 0 && ++previewShotFrames > 6) {
                Log("preview", "screenshot saved, meshed chunks {}", levelRenderer->MeshedChunks());
                MeshProfilePrint();
                levelRenderer->PrintLoadProfile();
                if (const char* bench = PERLIN_ENV("PERLIN_MESHBENCH"))
                    levelRenderer->BenchmarkMeshing(*previewLevel, std::atoi(bench));
                requestQuit();
            }
        }
    }

    void LevelPass::HandleLiveScreenshot(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& live = f.live;
        auto& startedAt = loop->startedAt;
        if (!live)
            return;
        static const char* shot = PERLIN_ENV("PERLIN_LIVE_SHOT");
        static const char* swingAt = PERLIN_ENV("PERLIN_LIVE_SWING");
        const double t = std::chrono::duration<double>(frameStart - startedAt).count();
        if (swingAt && !swung && t > std::atof(swingAt)) {
            swung = true;
            handRenderer.Attack(modHost.ModHand(), true);
            live->SendSwingArm("attack");
        }
        if (shot) {
            const std::string spec = shot;
            const size_t at = spec.rfind('@');
            const double when = at == std::string::npos ? 20.0 : std::atof(spec.c_str() + at + 1);
            if (!shotDone && t > when) {
                shotDone = true;
                renderer->RequestScreenshot(spec.substr(0, at));
            } else if (shotDone && ++shotFrames > 8) {
                requestQuit();
            }
        }
    }

    void LevelPass::RenderHand(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& dt = f.dt;
        auto& live = f.live;
        auto& worldTicks = f.worldTicks;
        auto& partial = f.partial;
        auto& handViewEffect = f.handViewEffect;
        auto& dyn = f.dyn;
        auto& params = f.params;
        auto& fpSelf = f.fpSelf;
        auto& startedAt = loop->startedAt;
        auto& damage = loop->damage;
        auto& preview = loop->preview;
        auto& handSky = loop->handSky;
        auto& handBlock = loop->handBlock;
        if ((live || preview) &&
            (!replayCtl.Playing() || replayCtl.cameraMode == replay::CameraMode::Follow) &&
            camera.mode == PerspectiveMode::FirstPerson && !cameraDetached && options.showHud &&
            hudState.mode != ui::GameMode::Spectator && !deathOpen() &&
            !uiAssets.VanillaRoot().empty()) {
            HandRenderer::FrameIn handFrame;
            handFrame.partial = partial;
            handFrame.yaw = camera.yaw - 90.0f;
            handFrame.pitch = -camera.pitch;
            {
                const int sky = std::clamp(handSky, 0, 15);
                int blk = std::clamp(handBlock, 0, 15);
                if (!dyn.empty())
                    blk = std::max(blk, static_cast<int>(dynlight::LevelAt(
                                            dyn, glm::dvec3(camera.x, camera.y, camera.z))));
                const uint8_t* px = params.lightmap + (static_cast<size_t>(sky) * kLightmapSize + blk) * 4;
                handFrame.light = {px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f};
            }
            handFrame.viewEffect = handViewEffect;
            handFrame.skins3d = options.skins3d;
            handFrame.hurt = damage.HurtFraction(partial);
            const LoadedSkin* handSkin = session ? &session->Skin() : nullptr;
            if (session && replayCtl.Playing()) {
                for (const auto& tp : session->PlayerList()) {
                    if (!tp.skin || tp.skin->skinRgba.empty() || tp.name != replayCtl.Meta().playerName)
                        continue;
                    if (replayHandSerial != tp.skin->serial) {
                        replayHandSerial = tp.skin->serial;
                        replayHandSkin = LoadedSkin{};
                        replayHandSkin.skinWidth = tp.skin->skinWidth;
                        replayHandSkin.skinHeight = tp.skin->skinHeight;
                        replayHandSkin.skinRgba = tp.skin->skinRgba;
                        replayHandSkin.slimArms = tp.skin->slim;
                        replayHandSkin.skinId = "replay-" + std::to_string(tp.skin->serial);
                    }
                    handSkin = &replayHandSkin;
                    break;
                }
            }
            if (session && handSkin) {
                handFrame.skin = handSkin;
                handFrame.skinSerial =
                    std::hash<std::string>{}(handSkin->skinId) * 2 + (handSkin->slimArms ? 1 : 0);
                handFrame.slimArm = handSkin->slimArms;
                handFrame.leftSleeve = options.skinParts.leftSleeve;
                handFrame.rightSleeve = options.skinParts.rightSleeve;
            }
            {
                frame::ProfileScope p(frame::Profiler::Hand);
                handRenderer.Render(handFrame, modHost.ModHand());
            }
            {
                int sw = 0, sh = 0;
                renderer->FramebufferSize(sw, sh);
                handRenderer.RenderScope(sw, sh, partial);
            }
            if (modHost.ModHand().bedrockRig && PERLIN_ENV("PERLIN_BEDROCK_RIG") &&
                (fpSelf || previewEntitiesSpawned || true)) {
                if (!fpSelf) {
                    fpSelf.emplace();
                    fpSelf->runtimeId = 0xFFFFFFFFFFFF0001ull;
                    fpSelf->type = "minecraft:player";
                    fpSelf->isPlayer = fpSelf->isSelf = true;
                    fpSelf->pos = {camera.x, camera.y - static_cast<double>(player.EyeOffset()), camera.z};
                    fpSelf->yaw = fpSelf->headYaw = camera.yaw - 90.0f;
                    fpSelf->pitch = -camera.pitch;
                    fpSelf->moveSerial = 1;
                }
                const HandRenderer::RigState rs = handRenderer.Rig(partial);
                fpSelf->mainHand = rs.main;
                fpSelf->offHand = modHost.ModHand().showOffhand ? rs.off : std::string();
                if (modHost.ModHand().leftHanded)
                    std::swap(fpSelf->mainHand, fpSelf->offHand);
                entity::EntityRenderer::FrameIn fin;
                fin.time = std::chrono::duration<double>(frameStart - startedAt).count();
                fin.dt = dt;
                fin.camera = {camera.x, camera.y, camera.z};
                fin.firstPerson = true;
                fin.cameraYaw = camera.yaw - 90.0f;
                fin.cameraPitch = -camera.pitch;
                fin.worldTicks = worldTicks;
                entity::EntityRenderer::FpState fps;
                fps.attackTime = rs.attackTime;
                fps.armHeight = rs.armHeight;
                fps.offArmHeight = rs.offArmHeight;
                fps.bob = modHost.ModHand().viewBobbing && modHost.ModHand().bobHand;
                fps.using_ = rs.using_;
                fps.blocking = rs.blocking;
                fps.usedTicks = rs.usedTicks;
                fps.viewEffect = handFrame.viewEffect;
                fps.light = handFrame.light;
                entityRenderer.RenderFirstPerson(*fpSelf, fin, fps);
            }
        }
    }
}
