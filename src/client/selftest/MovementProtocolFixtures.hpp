#pragma once

#include "client/systems/player/LocalPlayer.hpp"

namespace perlin {
    struct MovementProtocolTestAccess {
        static void Initialize(BedrockSession& session) { session.initializedSent = true; }
        static void SetUniqueId(BedrockSession& session, int64_t id) { session.uniqueId = id; }
        static void ResetPlayerState(BedrockSession& session) { session.ResetPlayerState(); }
        static void SetAuthTick(BedrockSession& session, uint64_t tick) { session.authTick = tick; }
        static void SettleColumn(BedrockSession& session, int x, int z) {
            std::lock_guard lock(session.reqMutex);
            session.seenColumns.insert({x, z});
            session.colPending.erase({x, z});
        }
        static void ReplayTransport(BedrockSession& session, bool available) { session.replayMode = available; }
        static void Reset(BedrockSession& session) { session.ResetMovementProtocolState(); }
        static bool HasSentPosition(const BedrockSession& session) { return session.haveLastAuthPos; }
        static bool SendInitialized(BedrockSession& session) { return session.SendSetLocalPlayerAsInitialized(); }
        static void Place(LocalPlayer& player, const physics::State& state) {
            player.state = state;
            player.previous = state;
            player.history.clear();
            player.futureMotion.clear();
            player.futureTeleports.clear();
            player.futureEffectUpdates.clear();
            player.futureAttributeUpdates.clear();
        }
        static void Attributes(LocalPlayer& player, BedrockSession& session) {
            player.ApplyAttributeUpdates(session);
        }
        static void AttributeFrame(LocalPlayer& player, BedrockSession& session, uint64_t tick,
                                   const physics::Input& input, float freeze) {
            LocalPlayer::Record record;
            record.tick = tick;
            record.input = input;
            record.freeze = freeze;
            record.attributes.speed =
                session.PredictedMovementSpeed(input.sprint, freeze, record.movementAttribute);
            record.attributes.underwaterSpeed = session.UnderwaterMovementSpeedAttribute();
            record.attributes.lavaSpeed = session.LavaMovementSpeedAttribute();
            record.attributes.airDrag = session.AirDragAttribute();
            record.attributePresence = session.MovementAttributePresence();
            record.attributeUpdates.swap(player.futureAttributeUpdates);
            record.before = record.after = player.state;
            player.history.push_back(std::move(record));
        }
        static size_t AttributeEvents(const LocalPlayer& player, size_t index) {
            return player.history.at(index).attributeUpdates.size();
        }
        static const BedrockSession::MovementAttributeState& FrameMovementAttribute(
            const LocalPlayer& player, size_t index) {
            return player.history.at(index).movementAttribute;
        }
        static const physics::State& FrameBefore(const LocalPlayer& player, size_t index) {
            return player.history.at(index).before;
        }
        static size_t PendingAttributes(const LocalPlayer& player) {
            return player.futureAttributeUpdates.size();
        }
        static void Effects(LocalPlayer& player, BedrockSession& session) {
            player.ApplyEffectUpdates(session);
        }
        static void EffectFrame(LocalPlayer& player, uint64_t tick,
                                const std::vector<BedrockSession::EffectState>& effects,
                                const physics::Attributes& attributes) {
            LocalPlayer::Record record;
            record.tick = tick;
            record.effects = effects;
            record.attributes = attributes;
            record.before = record.after = player.state;
            player.history.push_back(std::move(record));
        }
        static const physics::Attributes& FrameAttributes(const LocalPlayer& player, size_t index) {
            return player.history.at(index).attributes;
        }
        static const physics::Input& FrameInput(const LocalPlayer& player, size_t index) {
            return player.history.at(index).input;
        }
        static void Replay(LocalPlayer& player, BedrockSession& session, size_t first) {
            player.ReplayFrom(session, first);
        }
        static const std::vector<BedrockSession::EffectState>& FrameEffects(const LocalPlayer& player,
                                                                         size_t index) {
            return player.history.at(index).effects;
        }
        static size_t EffectEvents(const LocalPlayer& player, size_t index) {
            return player.history.at(index).effectUpdates.size();
        }
        static const BedrockSession::EffectUpdate& EffectEvent(const LocalPlayer& player, size_t frame,
                                                              size_t index) {
            return player.history.at(frame).effectUpdates.at(index);
        }
        static size_t PendingEffects(const LocalPlayer& player) { return player.futureEffectUpdates.size(); }
        static bool ItemSlow(const LocalPlayer& player) { return player.itemSlow; }
        static void Tick(LocalPlayer& player, BedrockSession& session, const InputManager& input,
                         const Camera& camera, const physics::World& world) {
            player.Tick(session, input, camera, world);
        }
    };
}
