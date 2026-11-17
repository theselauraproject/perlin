#include "client/features/worldview/GameTick.hpp"
#include "client/systems/sprites/ItemData.hpp"
#include "util/Env.hpp"

namespace perlin::worldview {
    namespace {
        float StepWeather(float level, float target) {
            if (level < target)
                return std::min(target, level + 0.01f);
            return std::max(target, level - 0.01f);
        }
    }

    GameTick::GameTick(Deps d)
        : modHost(d.modHost), input(d.input), inputManager(d.inputManager), hudState(d.hudState), handRenderer(d.handRenderer), sounds(d.sounds), camera(d.camera), player(d.player), blockLight(d.blockLight), tickDebt(d.tickDebt), rainLevel(d.rainLevel), previousRainLevel(d.previousRainLevel), thunderLevel(d.thunderLevel), previousThunderLevel(d.previousThunderLevel), loop(d.loop), tickHud(std::move(d.tickHud)) {}

    void GameTick::TickGame(FrameContext& f) {
        auto& dt = f.dt;
        auto& live = f.live;
        auto& underwater = f.underwater;
        auto& damage = loop->damage;
        auto& preview = loop->preview;
        auto& waterVisionTime = loop->waterVisionTime;
        tickDebt += dt * 20.0f;
        while (tickDebt >= 1.0) {
            tickDebt -= 1.0;
            blockLight.Tick();
            damage.Tick();
            modHost.TickMods(live);
            if (live || preview) {
                tickHud();
                HandRenderer::TickIn handIn;
                handIn.yaw = camera.yaw - 90.0f;
                handIn.pitch = -camera.pitch;
                handIn.main = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)];
                handIn.off = hudState.offhand;
                handIn.slot = std::clamp(hudState.selectedSlot, 0, 8);
                if (live) {
                    int fast = -1, slow = -1;
                    for (const auto& fx : live->GetEffects()) {
                        if (fx.id == 3 || fx.id == 26)
                            fast = std::max(fast, fx.amplifier);
                        else if (fx.id == 4)
                            slow = std::max(slow, fx.amplifier);
                    }
                    handIn.swingTicks = hand::SwingTicks(6, fast, slow);
                }
                handIn.survival = hudState.Survival();
                handIn.sneaking = player.Sneaking();
                handIn.useHeld = ((input.Captured() && inputManager.IsDown(Action::Use)) ||
                                  PERLIN_ENV("PERLIN_PREVIEW_USE") != nullptr) &&
                                 detail::HasAmmo(hudState, live, handIn.main.name);
                {
                    const std::string& mn = handIn.main.name;
                    const bool drink = mn == "minecraft:potion" || mn == "minecraft:milk_bucket" ||
                                       mn == "minecraft:honey_bottle" || mn == "minecraft:ominous_bottle";
                    const bool always =
                        mn == "minecraft:golden_apple" || mn == "minecraft:enchanted_golden_apple" ||
                        mn == "minecraft:chorus_fruit" || drink || mn == "minecraft:suspicious_stew";
                    handIn.eating = handIn.survival && handIn.useHeld && ui::IsFood(mn);
                    (void)always;
                }
                handRenderer.Tick(handIn, modHost.ModHand());
                {
                    const int et = handRenderer.EatTicks();
                    if (et >= 7 && et % 4 == 3) {
                        const std::string& mn = handIn.main.name;
                        const bool drink = mn == "minecraft:potion" || mn == "minecraft:milk_bucket" ||
                                           mn == "minecraft:honey_bottle" || mn == "minecraft:ominous_bottle";
                        sounds.PlayEntityEvent("minecraft:player", drink ? "drink" : "eat",
                                               glm::dvec3(camera.x, camera.y, camera.z));
                    }
                }
            }
            waterVisionTime =
                underwater ? std::min(600, waterVisionTime + 1) : std::max(0, waterVisionTime - 10);
            if (live) {
                previousRainLevel = rainLevel;
                previousThunderLevel = thunderLevel;
                rainLevel = StepWeather(rainLevel, live->RainTarget());
                thunderLevel = StepWeather(thunderLevel, live->ThunderTarget());
            }
        }
    }
}
