#include "client/selftest/Check.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"

#include <cmath>
#include <map>

namespace {
    using namespace perlin;

    bool Near(float a, float b) { return std::abs(a - b) < 0.001f; }
    bool Near(glm::vec3 a, glm::vec3 b) {
        return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
    }

    struct Rig {
        selftest::Checker& checks;
        BedrockSession session;
        CameraPose base{{100, 70, 200}, 135, 15};
        std::map<int64_t, TrackedEntity> entities;
        size_t replies = 0;
        int protocol;

        Rig(selftest::Checker& checks, int protocol) : checks(checks), protocol(protocol) {
            session.BeginReplayMode("camera-effects", 7, protocol);
            session.SetSentTap([this](uint32_t, const std::vector<uint8_t>&) { ++replies; });
        }
        ~Rig() { session.SetSentTap({}); }

        template<class Packet> std::vector<uint8_t> Body(const Packet& packet) {
            const net::context::ScopedSettings settings({protocol, -1});
            std::vector<uint8_t> result;
            checks.Check(net::EncodeChecked(packet, result), "camera fixture encodes completely in its profile");
            return result;
        }
        template<class Packet> void Inject(const Packet& packet) {
            session.InjectIncoming(Packet::kId, Body(packet));
        }
        void Tick() {
            session.Camera().Tick(base, [this](int64_t uid, TrackedEntity& found) {
                const auto it = entities.find(uid);
                if (it == entities.end())
                    return false;
                found = it->second;
                return true;
            });
        }
        void Set(int preset, std::optional<proto::Vec3F> position = {},
                 std::optional<proto::Vec2F> rotation = {}, bool defaults = false) {
            proto::CameraInstructionPacket packet;
            auto& set = packet.instructionSet.emplace();
            set.runtimeId = preset;
            set.position = position;
            set.rotation = rotation;
            if (defaults)
                set.default_ = true;
            Inject(packet);
            Tick();
        }
        void Clear(bool value = true) {
            proto::CameraInstructionPacket packet;
            packet.clear = value;
            Inject(packet);
            Tick();
        }
    };

    proto::CameraPresetsPacket Presets() {
        proto::CameraPresetsPacket packet;
        proto::CameraPresets free;
        free.name = "minecraft:free";
        free.playerEffects = false;
        free.audioListener = 0;
        packet.presets.push_back(free);
        proto::CameraPresets parent;
        parent.name = "test:parent";
        parent.parent = free.name;
        parent.position = {10.0f, 20.0f, 30.0f};
        parent.rotation = {10.0f, 20.0f};
        parent.playerEffects = false;
        parent.audioListener = 1;
        parent.horizontalRotationLimit = proto::Vec2F{180, 180};
        parent.verticalRotationLimit = proto::Vec2F{0, 180};
        parent.trackingRadius = 100.0f;
        packet.presets.push_back(parent);
        proto::CameraPresets child;
        child.name = "test:child";
        child.parent = parent.name;
        child.position.x = 99.0f;
        child.rotation.y = 45.0f;
        child.yawLimitMin = -90.0f;
        child.yawLimitMax = 90.0f;
        child.playerEffects = true;
        child.controlScheme = proto::CameraPresetsControlScheme::CameraRelativeStrafe;
        auto& assist = child.aimAssist.emplace();
        assist.presetId = "test:assist";
        assist.targetMode = proto::CameraPresetsAimAssistTargetMode::Angle;
        assist.angle = proto::Vec2F{20, 30};
        assist.distance = 12.0f;
        packet.presets.push_back(child);
        for (const char* name : {"minecraft:first_person", "minecraft:third_person",
                                 "minecraft:third_person_front"}) {
            proto::CameraPresets perspective;
            perspective.name = name;
            packet.presets.push_back(std::move(perspective));
        }
        proto::CameraPresets orbit;
        orbit.name = "minecraft:follow_orbit";
        orbit.radius = 2.0f;
        orbit.entityOffset = proto::Vec3F{0, 0, 0};
        packet.presets.push_back(std::move(orbit));
        return packet;
    }
}

