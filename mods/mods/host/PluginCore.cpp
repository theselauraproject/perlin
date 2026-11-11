#include "host/PluginCore.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>
#include <vector>

namespace perlin::mods {
    namespace {
        constexpr uint32_t kBadf = 8, kFault = 21, kNosys = 52, kNotcapable = 76, kSpipe = 70;
        using Ctx = WasmInstance::HostContext;

        bool PutSize(WasmInstance& w, uint64_t at, uint64_t value) {
            const uint64_t width = w.Is64() ? 8 : 4;
            uint8_t* p = w.Memory(at, width);
            if (!p)
                return false;
            std::memcpy(p, &value, width);
            return true;
        }

        WasmInstance::HostFn Const(uint32_t code) {
            return [code](Ctx&, const uint64_t*, uint32_t, uint64_t* r, uint32_t nr) {
                if (nr)
                    r[0] = code;
                return true;
            };
        }

        uint64_t Nanos(uint64_t clock) {
            using namespace std::chrono;
            if (clock == 0)
                return static_cast<uint64_t>(
                    duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
            return static_cast<uint64_t>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
        }
    }

    Subscriptions ReadSubscriptions(WasmInstance& w) {
        Subscriptions s{};
        std::string ignored;
        uint64_t v = 0;
        if (w.HasExport("perlin_subscriptions_word")) {
            for (uint32_t i = 0; i < s.size(); i++)
                if (w.Call("perlin_subscriptions_word", {i}, &v, ignored))
                    s[i] = static_cast<uint32_t>(v);
            return s;
        }
        if (!w.HasExport("perlin_subscriptions"))
            return AllEvents();
        if (w.Call("perlin_subscriptions", {}, &v, ignored))
            s[0] = static_cast<uint32_t>(v);
        if (w.HasExport("perlin_subscriptions_hi") && w.Call("perlin_subscriptions_hi", {}, &v, ignored))
            s[1] = static_cast<uint32_t>(v);
        return s;
    }

    WasmInstance::HostFn WasiFunction(const std::string& name, GuestLog log) {
        if (name == "args_sizes_get" || name == "environ_sizes_get")
            return [](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
                r[0] = n >= 2 && PutSize(c.instance, a[0], 0) && PutSize(c.instance, a[1], 0) ? 0 : kFault;
                return true;
            };
        if (name == "args_get" || name == "environ_get" || name == "fd_close" || name == "fd_sync" ||
            name == "fd_datasync" || name == "sched_yield")
            return Const(0);
        if (name == "clock_time_get")
            return [](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
                uint8_t* out = n >= 3 ? c.instance.Memory(a[2], 8) : nullptr;
                if (!out) {
                    r[0] = kFault;
                    return true;
                }
                const uint64_t ns = Nanos(a[0]);
                std::memcpy(out, &ns, 8);
                r[0] = 0;
                return true;
            };
        if (name == "clock_res_get")
            return [](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
                uint8_t* out = n >= 2 ? c.instance.Memory(a[1], 8) : nullptr;
                const uint64_t res = 1000;
                if (out)
                    std::memcpy(out, &res, 8);
                r[0] = out ? 0 : kFault;
                return true;
            };
        if (name == "random_get")
            return [](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
                uint8_t* bytes = n >= 2 ? c.instance.Memory(a[0], a[1]) : nullptr;
                if (!bytes) {
                    r[0] = kFault;
                    return true;
                }
                thread_local std::mt19937_64 rng{std::random_device{}()};
                for (uint64_t i = 0; i < a[1]; i++)
                    bytes[i] = static_cast<uint8_t>(rng());
                r[0] = 0;
                return true;
            };
        if (name == "fd_write")
            return [log = std::move(log)](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
                if (n < 4)
                    return false;
                if (a[0] != 1 && a[0] != 2) {
                    r[0] = kBadf;
                    return true;
                }
                const uint64_t stride = c.instance.Is64() ? 16 : 8, half = stride / 2;
                const uint64_t count = a[2];
                const uint8_t* iovs = count <= 1024 ? c.instance.Memory(a[1], count * stride) : nullptr;
                if (!iovs) {
                    r[0] = kFault;
                    return true;
                }
                std::vector<uint8_t> list(iovs, iovs + count * stride);
                std::string text;
                uint64_t total = 0;
                for (uint64_t i = 0; i < count; i++) {
                    uint64_t ptr = 0, len = 0;
                    std::memcpy(&ptr, list.data() + i * stride, half);
                    std::memcpy(&len, list.data() + i * stride + half, half);
                    const uint8_t* buf = c.instance.Memory(ptr, len);
                    if (!buf) {
                        r[0] = kFault;
                        return true;
                    }
                    if (text.size() < 65536)
                        text.append(reinterpret_cast<const char*>(buf),
                                    static_cast<size_t>(std::min<uint64_t>(len, 65536)));
                    total += len;
                }
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                    text.pop_back();
                if (!text.empty() && log)
                    log(text);
                r[0] = PutSize(c.instance, a[3], total) ? 0 : kFault;
                return true;
            };
        if (name == "fd_fdstat_get")
            return [](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
                uint8_t* out = n >= 2 && a[0] <= 2 ? c.instance.Memory(a[1], 24) : nullptr;
                if (!out) {
                    r[0] = kBadf;
                    return true;
                }
                uint8_t stat[24] = {};
                stat[0] = 2;
                const uint64_t rights = a[0] == 0 ? (1ull << 1) : (1ull << 6);
                std::memcpy(stat + 8, &rights, 8);
                std::memcpy(out, stat, 24);
                r[0] = 0;
                return true;
            };
        if (name == "proc_exit")
            return [](Ctx& c, const uint64_t*, uint32_t, uint64_t*, uint32_t) {
                c.error = "plugin exited";
                return false;
            };
        if (name == "fd_seek" || name == "fd_tell")
            return Const(kSpipe);
        if (name.starts_with("fd_"))
            return Const(kBadf);
        if (name.starts_with("path_"))
            return Const(kNotcapable);
        return Const(kNosys);
    }

