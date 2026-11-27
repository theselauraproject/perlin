#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"

#include <atomic>
#include <barrier>
#include <chrono>
#include <functional>
#include <future>
#include <thread>

namespace {
    using namespace perlin;

    template<class Packet>
    std::vector<uint8_t> Wire(const Packet& packet, int protocol, int shield = -1) {
        const net::context::ScopedSettings context({protocol, shield});
        return net::Encode(packet);
    }

    void Registry(BedrockSession& session, int shield) {
        proto::ItemRegistryPacket packet;
        proto::ItemstatesEntry item;
        item.name = shield >= 0 ? "minecraft:shield" : "minecraft:stone";
        item.runtimeId = static_cast<int16_t>(shield >= 0 ? shield : 1);
        packet.itemstates.push_back(item);
        session.InjectIncoming(packet.kId, Wire(packet, session.ServerProtocol()));
    }

    proto::InventorySlotPacket Shield(int runtimeId) {
        proto::InventorySlotPacket packet;
        packet.windowId = proto::WindowIDVarint::Inventory;
        packet.item.networkId = runtimeId;
        packet.item.count = 1;
        packet.item.hasStackId = true;
        packet.item.stackId = 42;
        proto::ItemExtraDataWithBlockingTick extra;
        extra.blockingTick = 123456789;
        packet.item.extra = extra;
        return packet;
    }

    struct ResetWhileWriting {
        proto::PlaySoundPacket packet;
        std::function<void()> reset;
        static constexpr uint32_t kId = proto::PlaySoundPacket::kId;
        static constexpr const char* kName = "codec_profile_reset_test";
    };

    void Write(net::WriteStream& stream, const ResetWhileWriting& value) {
        Write(stream, value.packet);
        value.reset();
    }
}

