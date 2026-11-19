#include "client/systems/multiplayer/handlers/HandlerSupport.hpp"

namespace perlin {
    using namespace proto;
    using namespace handlers;

    void BedrockSession::RegisterLocalPlayerHandlers() {
        On<MovePlayerPacket>([this](const MovePlayerPacket& p) {
            if (runtimeId != 0 && p.runtimeId != runtimeId)
                return;
            runtimeId = p.runtimeId;
            const Vec3F& eye = p.position;
            if (!SaneWorldPos(eye.x, eye.y, eye.z))
                return;
            pose = {eye.x, eye.y - kPlayerEyeHeight, eye.z, p.yaw, p.pitch, p.headYaw, p.onGround, true};
            haveLastAuthPos = false;
            gotSpawn = true;
            const bool forced =
                p.mode == MovePlayerPacketMode::Teleport || p.mode == MovePlayerPacketMode::Reset;
            // vanilla places the local player on a normal move too (some servers teleport pearls that way),
            // only without the teleport acknowledgement
            if (forced || p.mode == MovePlayerPacketMode::Normal) {
                if (forced)
                    pendingTeleportAck = true;
                ServerMotion motion;
                motion.poseChanged = true;
                motion.teleport = true;
                motion.hasRotation = true;
                motion.filterTravel = p.mode == MovePlayerPacketMode::Teleport;
                motion.tick = p.tick;
                motion.onGround = p.onGround;
                motion.yaw = p.yaw;
                motion.pitch = p.pitch;
                motion.hasVelocity = true;
                QueueServerMotion(motion);
            }
        });

        On<DeathInfoPacket>([this](const DeathInfoPacket& p) {
            const std::string text = ui::FormatTextPacket(2, true, std::string(), p.cause, p.messages);
            Log("bedrock", "death_info {} -> {}", p.cause, text);
            std::lock_guard<std::mutex> lk(deathMutex);
            deathCause = text;
            deathPending = true;
        });

        On<RespawnPacket>([this](const RespawnPacket& p) {
            Log("bedrock", "Respawn pos=({},{},{}) state={}", p.position.x, p.position.y, p.position.z,
                static_cast<int>(p.state));
            if (p.state != 1)
                return;
            {
                std::lock_guard<std::mutex> lk(deathMutex);
                deathCause.clear();
                deathPending = false;
            }
            if (SaneWorldPos(p.position.x, p.position.y, p.position.z)) {
                pose = {p.position.x, p.position.y - kPlayerEyeHeight,
                        p.position.z, pose.yaw,
                        pose.pitch,   pose.headYaw,
                        false,        true};
                gotSpawn = true;
                haveLastAuthPos = false;
                ServerMotion motion;
                motion.poseChanged = true;
                motion.teleport = true;
                motion.hasVelocity = true;
                QueueServerMotion(motion);
            }
            proto::PlayerActionPacket done;
            done.runtimeEntityId = runtimeId;
            done.action = static_cast<proto::Action>(7);
            done.position = {0, 0, 0};
            done.resultPosition = {0, 0, 0};
            done.face = -1;
            Log("bedrock", "respawn ready -> PlayerAction Respawn");
            Send(done);
        });

        On<CorrectPlayerMovePredictionPacket>([this](const CorrectPlayerMovePredictionPacket& p) {
            if (p.predictionType == CorrectPlayerMovePredictionPacketPredictionType::Vehicle) {
                if (!SaneWorldPos(p.position.x, p.position.y, p.position.z))
                    return;
                VehicleCorrection c;
                c.tick = p.tick;
                c.pos[0] = p.position.x;
                c.pos[1] = p.position.y;
                c.pos[2] = p.position.z;
                c.delta[0] = p.delta.x;
                c.delta[1] = p.delta.y;
                c.delta[2] = p.delta.z;
                c.rotation[0] = p.rotation.x;
                c.rotation[1] = p.rotation.z;
                c.angularVelocity = p.angularVelocity;
                c.onGround = p.onGround;
                std::lock_guard lk(vehicleCorrectionMutex);
                vehicleCorrections.push_back(c);
                while (vehicleCorrections.size() > 64)
                    vehicleCorrections.pop_front();
                return;
            }
            if (p.predictionType != CorrectPlayerMovePredictionPacketPredictionType::Player)
                return;
            if (!SaneWorldPos(p.position.x, p.position.y, p.position.z))
                return;
            pose.x = p.position.x;
            pose.y = p.position.y - kPlayerEyeHeight;
            pose.z = p.position.z;
            pose.onGround = p.onGround;
            haveLastAuthPos = false;
            ArmPredictionSync();
            if (correctionsReceived++ < 5 || correctionsReceived % 100 == 0)
                Log("move", "server correction #{} tick={} pos=({},{},{}) delta=({},{},{})",
                    correctionsReceived.load(), p.tick, p.position.x, p.position.y, p.position.z, p.delta.x,
                    p.delta.y, p.delta.z);
            if (PERLIN_ENV("PERLIN_BLOCK_DEBUG"))
                Log("move", "correction_received #{} tick={} afterAuthTick={}", correctionsReceived.load(),
                    p.tick, LastAuthTick());
            ServerMotion motion;
            motion.poseChanged = true;
            motion.correction = true;
            motion.tick = p.tick;
            motion.onGround = p.onGround;
            motion.hasVelocity = true;
            motion.velocity[0] = p.delta.x;
            motion.velocity[1] = p.delta.y;
            motion.velocity[2] = p.delta.z;
            QueueServerMotion(motion);
        });

        On<SetEntityMotionPacket>([this](const SetEntityMotionPacket& p) {
            if (runtimeId == 0 || p.runtimeEntityId != runtimeId)
                return;
            ServerMotion motion;
            motion.hasVelocity = true;
            motion.stampedVelocity = p.tick != 0;
            motion.tick = p.tick;
            motion.velocity[0] = p.velocity.x;
            motion.velocity[1] = p.velocity.y;
            motion.velocity[2] = p.velocity.z;
            QueueServerMotion(motion);
            if (PERLIN_ENV("PERLIN_BLOCK_DEBUG"))
                Log("move", "motion_received tick={} afterAuthTick={} velocity=({},{},{})", p.tick,
                    LastAuthTick(), p.velocity.x, p.velocity.y, p.velocity.z);
            NoteKnockback(p.velocity.x, p.velocity.z);
        });
    }
}
