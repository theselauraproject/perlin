#pragma once
#include "host/WasmRuntime.hpp"

#include "perlin/abi.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace perlin::mods {
    inline constexpr uint32_t kMaxSubscriptionWords = 4;
    using Subscriptions = std::array<uint32_t, kMaxSubscriptionWords>;

    constexpr Subscriptions AllEvents() {
        Subscriptions s{};
        for (uint32_t& w : s)
            w = ~0u;
        return s;
    }

    constexpr bool Subscribed(const Subscriptions& s, uint32_t id) {
        return id / 32 < s.size() && (s[id / 32] >> (id % 32) & 1u);
    }
    constexpr bool Subscribed(const Subscriptions& s, abi::Event e) {
        return Subscribed(s, static_cast<uint32_t>(e));
    }

    class AtomicSubscriptions {
    public:
        AtomicSubscriptions() { Store(AllEvents()); }
        explicit AtomicSubscriptions(const Subscriptions& s) { Store(s); }
        void Store(const Subscriptions& s) {
            for (size_t i = 0; i < words.size(); i++)
                words[i].store(s[i], std::memory_order_relaxed);
        }
        Subscriptions Load() const {
            Subscriptions s{};
            for (size_t i = 0; i < words.size(); i++)
                s[i] = words[i].load(std::memory_order_relaxed);
            return s;
        }
        bool Has(abi::Event e) const {
            const uint32_t id = static_cast<uint32_t>(e);
            return id / 32 < words.size() && (words[id / 32].load(std::memory_order_relaxed) >> (id % 32) & 1u);
        }

    private:
        std::array<std::atomic<uint32_t>, kMaxSubscriptionWords> words;
    };

    Subscriptions ReadSubscriptions(WasmInstance& instance);

    using GuestLog = std::function<void(const std::string&)>;
    WasmInstance::HostFn WasiFunction(const std::string& name, GuestLog log);

    struct GuestExports {
        bool alloc = false;
        bool free = false;
        bool event = false;
        bool subscriptions = false;
        bool initialize = false;
    };
    GuestExports ProbeExports(WasmInstance& instance);

    bool CallEvent(WasmInstance& instance, const GuestExports& exports, uint32_t id,
                   std::span<const uint8_t> payload, uint32_t& flags, std::string& error);

    int32_t Reply(abi::Status status, const abi::Writer& reply, uint8_t* out, uint64_t outCap);

    using CallHandler = std::function<int32_t(uint32_t op, std::span<const uint8_t> in, uint8_t* out,
                                              uint32_t outCap, std::string& fatal)>;
    WasmInstance::HostFn PerlinCall(CallHandler handler);

    WasmInstance::Resolver StandardImports(CallHandler call, GuestLog log);
}
