#include "client/systems/mods/ModInternal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <random>
#include <set>
#include "util/Log.hpp"

namespace perlin::mods {
    using Clock = std::chrono::steady_clock;

    bool HostBridge::Call(ModManager::Plugin& plugin, WasmInstance::HostContext& ctx, const uint64_t* a,
                      uint32_t n, uint64_t* r) {
        if (plugin.revoked) {
            ctx.error = kRevoked;
            return false;
        }
        if (n != 5) {
            ctx.error = "perlin.call has the wrong signature";
            return false;
        }
        const uint32_t op = static_cast<uint32_t>(a[0]);
        uint8_t* in = ctx.instance.Memory(a[1], a[2]);
        uint8_t* out = ctx.instance.Memory(a[3], a[4]);
        if (!in || !out || a[2] > 0xFFFFFFFFu || a[4] > 0xFFFFFFFFu) {
            r[0] = static_cast<uint32_t>(static_cast<int32_t>(abi::Status::BadArguments));
            return true;
        }
        const int32_t rc = plugin.owner->HostCall(plugin, op, in, static_cast<uint32_t>(a[2]), out,
                                             static_cast<uint32_t>(a[4]));
        if (rc == INT32_MIN) {
            ctx.error = kRevoked;
            return false;
        }
        r[0] = static_cast<uint32_t>(rc);
        return true;
    }

    WasmInstance::Resolver HostBridge::Imports(ModManager::Plugin& plugin) {
        return [&plugin](const std::string& module, const std::string& name) -> WasmInstance::HostFn {
            if (module == "perlin" && name == "call")
                return [&plugin](WasmInstance::HostContext& c, const uint64_t* a, uint32_t n, uint64_t* r,
                                 uint32_t nr) {
                    if (nr != 1) {
                        c.error = "perlin.call must return i32";
                        return false;
                    }
                    return Call(plugin, c, a, n, r);
                };
            return StandardImports({}, [&plugin](const std::string& text) {
                Log("mod:" + plugin.name, "{}", text);
            })(module, name);
        };
    }

    bool HostBridge::CallEvent(ModManager::Plugin& mod, uint32_t id, const std::vector<uint8_t>& payload,
                               uint32_t& flags, std::string& error) {
        return ::perlin::mods::CallEvent(*mod.instance, mod.exports, id, payload, flags, error);
    }

    abi::Status ModManager::InvokeOp(abi::Op op, CallContext& ctx, abi::Reader& reader, abi::Writer& writer) {
        std::shared_lock lock(opsMutex);
        const auto it = ops.find(static_cast<uint32_t>(op));
        if (it == ops.end())
            return abi::Status::UnknownOp;
        if ((ctx.permissions & it->second.permissions) != it->second.permissions)
            return abi::Status::Denied;
        return it->second.handler(ctx, reader, writer);
    }

    int32_t ModManager::HostCall(Plugin& mod, uint32_t op, const uint8_t* in, uint32_t inLen, uint8_t* out,
                                 uint32_t outCap) {
        std::lock_guard gateLock(mod.gate);
        if (mod.revoked)
            return INT32_MIN;
        CallContext ctx{mod.name, mod.permissions, mod.storage, &mod, mod.apiRevision};
        abi::Reader reader(in, inLen);
        abi::Writer writer;
        static const bool wireCheck = std::getenv("PERLIN_MOD_WIRE_CHECK") != nullptr;
        const auto opId = static_cast<abi::Op>(op);
        if (wireCheck && !abi::host::RequestMatchesSchema(opId, {in, inLen}))
            Log("mods", "wire check: {} sent {} bytes to {} that do not match the schema", mod.name, inLen,
                abi::NameOf(opId));
        const abi::Status status = InvokeOp(static_cast<abi::Op>(op), ctx, reader, writer);
        if (status != abi::Status::Ok)
            return static_cast<int32_t>(status);
        if (wireCheck && !abi::host::ResponseMatchesSchema(opId, {writer.Data(), writer.Size()}))
            Log("mods", "wire check: the host answered {} with {} bytes that do not match the schema",
                abi::NameOf(opId), writer.Size());
        return Reply(abi::Status::Ok, writer, out, outCap);
    }