namespace perlin::selftest {
    int SessionCodecContext() {
        Checker t("session_codec_context_test");
        struct Defaults {
            int32_t protocol = net::context::ProtocolSlot().load();
            int32_t shield = net::context::ShieldItemIdSlot().load();
            ~Defaults() {
                net::context::SetProtocol(protocol);
                net::context::SetShieldItemID(shield);
            }
        } defaults;
        constexpr int oldProtocol = 944;
        BedrockSession older, current;
        older.BeginReplayMode("older", 7, oldProtocol);
        current.BeginReplayMode("current", 8, kBedrockProtocol);

        proto::PlaySoundPacket play;
        play.name = "context.tone";
        play.coordinates = {8, 16, 24};
        play.volume = 1;
        play.pitch = 0.75f;
        play.loopCount = 3;
        play.bypassListenerRangeCheck = true;
        play.handle = 42;
        play.playbackPositionSeconds = 0.5f;
        const auto oldWire = Wire(play, oldProtocol);
        const auto currentWire = Wire(play, kBedrockProtocol);
        t.Check(!oldWire.empty() && oldWire.size() < currentWire.size(),
                "the two session fixtures use structurally different wire profiles");

        net::context::SetProtocol(kBedrockProtocol);
        older.InjectIncoming(play.kId, oldWire);
        auto oldSounds = older.TakeSounds();
        t.Check(oldSounds.size() == 1 && oldSounds[0].name == play.name &&
                    oldSounds[0].loopCount == 0 && !oldSounds[0].handle,
                "older session decoding ignores another session's current global profile");
        net::context::SetProtocol(oldProtocol);
        current.InjectIncoming(play.kId, currentWire);
        auto currentSounds = current.TakeSounds();
        t.Check(currentSounds.size() == 1 && currentSounds[0].loopCount == 3 &&
                    currentSounds[0].handle == play.handle &&
                    currentSounds[0].playbackPositionSeconds == play.playbackPositionSeconds,
                "current session retains fields absent from the old global profile");

        std::vector<uint8_t> sent;
        int tapProtocol = 0, tapShield = 0;
        older.SetSentTap([&](uint32_t, const auto& body) {
            sent = body;
            tapProtocol = net::context::Protocol();
            tapShield = net::context::ShieldItemID();
        });
        net::context::SetProtocol(kBedrockProtocol);
        t.Check(older.Send(play) && sent == oldWire && tapProtocol == oldProtocol,
                "reliable send binds its owning session through encoding and transport callbacks");
        t.Check(older.SendUnreliable(play) && sent == oldWire && tapProtocol == oldProtocol,
                "unreliable send uses the same owning profile");
        t.Check(older.SendRaw(play.kId, oldWire) && tapProtocol == oldProtocol,
                "raw-send transport logging and callbacks bind their owning session");

        bool nestedRan = false;
        current.SetSentTap([&](uint32_t, const auto& body) {
            t.Check(net::context::Protocol() == kBedrockProtocol && body == currentWire,
                    "a nested send temporarily binds the other session");
        });
        older.On<proto::PlaySoundPacket>([&](const auto&) {
            t.Equal(net::context::Protocol(), oldProtocol, "incoming callback runs in its packet profile");
            t.Check(current.Send(play), "nested send succeeds");
            t.Check(net::context::Protocol() == oldProtocol && net::Encode(play) == oldWire,
                    "return from the nested session restores the outer callback's codec");
            nestedRan = true;
        });
        net::context::SetProtocol(1001);
        older.InjectIncoming(play.kId, oldWire);
        t.Check(nestedRan && net::context::Protocol() == 1001,
                "return from dispatch restores the standalone caller's default");
        older.TakeSounds();

        // Both callbacks are held inside their active Send scopes at the same
        // time. This fails if the override itself is process-global.
        std::barrier simultaneous(2);
        std::atomic<bool> oldConcurrent{false}, currentConcurrent{false};
        older.SetSentTap([&](uint32_t, const auto& body) {
            simultaneous.arrive_and_wait();
            oldConcurrent = net::context::Protocol() == oldProtocol && body == oldWire;
            simultaneous.arrive_and_wait();
        });
        current.SetSentTap([&](uint32_t, const auto& body) {
            simultaneous.arrive_and_wait();
            currentConcurrent = net::context::Protocol() == kBedrockProtocol && body == currentWire;
            simultaneous.arrive_and_wait();
        });
        std::thread oldSender([&] {
            if (!older.Send(play))
                simultaneous.arrive_and_drop();
        });
        std::thread currentSender([&] {
            if (!current.Send(play))
                simultaneous.arrive_and_drop();
        });
        oldSender.join();
        currentSender.join();
        t.Check(oldConcurrent && currentConcurrent, "simultaneous sessions keep independent thread bindings");
        t.Equal(net::context::Protocol(), 1001, "worker scopes do not change the main thread's default");
        older.SetSentTap({});
        current.SetSentTap({});

        older.SetSentTap([&](uint32_t, const auto& body) {
            sent = body;
            tapProtocol = net::context::Protocol();
        });
        t.Check(older.Send(ResetWhileWriting{play, [&] {
                    older.BeginReplayMode("replacement", 7, kBedrockProtocol);
                }}) && sent == oldWire && tapProtocol == oldProtocol,
                "transport callbacks retain the encoding snapshot if the session profile changes");
        older.BeginReplayMode("older", 7, oldProtocol);
        older.SetSentTap({});

        Registry(older, 17);
        Registry(current, 99);
        auto oldShield = Shield(17);
        auto currentShield = Shield(99);
        const auto oldShieldWire = Wire(oldShield, oldProtocol, 17);
        const auto currentShieldWire = Wire(currentShield, kBedrockProtocol, 99);
        bool oldShieldRead = false, currentShieldRead = false;
        older.On<proto::InventorySlotPacket>([&](const auto& packet) {
            if (const auto* extra = std::get_if<proto::ItemExtraDataWithBlockingTick>(&packet.item.extra))
                oldShieldRead = packet.item.networkId == 17 && extra->blockingTick == 123456789;
        });
        current.On<proto::InventorySlotPacket>([&](const auto& packet) {
            if (const auto* extra = std::get_if<proto::ItemExtraDataWithBlockingTick>(&packet.item.extra))
                currentShieldRead = packet.item.networkId == 99 && extra->blockingTick == 123456789;
        });
        older.InjectIncoming(oldShield.kId, oldShieldWire);
        net::context::SetShieldItemID(17);
        current.InjectIncoming(currentShield.kId, currentShieldWire);
        t.Check(oldShieldRead && currentShieldRead, "item decoding uses each session's shield registry ID");
        older.SetSentTap([&](uint32_t, const auto& body) {
            sent = body;
            tapShield = net::context::ShieldItemID();
        });
        net::context::SetShieldItemID(99);
        t.Check(older.Send(oldShield) && sent == oldShieldWire && tapShield == 17,
                "shield encoding is isolated from another registry's process default");
        Registry(older, -1);
        t.Check(older.SendRaw(play.kId, oldWire) && tapShield == -1,
                "a replacement registry without a shield removes the previous session ID");
        older.SetSentTap({});

        proto::AnimatePacket swing;
        swing.actionId = proto::AnimatePacketActionId::SwingArm;
        swing.runtimeEntityId = 7;
        swing.hasSwingSource = true;
        swing.swingSource = "context";
        net::context::SetProtocol(kBedrockProtocol);
        older.InjectClientPacket(swing.kId, Wire(swing, oldProtocol));
        std::vector<BedrockSession::ReplayClientEvent> clientEvents;
        older.TakeReplayClientEvents(clientEvents);
        t.Check(clientEvents.size() == 1 &&
                    clientEvents[0].kind == BedrockSession::ReplayClientEvent::Kind::Swing,
                "outgoing replay packet decoding binds its recording's profile");

        // Queue a second packet while the worker is blocked in the first. A
        // profile reset must not reinterpret bytes already in its queue.
        Registry(older, 17);
        struct Deferred {
            std::promise<void> entered, release;
            std::shared_future<void> released = release.get_future().share();
            std::promise<net::context::Settings> observed;
            std::atomic<int> calls{0};
        };
        const auto deferred = std::make_shared<Deferred>();
        auto enteredFuture = deferred->entered.get_future();
        auto observedFuture = deferred->observed.get_future();
        older.On<proto::ClientCacheMissResponsePacket>([deferred](const auto&) {
            if (deferred->calls.fetch_add(1) == 0) {
                deferred->entered.set_value();
                deferred->released.wait();
            } else {
                deferred->observed.set_value({net::context::Protocol(), net::context::ShieldItemID()});
            }
        });
        proto::ClientCacheMissResponsePacket emptyCache;
        const auto cacheWire = Wire(emptyCache, oldProtocol, 17);
        older.InjectIncoming(emptyCache.kId, cacheWire);
        const bool enteredWorker = enteredFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        t.Check(enteredWorker, "deferred packet reaches its worker");
        if (enteredWorker) {
            older.InjectIncoming(emptyCache.kId, cacheWire);
            older.BeginReplayMode("replacement", 9, kBedrockProtocol);
        }
        deferred->release.set_value();
        if (enteredWorker) {
            const bool completed = observedFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
            t.Check(completed, "queued packet completes after the profile changes");
            if (completed) {
                const auto settings = observedFuture.get();
                t.Check(settings.protocol == oldProtocol && settings.shieldItemId == 17,
                        "deferred packets retain their enqueue-time protocol and registry snapshot");
            }
        }
        older.WaitChunkWorkIdle();
        return t.Finish();
    }
}
