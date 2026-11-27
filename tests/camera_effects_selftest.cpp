#include "client/systems/camera/CameraEffects.hpp"
#include "client/systems/camera/Camera.hpp"

#include <cmath>
#include <cstdio>

namespace {
    using namespace perlin;
    int checks = 0, failures = 0;
    void Check(bool value, const char* message) {
        ++checks;
        if (!value) {
            ++failures;
            std::fprintf(stderr, "FAIL camera_effects: %s\n", message);
        }
    }
    bool Near(float actual, float expected, float epsilon = 0.0002f) {
        return std::isfinite(actual) && std::abs(actual - expected) <= epsilon;
    }
    bool At(glm::vec3 actual, glm::vec3 expected) {
        return Near(actual.x, expected.x) && Near(actual.y, expected.y) && Near(actual.z, expected.z);
    }
    CameraPreset Free(std::string name = "minecraft:free") {
        CameraPreset result;
        result.name = std::move(name);
        result.parent = result.name == "minecraft:free" ? "" : "minecraft:free";
        return result;
    }
    CameraSet SetAt(glm::vec3 position, glm::vec2 rotation = {0, 0}) {
        CameraSet result;
        result.presetIndex = 0;
        result.position = position;
        result.rotation = rotation;
        return result;
    }
    void PresetsAndPersistence() {
        double clock = 0;
        CameraEffects fx([&] { return clock; });
        auto parent = Free("example:base");
        parent.positionComponents[1] = 64;
        parent.positionComponents[2] = 3;
        parent.rotationComponents[0] = 10;
        auto child = Free("example:child");
        child.parent = parent.name;
        child.positionComponents[0] = 2;
        child.rotationComponents[1] = 90;
        fx.SetPresets({child, parent, Free()});
        CameraSet set;
        set.presetIndex = 0;
        CameraPose player{{900, 80, 900}, 45, 20};
        fx.Set(set);
        fx.Tick(player, {});
        Check(At(fx.Pose().position, {2, 64, 3}) && Near(fx.Pose().pitch, -10) && Near(fx.Pose().yaw, 180),
              "inherit each position and rotation component independently");
        player.position += glm::vec3(10);
        player.yaw += 25;
        fx.Tick(player, {});
        Check(At(fx.Pose().position, {2, 64, 3}) && Near(fx.Pose().yaw, 180),
              "a free camera does not follow player movement or rotation");
        Check(!fx.PlayerEffectsEnabled(), "free camera excludes player visual effects by default");
        Check(At(fx.PlayerPose().position, player.position), "player audio position stays distinct from free view");
        set.position = glm::vec3(100, 90, 80);
        set.rotation = glm::vec2(15, 45);
        fx.Set(set);
        fx.Tick(player, {});
        fx.Clear();
        Check(!fx.PoseActive() && fx.PresetKind() == CameraEffects::Kind::None, "clear restores normal camera");
        set.position.reset();
        set.rotation.reset();
        fx.Set(set);
        fx.Tick(player, {});
        Check(At(fx.Pose().position, {100, 90, 80}) && Near(fx.Pose().yaw, 135),
              "clear preserves per-preset position and rotation overrides");
        set.useDefault = true;
        set.position = glm::vec3(-100);
        set.rotation = glm::vec2(50);
        fx.Set(set);
        fx.Tick(player, {});
        Check(At(fx.Pose().position, {2, 64, 3}) && Near(fx.Pose().yaw, 180),
              "native default wins over position and rotation in the same instruction");
        set = {};
        set.presetIndex = 2;
        fx.Set(set);
        fx.Tick(player, {});
        Check(At(fx.Pose().position, {0, 0, 0}) && Near(fx.Pose().yaw, 90), "built-in free defaults to zero");
        set.presetIndex = 999;
        fx.Set(set);
        fx.Tick(player, {});
        Check(At(fx.Pose().position, {0, 0, 0}), "unknown runtime ID leaves the active camera intact");
        child.parent = parent.name;
        parent.parent = child.name;
        fx.SetPresets({child, parent});
        set.presetIndex = 0;
        fx.Set(set);
        fx.Tick(player, {});
        Check(At(fx.Pose().position, {0, 0, 0}), "cyclic inheritance cannot activate a partial preset");
        fx.Reset();
        Check(!fx.PoseActive() && !fx.PresetAt(0) && fx.PlayerEffectsEnabled(), "session reset clears preset state");
        fx.SetPresets({Free()});
        fx.Set(CameraSet{.presetIndex = 0});
        fx.Tick(player, {});
        Check(At(fx.Pose().position, {0, 0, 0}), "rejoining does not restore old preset overrides");
    }
    void EasingAndFov() {
        double clock = 0;
        CameraEffects fx([&] { return clock; });
        fx.SetPresets({Free()});
        CameraPose player{{0, 64, 0}, 90, 0};
        CameraPose thirdPerson{{-5, 64, 0}, 0, 0};
        auto set = SetAt({5, 64, 0});
        set.hasEase = true;
        set.easeDuration = 2;
        fx.Set(set);
        fx.Tick(player, {}, thirdPerson);
        Check(At(fx.Pose().position, {-5, 64, 0}) && Near(fx.Pose().yaw, 0),
              "easing starts at the displayed third-person view");
        clock = 1;
        fx.Tick(player, {});
        Check(At(fx.Pose().position, {0, 64, 0}) && Near(fx.Pose().yaw, 45), "linear transition midpoint");
        set.position = glm::vec3(20, 64, 0);
        fx.Set(set);
        fx.Tick(player, {});
        Check(At(fx.Pose().position, {0, 64, 0}), "interrupting an ease keeps the current displayed position");
        clock = 2;
        fx.Tick(player, {});
        Check(At(fx.Pose().position, {10, 64, 0}), "replacement ease starts from the interrupted pose");
        clock = 3;
        fx.Tick(player, {});
        Check(At(fx.Pose().position, {20, 64, 0}), "transition reaches its endpoint");

        clock = 0;
        fx.Reset();
        fx.SetFov(90, 2, 0, false);
        Check(Near(fx.Fov(70), 70), "FOV ease begins at the user's FOV");
        clock = 1;
        Check(Near(fx.Fov(70), 80), "FOV linear midpoint");
        clock = 1.5;
        fx.SetFov(110, 2, 0, false);
        Check(Near(fx.Fov(70), 85), "interrupted FOV samples current time, not the previous frame");
        clock = 2.5;
        Check(Near(fx.Fov(70), 97.5f), "replacement FOV ease retains continuity");
        fx.SetFov(0, 1, 0, true);
        clock = 3;
        Check(Near(fx.Fov(70), 83.75f), "fov_clear eases toward the configured view");
        clock = 3.5;
        Check(Near(fx.Fov(70), 70) && Near(fx.Fov(100), 100), "completed FOV clear follows subsequent settings");
        fx.SetFov(120, 0, 0, false);
        Check(Near(fx.Fov(70), 110), "native FOV target clamps at 110 degrees");
        fx.SetFov(1, 0, 0, false);
        Check(Near(fx.Fov(70), 30), "native FOV target clamps at 30 degrees");
        fx.Reset();
        Check(Near(fx.Fov(75), 75), "session reset cannot leak a completed FOV override");

        const float expected[] = {0.25f, 0.0625f, 0.4375f, 0.125f};
        const int kinds[] = {0, 2, 3, 4};
        for (size_t i = 0; i < 4; ++i) {
            clock = 0;
            fx.Reset();
            fx.SetFov(110, 1, kinds[i], false);
            fx.Fov(70);
            clock = 0.25;
            Check(Near(fx.Fov(70), 70 + 40 * expected[i]), "independent quarter-time easing sample");
        }
        for (int kind = 0; kind < 33; ++kind) {
            clock = 0;
            fx.Reset();
            fx.SetFov(90, 1, kind, false);
            Check(Near(fx.Fov(70), 70), "registered easing starts at the initial FOV");
            clock = 0.37;
            Check(std::isfinite(fx.Fov(70)), "registered easing stays finite");
            clock = 1;
            Check(Near(fx.Fov(70), 90), "registered easing ends at the requested FOV");
        }
        clock = 0;
        fx.Reset();
        fx.SetFov(110, 1, 1, false);
        fx.Fov(70);
        clock = 0.25;
        Check(Near((fx.Fov(70) - 70) / 40, 0.6633367f), "native spring quarter-time sample");
        clock = 0.5;
        Check(Near((fx.Fov(70) - 70) / 40, 1.0510158f), "native spring overshoots at its midpoint");
        clock = 0.75;
        Check(Near((fx.Fov(70) - 70) / 40, 0.9863451f), "native spring returns toward its target");
    }
    void FadeAndReset() {
        double clock = 0;
        CameraEffects fx([&] { return clock; });
        fx.Fade(2, 1, 2, {0.25f, 0.5f, 0.75f});
        Check(fx.Fading() && Near(fx.Tint().a, 0), "fade starts transparent");
        clock = 1;
        Check(Near(fx.Tint().a, 0.5f), "fade opacity advances without needing a render tick");
        fx.Fade(0.5f, 3, 0.5f, {1, 0, 0});
        clock = 1.25;
        Check(Near(fx.Tint().a, 0.75f) && At(glm::vec3(fx.Tint()), {0.25f, 0.5f, 0.75f}),
              "overlapping fade accelerates from current opacity and retains the old color");
        clock = 4;
        Check(Near(fx.Tint().a, 1), "overlapping fade holds until both fades can finish");
        clock = 4.75;
        Check(Near(fx.Tint().a, 0.5f), "overlapping fade combines its end time");
        clock = 5;
        Check(!fx.Fading() && Near(fx.Tint().a, 0), "fade ends transparent");
        fx.Fade(0, 0, 0, {0, 0, 0});
        Check(Near(fx.Tint().a, 1), "zero-time fade receives its minimum hold");
        clock = 5.49;
        Check(Near(fx.Tint().a, 1), "minimum fade duration is half a second");
        clock = 5.5;
        Check(!fx.Fading(), "minimum fade expires at the half-second boundary");
        fx.Fade(0, 0, 4, {0, 0, 1});
        clock = 7.5;
        Check(Near(fx.Tint().a, 0.5f), "old fade is halfway through fading out");
        fx.Fade(1, 0, 1, {1, 0, 0});
        clock = 8;
        Check(Near(fx.Tint().a, 0.75f), "native keyframe merge uses a full new fade-in interval during fade-out");
        clock = 8.5;
        Check(Near(fx.Tint().a, 1), "merged fade reaches full opacity at its new keyframe");
        clock = 9.5;
        Check(!fx.Fading(), "merged fade-out ends at the combined keyframe endpoint");
        fx.Fade(0, 10, 0, {1, 1, 1});
        fx.Shake(1, 10, 0);
        fx.Attach(42);
        fx.Target(43, {});
        fx.Reset();
        Check(!fx.Fading() && !fx.Shaking() && fx.AttachedUid() == 0 && fx.TargetUid() == 0 &&
                  Near(fx.Tint().a, 0), "session reset removes every transient camera effect");
        fx.Fade(0.00005f, 0.5f, 0, {0, 0, 0});
        Check(Near(fx.Tint().a, 1), "native fade evaluator uses its keyframe time epsilon");
    }
    void FocusAndOrbit() {
        double clock = 0;
        CameraEffects fx([&] { return clock; });
        auto preset = Free();
        preset.rotationSpeed = 90;
        fx.SetPresets({preset});
        fx.Set(SetAt({0, 0, 0}));
        TrackedEntity entity{{10, 0, 0}, {10, 1, 0}, 180, -20};
        EntityLookup lookup = [&](int64_t uid, TrackedEntity& result) { result = entity; return uid == 42; };
        fx.Tick({}, lookup);
        fx.Target(42, {});
        clock = 0.1;
        fx.Tick({}, lookup);
        Check(Near(fx.Pose().yaw, 81), "default 360-degree limits permit leftward focus tracking");
        clock = 0.2;
        fx.Tick({}, lookup);
        Check(Near(fx.Pose().yaw, 72), "limited-speed tracking accumulates across frames");
        preset.snapToTarget = true;
        fx.SetPresets({preset});
        fx.Set(SetAt({0, 0, 0}));
        fx.Target(42, {});
        clock = 0.3;
        fx.Tick({}, lookup);
        Check(Near(fx.Pose().yaw, 0), "snap_to_target uses the first tracking frame, not zero frame time");
        preset.verticalLimit = glm::vec2(100, 180);
        preset.rotationSpeed = 0;
        fx.SetPresets({preset});
        fx.Set(SetAt({0, 0, 0}));
        entity.center = {0, 10, 1};
        fx.Target(42, {});
        fx.Tick({}, lookup);
        Check(fx.Pose().pitch > 80, "vertical focus limits use down=0, horizon=90, up=180");
        entity.center = {2000, 0, 0};
        fx.Tick({}, lookup);
        Check(fx.TargetUid() == 0, "target beyond 1024 blocks is removed");

        CameraPreset orbit;
        orbit.name = "minecraft:follow_orbit";
        orbit.controlScheme = 1;
        fx.SetPresets({orbit});
        CameraSet set;
        set.presetIndex = 0;
        fx.Set(set);
        CameraPose player{{0, 1.62f, 0}, 90, 0};
        fx.Tick(player, lookup, {}, glm::vec3(0, 0.9f, 0));
        Check(At(fx.Pose().position, {0, 0.9f, -10}), "orbit uses the entity center and default ten-block radius");
        Check(fx.ApplyLook(90, 0), "camera-relative orbit consumes camera look independently of actor facing");
        player.position.x = 20;
        fx.Tick(player, lookup, {}, glm::vec3(20, 0.9f, 0));
        Check(At(fx.Pose().position, {30, 0.9f, 0}) && Near(fx.Pose().yaw, 180),
              "orbit rotates around the moving pivot");
        fx.SetControlScheme(2);
        Check(fx.ControlScheme() == 0, "orbit rejects unsupported camera-relative-strafe controls");
        fx.SetControlScheme(3);
        Check(fx.ApplyLook(10, 0), "player-relative orbit keeps mouse look on the camera");
        fx.SetControlScheme({});
        orbit.name = "minecraft:fixed_boom";
        orbit.rotation = glm::vec2(0, 0);
        fx.SetPresets({orbit});
        fx.Clear();
        fx.Set(set);
        fx.Tick(player, lookup, {}, glm::vec3(20, 0.9f, 0));
        const auto fixed = fx.Pose();
        Check(fx.ApplyLook(90, 30), "camera-relative fixed boom consumes actor look without orbiting");
        player.yaw = 270;
        fx.Tick(player, lookup, {}, glm::vec3(20, 0.9f, 0));
        Check(At(fx.Pose().position, fixed.position) && Near(fx.Pose().yaw, fixed.yaw),
              "fixed boom preserves orientation while the actor turns");
        fx.SetPresets({Free()});
        fx.Set(SetAt({100, 100, 100}));
        fx.Attach(42);
        fx.Tick(player, lookup);
        Check(At(fx.Pose().position, {100, 100, 100}),
              "native entity attachment does not move a stationary free camera");
        fx.SetPresets({orbit});
        fx.Set(CameraSet{.presetIndex = 0});
        fx.Tick(player, lookup);
        Check(At(fx.Pose().position, entity.center + glm::vec3(0, 0, -10)),
              "stored attachment changes the pivot of a subsequently selected boom camera");
        fx.Detach();
        fx.Tick(player, lookup, {}, glm::vec3(20, 0.9f, 0));
        Check(At(fx.Pose().position, {20, 0.9f, -10}), "detach restores the boom camera's player pivot");
    }
    void SplinesAndMatrices() {
        double clock = 0;
        CameraEffects fx([&] { return clock; });
        CameraSpline spline;
        spline.name = "example:path";
        spline.type = "linear";
        spline.totalTime = 2;
        spline.curve = {{0, 0, 0}, {20, 10, 0}};
        spline.rotation = {{{0, 350, 0}, 0, 0}, {{20, 10, 60}, 2, 0}};
        fx.SetSplines({spline});
        Check(fx.FindSpline(spline.name).has_value() && !fx.FindSpline("missing"), "named spline lookup is atomic");
        fx.StartSpline(spline);
        fx.Tick({}, {});
        clock = 1;
        fx.Tick({}, {});
        Check(At(fx.Pose().position, {10, 5, 0}) && Near(fx.Pose().pitch, -10) &&
                  Near(fx.Pose().yaw, 270) && Near(fx.Pose().roll, 30),
              "native spline rotation linearly mixes all three components, including a long yaw turn");
        clock = 2;
        fx.Tick({}, {});
        Check(At(fx.Pose().position, {20, 10, 0}), "spline reaches the final control point");
        spline.progress = {{0, 0, 2}, {1, 2, 0}};
        spline.rotation[0].ease = 2;
        clock = 3;
        fx.StartSpline(spline);
        clock = 4;
        fx.Tick({}, {});
        Check(At(fx.Pose().position, {5, 2.5f, 0}) && Near(fx.Pose().pitch, -5) &&
                  Near(fx.Pose().yaw, 355) && Near(fx.Pose().roll, 15),
              "progress and rotation segments use the preceding keyframe's easing function");
        fx.Clear();
        Check(!fx.PoseActive(), "clear cancels an animation");
        for (float pitch : {-90.0f, 90.0f}) {
            Camera camera;
            camera.pitch = pitch;
            const auto matrix = camera.ViewProjection();
            bool finite = true;
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    finite &= std::isfinite(matrix[c][r]);
            Check(finite, "exactly vertical command cameras have a finite view matrix");
        }
    }
    void CommandBounds() {
        double clock = 0;
        CameraEffects fx([&] { return clock; });
        CameraPreset boom;
        boom.name = "minecraft:fixed_boom";
        boom.rotation = glm::vec2(0);
        fx.SetPresets({boom});
        CameraSet set;
        set.presetIndex = 0;
        set.offset = glm::vec2(128, -128);
        set.entityOffset = glm::vec3(-128, 128, 128);
        fx.Set(set);
        fx.Tick({}, {}, {}, glm::vec3(0));
        Check(At(fx.Pose().position, {-128, 0, 54}), "received view and entity offsets are bounded to64 per component");
        set = CameraSet{.presetIndex = 0};
        set.removeIgnoreStartingValues = true;
        fx.Set(set);
        fx.Tick({}, {}, {}, glm::vec3(0));
        Check(At(fx.Pose().position, {-128, 0, 54}), "removeIgnoreStartingValues alone does not reset offsets");
        set.useDefault = true;
        set.removeIgnoreStartingValues = false;
        fx.Set(set);
        fx.Tick({}, {}, {}, glm::vec3(0));
        Check(At(fx.Pose().position, {-128, 0, 54}), "default without the starting-values flag preserves offset overrides");
        set.removeIgnoreStartingValues = true;
        fx.Set(set);
        fx.Tick({}, {}, {}, glm::vec3(0));
        Check(At(fx.Pose().position, {0, 0, -10}), "default with the starting-values flag restores original offsets");
        set = CameraSet{.presetIndex = 0};
        set.rotation = glm::vec2(123, 0);
        fx.Set(set);
        fx.Tick({}, {}, {}, glm::vec3(0));
        Check(Near(fx.Pose().pitch, -90), "received Set pitch is clamped to90 degrees");
        fx.SetPresets({Free()});
        fx.Set(SetAt({1, 2, 3}));
        set = CameraSet{.presetIndex = 0};
        set.offset = glm::vec2(20, 30);
        fx.Set(set);
        fx.Tick({}, {});
        Check(At(fx.Pose().position, {1, 2, 3}), "boom offset instructions do not translate a stationary free camera");
    }
}

int main() {
    PresetsAndPersistence();
    EasingAndFov();
    FadeAndReset();
    FocusAndOrbit();
    SplinesAndMatrices();
    CommandBounds();
    std::printf("camera_effects_selftest: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
