#pragma once

#include "client/systems/player/LocalPlayer.hpp"

namespace perlin {
    struct MovementProtocolTestAccess {
        static void Initialize(BedrockSession& session) { session.initializedSent = true; }
        static void SettleColumn(BedrockSession& session, int x, int z) {
            std::lock_guard lock(session.reqMutex);
            session.seenColumns.insert({x, z});
            session.colPending.erase({x, z});
        }
        static void ReplayTransport(BedrockSession& session, bool available) { session.replayMode = available; }
        static void Reset(BedrockSession& session) { session.ResetMovementProtocolState(); }
        static bool HasSentPosition(const BedrockSession& session) { return session.haveLastAuthPos; }
        static bool SendInitialized(BedrockSession& session) { return session.SendSetLocalPlayerAsInitialized(); }
        static void Tick(LocalPlayer& player, BedrockSession& session, const InputManager& input,
                         const Camera& camera, const physics::World& world) {
            player.Tick(session, input, camera, world);
        }
    };
}