    void ModManager::Revoke(Plugin& mod, const char* why) {
        {
            std::lock_guard gateLock(mod.gate);
            mod.revoked = true;
            if (mod.instance)
                mod.instance->Interrupt();
        }
        mod.stuck = true;
        mod.loaded = false;
        {
            std::lock_guard lock(mutex);
            mod.error = why;
            ClearRegistrations(mod);
        }
        layerVersion++;
        commandVersion++;
        Log("mods", "{}: {}", mod.name, why);
    }

    void ModManager::Spawn(Plugin& mod) {
        mod.job.kind = Plugin::Job::Kind::None;
        mod.thread = std::thread(&ModManager::Worker, this, &mod);
    }

    void ModManager::Worker(ModManager* self, Plugin* mod) {
        tlsPlugin = mod;
        for (;;) {
            std::unique_lock lock(mod->job.mutex);
            mod->job.wake.wait(lock, [&] { return mod->job.kind != Plugin::Job::Kind::None; });
            const Plugin::Job::Kind kind = mod->job.kind;
            mod->job.kind = Plugin::Job::Kind::None;
            if (kind == Plugin::Job::Kind::Quit)
                break;
            lock.unlock();
            if (kind == Plugin::Job::Kind::Load)
                self->WorkerLoad(*mod);
            else
                self->WorkerEvent(*mod);
            lock.lock();
            mod->job.done = true;
            mod->job.finished.notify_all();
        }
        self->WorkerFree(*mod);
    }

    void ModManager::WorkerFree(Plugin& mod) {
        std::unique_ptr<WasmInstance> dead;
        {
            std::lock_guard gateLock(mod.gate);
            dead = std::move(mod.instance);
        }
        dead.reset();
        mod.exports = {};
    }

    void ModManager::WorkerLoad(Plugin& mod) {
        mod.job.error.clear();
        {
            std::lock_guard gateLock(mod.gate);
            mod.instance = std::make_unique<WasmInstance>();
            if (mod.revoked)
                mod.instance->Interrupt();
        }
        std::string error;
        if (!mod.instance->Load(mod.wasm.data(), mod.wasm.size(), HostBridge::Imports(mod), error)) {
            mod.job.error = error;
            return;
        }
        mod.exports = ProbeExports(*mod.instance);
        if (!mod.exports.event) {
            mod.job.error = "not a Perlin plugin (missing perlin_event export)";
            return;
        }
        {
            std::lock_guard lock(mutex);
            mod.arch = mod.instance->Is64() ? "wasm64" : "wasm32";
        }
        mod.apiRevision = ReadApiRevision(*mod.instance);
        Log("mods", "{}: {} on {}, api revision {}", mod.name, mod.arch, WasmEngineName(), mod.apiRevision);
        if (mod.apiRevision > abi::kApiRevision)
            Log("mods", "{}: built for api revision {}, this game provides {}: newer operations answer UnknownOp", mod.name,
                mod.apiRevision, abi::kApiRevision);
        if (mod.exports.initialize &&
            !mod.instance->Call("_initialize", {}, nullptr, error)) {
            mod.job.error = "_initialize: " + error;
            return;
        }
        uint32_t flags = 0;
        if (!HostBridge::CallEvent(mod, static_cast<uint32_t>(abi::Event::Load), {}, flags, error)) {
            mod.job.error = error;
            return;
        }
        if (mod.exports.subscriptions)
            mod.subs.Store(ReadSubscriptions(*mod.instance));
    }