    GuestExports ProbeExports(WasmInstance& w) {
        GuestExports e;
        e.alloc = w.HasExport("perlin_alloc");
        e.free = w.HasExport("perlin_free");
        e.event = w.HasExport("perlin_event");
        e.subscriptions = w.HasExport("perlin_subscriptions") || w.HasExport("perlin_subscriptions_word");
        e.initialize = w.HasExport("_initialize");
        return e;
    }

    bool CallEvent(WasmInstance& w, const GuestExports& exports, uint32_t id, std::span<const uint8_t> payload,
                   uint32_t& flags, std::string& error) {
        uint64_t ptr = 0;
        const uint64_t len = payload.size();
        if (len) {
            if (!exports.alloc) {
                error = "plugin has no perlin_alloc export";
                return false;
            }
            if (!w.Call("perlin_alloc", {len}, &ptr, error)) {
                error = "alloc: " + error;
                return false;
            }
            uint8_t* dst = ptr ? w.Memory(ptr, len) : nullptr;
            if (!dst) {
                error = "perlin_alloc returned a bad pointer";
                return false;
            }
            std::memcpy(dst, payload.data(), payload.size());
        }
        uint64_t result = 0;
        if (!w.Call("perlin_event", {id, ptr, len}, &result, error)) {
            const auto name = abi::NameOf(static_cast<abi::Event>(id));
            error = std::string(name) + ": " + error;
            return false;
        }
        flags = static_cast<uint32_t>(result);
        std::string ignored;
        if (len && exports.free)
            w.Call("perlin_free", {ptr}, nullptr, ignored);
        return true;
    }

    int32_t Reply(abi::Status status, const abi::Writer& reply, uint8_t* out, uint64_t outCap) {
        if (status != abi::Status::Ok)
            return static_cast<int32_t>(status);
        if (reply.Size() > outCap)
            return -(abi::kNeedMoreBase + static_cast<int32_t>(std::min<size_t>(reply.Size(), 1u << 28)));
        if (reply.Size())
            std::memcpy(out, reply.Data(), reply.Size());
        return static_cast<int32_t>(reply.Size());
    }

    WasmInstance::HostFn PerlinCall(CallHandler handler) {
        return [handler = std::move(handler)](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t nr) {
            if (n != 5 || nr != 1) {
                c.error = "perlin.call has the wrong signature";
                return false;
            }
            const uint8_t* in = c.instance.Memory(a[1], a[2]);
            uint8_t* out = c.instance.Memory(a[3], a[4]);
            if ((!in && a[2]) || (!out && a[4]) || a[2] > 0xFFFFFFFFu || a[4] > 0xFFFFFFFFu) {
                r[0] = static_cast<uint32_t>(static_cast<int32_t>(abi::Status::BadArguments));
                return true;
            }
            std::string fatal;
            const int32_t rc = handler(static_cast<uint32_t>(a[0]), {in, static_cast<size_t>(a[2])}, out,
                                       static_cast<uint32_t>(a[4]), fatal);
            if (!fatal.empty()) {
                c.error = fatal;
                return false;
            }
            r[0] = static_cast<uint32_t>(rc);
            return true;
        };
    }

    WasmInstance::Resolver StandardImports(CallHandler call, GuestLog log) {
        return [call = std::move(call), log = std::move(log)](const std::string& module,
                                                              const std::string& name) -> WasmInstance::HostFn {
            if (module == "perlin" && name == "call")
                return PerlinCall(call);
            if (module == "wasi_snapshot_preview1")
                return WasiFunction(name, log);
            if (module == "env" && name == "emscripten_notify_memory_growth")
                return Const(0);
            return {};
        };
    }
}