namespace perlin::selftest {
    int CameraEffectsPackets() {
        Checker t("camera_effects_packets_test");
        const int previousProtocol = net::context::ProtocolSlot().load(std::memory_order_relaxed);
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            Rig rig(t, protocol);
            auto& camera = rig.session.Camera();
            const auto presets = Presets();
            rig.Inject(presets);
            const auto child = camera.PresetAt(2);
            t.Check(child && child->positionComponents[0] == 99.0f && !child->positionComponents[1] &&
                        !child->positionComponents[2] && !child->rotationComponents[0] &&
                        child->rotationComponents[1] == 45.0f,
                    "incoming presets retain partial per-axis inheritance data");
            t.Check(child && child->yawLimitMin == -90.0f && child->yawLimitMax == 90.0f &&
                        child->playerEffects == true && child->aimAssist &&
                        child->aimAssist->presetId == "test:assist" && child->aimAssist->distance == 12.0f,
                    "incoming presets retain yaw/effect settings and aim-assist metadata without claiming targeting");
            rig.Set(2);
            auto pose = camera.Pose();
            t.Check(camera.PoseActive() && Near(pose.position, {99, 20, 30}) &&
                        Near(pose.yaw, 135) && Near(pose.pitch, -10),
                    "partial child position and rotation inherit missing axes into the actual camera pose");
            t.Check(camera.PlayerEffectsEnabled() && camera.AudioAtPlayer(),
                    "explicit child effects and inherited listener reach camera consumers");
            rig.Set(1);
            t.Check(!camera.PlayerEffectsEnabled(), "an explicitly false inherited effect flag disables the consumer");
            rig.Set(2, proto::Vec3F{1, 2, 3}, proto::Vec2F{4, 5});
            t.Check(Near(camera.Pose().position, {1, 2, 3}) && Near(camera.Pose().yaw, 95) &&
                        Near(camera.Pose().pitch, -4),
                    "set position and pitch/yaw override inherited preset axes");
            rig.Clear(false);
            t.Check(camera.PoseActive(), "present clear=false does not clear a camera");
            rig.Clear();
            t.Check(!camera.PoseActive() && camera.PresetKind() == CameraEffects::Kind::None,
                    "clear=true returns control from an active preset");
            rig.Set(2);
            t.Check(Near(camera.Pose().position, {1, 2, 3}) && Near(camera.Pose().yaw, 95),
                    "selecting a preset after clear restores its persistent command overrides");
            rig.Set(2, {}, {}, true);
            t.Check(Near(camera.Pose().position, {99, 20, 30}) && Near(camera.Pose().yaw, 135),
                    "set default restores declared/inherited values rather than stale command overrides");
            rig.Set(2, proto::Vec3F{7, 8, 9}, proto::Vec2F{10, 11}, true);
            t.Check(Near(camera.Pose().position, {99, 20, 30}) && Near(camera.Pose().yaw, 135) &&
                        Near(camera.Pose().pitch, -10),
                    "default=true wins over simultaneous position and rotation in the same incoming instruction");
            rig.Set(0);
            t.Check(Near(camera.Pose().position, {0, 0, 0}) && Near(camera.Pose().yaw, 90) &&
                        Near(camera.Pose().pitch, 0),
                    "free-camera defaults are world-space zero, independent of the player's pose");
            rig.base.position += glm::vec3(40, 10, -20);
            rig.base.yaw += 50;
            rig.Tick();
            t.Check(Near(camera.Pose().position, {0, 0, 0}) && Near(camera.Pose().yaw, 90),
                    "a free camera does not follow the player between render ticks");
            for (int index = 3; index < 6; ++index) {
                rig.Set(index);
                const CameraEffects::Kind expected[]{CameraEffects::Kind::FirstPerson,
                                                     CameraEffects::Kind::ThirdPerson,
                                                     CameraEffects::Kind::ThirdPersonFront};
                t.Check(camera.PresetKind() == expected[index - 3],
                        "received builtin perspective selects the corresponding renderer camera kind");
            }
            rig.Set(0);
            proto::CameraInstructionPacket invalidSet;
            invalidSet.instructionSet.emplace().runtimeId = 999;
            auto& fov = invalidSet.fov.emplace();
            fov.fieldOfView = 90;
            fov.easeTime = 0;
            fov.easeType = "linear";
            rig.Inject(invalidSet);
            rig.Tick();
            t.Check(Near(camera.Pose().position, {0, 0, 0}) && camera.Fov(70) == 90,
                    "an invalid preset leaves the pose intact while other instruction fields still apply");
            proto::CameraInstructionPacket clearFov;
            clearFov.fov.emplace().clear = true;
            clearFov.fov->easeType = "linear";
            rig.Inject(clearFov);
            t.Check(camera.Fov(73) == 73, "explicit FOV clear restores the live base FOV");

            proto::CameraInstructionPacket malformed;
            malformed.instructionSet.emplace().runtimeId = 2;
            malformed.instructionSet->position = proto::Vec3F{7, 8, 9};
            auto bytes = rig.Body(malformed);
            bytes.resize(10);
            rig.session.InjectIncoming(malformed.kId, bytes);
            rig.Tick();
            t.Check(Near(camera.Pose().position, {0, 0, 0}),
                    "truncated set instructions cannot partially update the current pose");
            bytes = rig.Body(presets);
            if (!bytes.empty())
                bytes.pop_back();
            rig.session.InjectIncoming(presets.kId, bytes);
            t.Check(camera.PresetAt(2) && camera.PresetAt(2)->name == "test:child",
                    "truncated preset arrays preserve the last complete registry");

            rig.Set(1, proto::Vec3F{0, 0, 0}, proto::Vec2F{0, -90});
            rig.entities[-99] = TrackedEntity{{10, 0, 0}, {10, 2, 0}, 15, 5};
            proto::CameraInstructionPacket target;
            target.target.emplace().entityUniqueId = -99;
            target.target->offset = proto::Vec3F{0, 0, 0};
            rig.Inject(target);
            rig.Tick();
            t.Check(camera.TargetUid() == -99 && Near(camera.Pose().yaw, 0) && Near(camera.Pose().pitch, 0),
                    "target instructions retain signed unique IDs and affect actual camera orientation");
            proto::CameraInstructionPacket remove;
            remove.removeTarget = false;
            rig.Inject(remove);
            t.Equal(camera.TargetUid(), int64_t{-99}, "removeTarget=false does not clear a target");
            remove.removeTarget = true;
            rig.Inject(remove);
            t.Equal(camera.TargetUid(), int64_t{0}, "removeTarget=true clears the selected target");
            rig.Set(0, proto::Vec3F{1, 2, 3}, proto::Vec2F{0, -90});
            proto::CameraInstructionPacket attach;
            attach.attachToEntity = -99;
            rig.Inject(attach);
            rig.Tick();
            t.Check(camera.AttachedUid() == -99 && camera.PoseActive() &&
                        Near(camera.Pose().position, {1, 2, 3}),
                    "entity attachment does not translate an ordinary free camera");
            rig.Set(6, {}, proto::Vec2F{0, -90});
            t.Check(camera.PoseActive() && Near(camera.Pose().position, {8, 0, 0}),
                    "a subsequent boom camera consumes the retained attachment and tracked entity center");
            proto::CameraInstructionPacket detach;
            detach.detachFromEntity = false;
            rig.Inject(detach);
            t.Equal(camera.AttachedUid(), int64_t{-99}, "detach=false preserves entity attachment");
            detach.detachFromEntity = true;
            rig.Inject(detach);
            rig.Tick();
            t.Check(camera.AttachedUid() == 0 && camera.PoseActive() &&
                        Near(camera.Pose().position, rig.base.position - glm::vec3(2, 0, 0)),
                    "detach=true returns the active boom camera to the local-player reference");

            proto::CameraSplinePacket definitions;
            auto& definition = definitions.splines.emplace_back();
            definition.name = "test:path";
            definition.totalTime = 10;
            definition.splineType = "LINEAR";
            definition.controlPoints = {{5, 6, 7}, {5, 6, 7}};
            rig.Inject(definitions);
            t.Check(camera.FindSpline("test:path").has_value(), "incoming spline definitions reach the real registry");
            proto::CameraInstructionPacket spline;
            spline.spline.emplace().totalTime = 0;
            spline.spline->splineIdentifier = "test:path";
            spline.spline->loadFromJson = true;
            spline.spline->curve = {{8, 9, 10}, {8, 9, 10}};
            rig.Inject(spline);
            rig.Tick();
            t.Check(camera.PoseActive() && Near(camera.Pose().position, {5, 6, 7}),
                    "named playback recognizes case-folded types and uses registered points and duration");
            spline.spline->loadFromJson = false;
            spline.spline->totalTime = 10;
            spline.spline->hasSplineType = true;
            spline.spline->splineType = "linear";
            rig.Inject(spline);
            rig.Tick();
            t.Check(camera.PoseActive() && Near(camera.Pose().position, {8, 9, 10}),
                    "loadFromJson=false uses inline points even when the identifier matches a registered path");
            spline.spline->curve.clear();
            rig.Inject(spline);
            rig.Tick();
            t.Check(camera.PoseActive() && Near(camera.Pose().position, {8, 9, 10}),
                    "an empty inline path does not silently start the matching named definition");
            spline.spline->loadFromJson.reset();
            rig.Inject(spline);
            rig.Tick();
            t.Check(camera.PoseActive() && Near(camera.Pose().position, {8, 9, 10}),
                    "absent loadFromJson does not select named playback by identifier alone");
            spline.spline->loadFromJson = true;
            spline.spline->splineIdentifier = "test:missing";
            spline.spline->curve = {{1, 2, 3}, {1, 2, 3}};
            rig.Inject(spline);
            rig.Tick();
            t.Check(camera.PoseActive() && Near(camera.Pose().position, {8, 9, 10}),
                    "a missing named definition leaves the previous path active instead of falling back to inline");
            spline.spline->splineIdentifier = "TEST:PATH";
            rig.Inject(spline);
            rig.Tick();
            t.Check(camera.PoseActive() && Near(camera.Pose().position, {8, 9, 10}),
                    "named path identifiers remain case-sensitive when curve types are normalized");
            definition.splineType = "unknown";
            definition.controlPoints = {{1, 2, 3}, {1, 2, 3}};
            rig.Inject(definitions);
            spline.spline->splineIdentifier = "test:path";
            rig.Inject(spline);
            rig.Tick();
            t.Check(camera.PoseActive() && Near(camera.Pose().position, {8, 9, 10}),
                    "an unknown registered spline type does not alias CatmullRom or replace the active path");
            spline.spline->loadFromJson = false;
            spline.spline->splineType = "unknown";
            rig.Inject(spline);
            rig.Tick();
            t.Check(camera.PoseActive() && Near(camera.Pose().position, {8, 9, 10}),
                    "an explicit unknown inline spline type does not replace the active path");

            proto::ClientboundControlsSchemePacket control;
            control.scheme = proto::ClientboundControlsSchemePacketScheme::CameraRelative;
            rig.Inject(control);
            t.Equal(camera.ControlScheme(), 1, "received control scheme reaches the input consumer");
            t.Check(!camera.TakeControlPitchReset(), "camera-relative scheme alone requests no pitch reset");
            control.scheme = proto::ClientboundControlsSchemePacketScheme::CameraRelativeStrafe;
            for (int repeat = 0; repeat < 2; ++repeat) {
                rig.Inject(control);
                t.Check(camera.TakeControlPitchReset(),
                        "each received strafe scheme requests a pitch reset, including duplicate values");
                t.Check(!camera.TakeControlPitchReset(), "a control pitch-reset event is consumed once");
            }
            proto::CameraShakePacket shake;
            shake.action = proto::CameraShakePacketAction::Add;
            shake.type = 1;
            shake.intensity = 0.5f;
            shake.duration = 10;
            rig.Inject(shake);
            t.Check(camera.Shaking(), "camera-shake Add starts the presentation effect");
            shake.action = static_cast<proto::CameraShakePacketAction>(255);
            shake.intensity = 0;
            shake.duration = 0;
            rig.Inject(shake);
            t.Check(camera.Shaking(), "an unknown camera-shake action leaves the active effect unchanged");
            shake.action = proto::CameraShakePacketAction::Stop;
            rig.Inject(shake);
            t.Check(!camera.Shaking(), "camera-shake Stop stops the presentation effect");
            proto::CameraInstructionPacket emptyFade;
            emptyFade.fade.emplace();
            rig.Inject(emptyFade);
            t.Check(camera.Fading(), "present-empty fade starts the default fade instead of being ignored");
            camera.Reset();
            proto::CameraInstructionPacket fade;
            fade.fade.emplace().timeData = proto::CameraInstructionPacketFadeTimeData{0, 10, 0};
            fade.fade->colorRgb = proto::Vec3F{0.25f, 0.5f, 0.75f};
            rig.Inject(fade);
            rig.Tick();
            const auto tint = camera.Tint();
            t.Check(Near(tint.r, 0.25f) && Near(tint.g, 0.5f) && Near(tint.b, 0.75f) && tint.a == 1.0f,
                    "timed fade fields reach the rendered color and opacity without a sleep in every profile");
            // Seed every state store again after the isolated fade test's Reset,
            // so the session-lifecycle assertion cannot pass on empty state.
            rig.Inject(presets);
            rig.Inject(definitions);
            rig.Set(0);
            rig.Inject(target);
            rig.Inject(attach);
            rig.Inject(control);
            rig.Inject(invalidSet);
            shake.action = proto::CameraShakePacketAction::Add;
            shake.intensity = 0.5f;
            shake.duration = 10;
            rig.Inject(shake);
            t.Check(camera.PoseActive() && camera.Fading() && camera.Shaking() && camera.Fov(70) == 90 &&
                        camera.TargetUid() == -99 && camera.AttachedUid() == -99 && camera.PresetAt(0) &&
                        camera.FindSpline("test:path") && camera.ControlScheme() == 2,
                    "lifecycle fixture contains active effects, registries, target, attachment and controls");
            t.Equal(rig.replies, size_t{0}, "clientbound camera effects do not invent network replies");
            rig.session.BeginReplayMode("camera-restart", 7, protocol);
            t.Check(!camera.PoseActive() && !camera.Fading() && !camera.Shaking() && camera.Fov(70) == 70 &&
                        camera.TargetUid() == 0 && camera.AttachedUid() == 0 && !camera.PresetAt(0) &&
                        !camera.FindSpline("test:path") && camera.ControlScheme() == -1 &&
                        !camera.TakeControlPitchReset(),
                    "starting a new replay session clears prior-server camera state and registries");
        }
        {
            BedrockSession beforePlayer;
            proto::ClientboundControlsSchemePacket control;
            control.scheme = proto::ClientboundControlsSchemePacketScheme::CameraRelative;
            const net::context::ScopedSettings settings({beforePlayer.ServerProtocol(), -1});
            beforePlayer.InjectIncoming(control.kId, net::Encode(control));
            t.Equal(beforePlayer.Camera().ControlScheme(), -1,
                    "control-scheme packets require a local player before changing input behavior");
        }
        net::context::SetProtocol(previousProtocol);
        return t.Finish();
    }
}