    void ModManager::WorkerEvent(Plugin& mod) {
        mod.job.error.clear();
        uint32_t flags = 0;
        std::string error;
        if (!HostBridge::CallEvent(mod, static_cast<uint32_t>(mod.job.event), mod.job.payload, flags, error)) {
            mod.job.error = error;
            return;
        }
        mod.job.flags = flags;
        if (mod.exports.subscriptions && mod.job.event != abi::Event::Frame && mod.job.event != abi::Event::Unload)
            mod.subs.Store(ReadSubscriptions(*mod.instance));
    }

    bool ModManager::Call(Plugin& mod, abi::Event event, const abi::Writer& payload, double budgetMs,
                          bool& cancelled) {
        if (!mod.loaded || mod.stuck || tlsPlugin)
            return false;
        const auto started = Clock::now();
        {
            std::lock_guard lock(mod.job.mutex);
            mod.job.event = event;
            mod.job.payload = payload.data;
            mod.job.done = false;
            mod.job.flags = 0;
            mod.job.kind = Plugin::Job::Kind::Event;
        }
        mod.job.wake.notify_all();
        std::unique_lock lock(mod.job.mutex);
        const bool finished = mod.job.finished.wait_for(lock, std::chrono::duration<double, std::milli>(budgetMs),
                                                  [&] { return mod.job.done; });
        if (!finished) {
            lock.unlock();
            Revoke(mod, "unresponsive: an event ran too long, the plugin was stopped");
            return false;
        }
        const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        mod.lastEventMs = elapsed;
        if (!mod.job.error.empty()) {
            const std::string message = mod.job.error;
            lock.unlock();
            std::lock_guard guard(mutex);
            mod.error = message;
            mod.loaded = false;
            ClearRegistrations(mod);
            layerVersion++;
            commandVersion++;
            Log("mods", "{} stopped: {}", mod.name, message);
            return false;
        }
        cancelled = (mod.job.flags & abi::kEventCancel) != 0;
        return true;
    }

    bool ModManager::WantsEvent(abi::Event event) const {
        std::vector<Plugin*> list;
        {
            std::lock_guard lock(const_cast<std::mutex&>(mutex));
            for (const auto& mod : mods)
                if (mod->loaded && !mod->stuck)
                    list.push_back(mod.get());
        }
        for (Plugin* mod : list)
            if (mod->subs.Has(event))
                return true;
        return false;
    }

    bool ModManager::Dispatch(abi::Event event, const abi::Writer& payload, double budgetMs) {
        if (tlsPlugin)
            return false;
        std::vector<Plugin*> list;
        {
            std::lock_guard lock(mutex);
            for (const auto& mod : mods)
                if (mod->loaded && !mod->stuck)
                    list.push_back(mod.get());
        }
        std::ranges::stable_sort(
            list, [](const Plugin* a, const Plugin* b) { return a->meta.priority > b->meta.priority; });
        bool cancelled = false;
        const auto now = Clock::now();
        for (Plugin* mod : list) {
            if (!mod->subs.Has(event))
                continue;
            bool c = false;
            if (event == abi::Event::Frame && mod->meta.frameRate > 0 && payload.data.size() >= sizeof(float)) {
                float dt = 0;
                std::memcpy(&dt, payload.data.data(), sizeof dt);
                mod->frameDtAccum += dt;
                const double interval = 1.0 / mod->meta.frameRate;
                if (std::chrono::duration<double>(now - mod->lastFrameAt).count() < interval * 0.95)
                    continue;
                mod->lastFrameAt = now;
                abi::Writer merged = payload;
                std::memcpy(merged.data.data(), &mod->frameDtAccum, sizeof(float));
                mod->frameDtAccum = 0;
                Call(*mod, event, merged, budgetMs, c);
                continue;
            }
            if (Call(*mod, event, payload, budgetMs, c) && c)
                cancelled = true;
        }
        return cancelled;
    }

    void ModManager::DispatchTo(const std::string& plugin, abi::Event event, const abi::Writer& payload,
                                double budgetMs) {
        if (tlsPlugin)
            return;
        Plugin* mod = Find(plugin);
        if (!mod)
            return;
        bool c = false;
        Call(*mod, event, payload, budgetMs, c);
    }
}
