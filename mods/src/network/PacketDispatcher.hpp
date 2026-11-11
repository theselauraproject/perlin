#pragma once
#include "network/Stream.hpp"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace perlin::net {

    class PacketDispatcher {
    public:
        enum class Result { Unhandled, Handled, Malformed };

        using FailureHandler = std::function<void(uint32_t id, const char* name, size_t size)>;

        template <class P, class Fn> void On(Fn&& fn) {
            Channel<P>().handlers.emplace_back(std::forward<Fn>(fn));
        }

        template <class P, class Fn> void OnPartial(Fn&& fn) {
            auto& channel = Channel<P>();
            channel.partial = true;
            channel.handlers.emplace_back(std::forward<Fn>(fn));
        }

        template <class Fn> void OnRaw(uint32_t id, Fn&& fn) { raw[id].emplace_back(std::forward<Fn>(fn)); }

        void OnFailure(FailureHandler handler) { failure = std::move(handler); }

        Result Dispatch(uint32_t id, const uint8_t* data, size_t size) const {
            Result result = Result::Unhandled;
            if (auto it = raw.find(id); it != raw.end()) {
                for (const auto& handler : it->second)
                    handler(data, size);
                result = Result::Handled;
            }
            if (auto it = channels.find(id); it != channels.end()) {
                if (it->second->Deliver(data, size))
                    result = Result::Handled;
                else {
                    result = Result::Malformed;
                    if (failure)
                        failure(id, it->second->Name(), size);
                }
            }
            return result;
        }

        bool Handles(uint32_t id) const { return channels.contains(id) || raw.contains(id); }

    private:
        struct ChannelBase {
            virtual ~ChannelBase() = default;
            virtual bool Deliver(const uint8_t* data, size_t size) const = 0;
            virtual const char* Name() const = 0;
        };

        template <class P> struct TypedChannel final : ChannelBase {
            std::vector<std::function<void(const P&)>> handlers;
            bool partial = false;

            bool Deliver(const uint8_t* data, size_t size) const override {
                P packet;
                ReadStream stream(data, size);
                const bool ok = Read(stream, packet) && stream.ok;
                if (!ok && !partial)
                    return false;
                for (const auto& handler : handlers)
                    handler(packet);
                return ok;
            }
            const char* Name() const override { return P::kName; }
        };

        template <class P> TypedChannel<P>& Channel() {
            auto& slot = channels[P::kId];
            if (!slot)
                slot = std::make_unique<TypedChannel<P>>();
            return static_cast<TypedChannel<P>&>(*slot);
        }

        std::unordered_map<uint32_t, std::unique_ptr<ChannelBase>> channels;
        std::unordered_map<uint32_t, std::vector<std::function<void(const uint8_t*, size_t)>>> raw;
        FailureHandler failure;
    };

}
