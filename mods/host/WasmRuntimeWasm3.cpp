#include "host/WasmRuntime.hpp"

#include <wasm3.h>

#include <algorithm>
#include <cstring>

namespace perlin::mods {
    namespace {
        struct KnownImport {
            const char* module;
            const char* name;
        };
        constexpr KnownImport kImports[] = {
            {"perlin", "call"},
            {"env", "emscripten_notify_memory_growth"},
            {"wasi_snapshot_preview1", "args_sizes_get"},
            {"wasi_snapshot_preview1", "environ_sizes_get"},
            {"wasi_snapshot_preview1", "args_get"},
            {"wasi_snapshot_preview1", "environ_get"},
            {"wasi_snapshot_preview1", "clock_time_get"},
            {"wasi_snapshot_preview1", "clock_res_get"},
            {"wasi_snapshot_preview1", "random_get"},
            {"wasi_snapshot_preview1", "fd_write"},
            {"wasi_snapshot_preview1", "fd_fdstat_get"},
            {"wasi_snapshot_preview1", "fd_close"},
            {"wasi_snapshot_preview1", "fd_sync"},
            {"wasi_snapshot_preview1", "fd_datasync"},
            {"wasi_snapshot_preview1", "fd_seek"},
            {"wasi_snapshot_preview1", "fd_tell"},
            {"wasi_snapshot_preview1", "fd_read"},
            {"wasi_snapshot_preview1", "fd_pread"},
            {"wasi_snapshot_preview1", "fd_pwrite"},
            {"wasi_snapshot_preview1", "fd_readdir"},
            {"wasi_snapshot_preview1", "fd_renumber"},
            {"wasi_snapshot_preview1", "fd_advise"},
            {"wasi_snapshot_preview1", "fd_allocate"},
            {"wasi_snapshot_preview1", "fd_fdstat_set_flags"},
            {"wasi_snapshot_preview1", "fd_fdstat_set_rights"},
            {"wasi_snapshot_preview1", "fd_filestat_get"},
            {"wasi_snapshot_preview1", "fd_filestat_set_size"},
            {"wasi_snapshot_preview1", "fd_filestat_set_times"},
            {"wasi_snapshot_preview1", "fd_prestat_get"},
            {"wasi_snapshot_preview1", "fd_prestat_dir_name"},
            {"wasi_snapshot_preview1", "path_open"},
            {"wasi_snapshot_preview1", "path_filestat_get"},
            {"wasi_snapshot_preview1", "path_filestat_set_times"},
            {"wasi_snapshot_preview1", "path_create_directory"},
            {"wasi_snapshot_preview1", "path_remove_directory"},
            {"wasi_snapshot_preview1", "path_unlink_file"},
            {"wasi_snapshot_preview1", "path_rename"},
            {"wasi_snapshot_preview1", "path_readlink"},
            {"wasi_snapshot_preview1", "path_symlink"},
            {"wasi_snapshot_preview1", "path_link"},
            {"wasi_snapshot_preview1", "poll_oneoff"},
            {"wasi_snapshot_preview1", "sched_yield"},
            {"wasi_snapshot_preview1", "proc_exit"},
            {"wasi_snapshot_preview1", "proc_raise"},
            {"wasi_snapshot_preview1", "sock_accept"},
            {"wasi_snapshot_preview1", "sock_recv"},
            {"wasi_snapshot_preview1", "sock_send"},
            {"wasi_snapshot_preview1", "sock_shutdown"},
        };

        bool Uses64BitMemory(const uint8_t* p, size_t n) {
            const auto leb = [&](size_t& at, uint64_t& out) {
                out = 0;
                for (int shift = 0; at < n && shift < 64; shift += 7) {
                    const uint8_t b = p[at++];
                    out |= static_cast<uint64_t>(b & 0x7F) << shift;
                    if (!(b & 0x80))
                        return true;
                }
                return false;
            };
            size_t at = 8;
            while (at < n) {
                const uint8_t id = p[at++];
                uint64_t size = 0;
                if (!leb(at, size) || size > n - at)
                    return false;
                if (id == 5) {
                    size_t q = at;
                    uint64_t count = 0;
                    if (leb(q, count) && count && q < n)
                        return (p[q] & 0x04) != 0;
                    return false;
                }
                at += static_cast<size_t>(size);
            }
            return false;
        }
    }

    struct WasmInstance::Impl {
        struct Link {
            Impl* owner = nullptr;
            HostFn fn;
        };

        WasmInstance* self = nullptr;
        IM3Environment env = nullptr;
        IM3Runtime runtime = nullptr;
        IM3Module module = nullptr;
        std::vector<std::unique_ptr<Link>> links;
        std::string hostError;
        void* frameMemory = nullptr;

        ~Impl() {
            if (runtime)
                m3_FreeRuntime(runtime);
            if (env)
                m3_FreeEnvironment(env);
        }

        static const void* Trampoline(IM3Runtime, IM3ImportContext ctx, uint64_t* sp, void* mem) {
            auto* link = static_cast<Link*>(ctx->userdata);
            Impl& impl = *link->owner;
            const uint32_t nr = std::min<uint32_t>(m3_GetRetCount(ctx->function), 4);
            const uint32_t na = std::min<uint32_t>(m3_GetArgCount(ctx->function), 16);
            uint64_t* ret = sp;
            uint64_t args[16] = {};
            for (uint32_t i = 0; i < na; i++) {
                const uint64_t raw = sp[nr + i];
                args[i] = m3_GetArgType(ctx->function, i) == c_m3Type_i64 ? raw : static_cast<uint32_t>(raw);
            }
            uint64_t results[4] = {};
            void* previous = impl.frameMemory;
            impl.frameMemory = mem;
            std::string error;
            HostContext hc{*impl.self, error};
            const bool ok = link->fn(hc, args, na, results, nr);
            impl.frameMemory = previous;
            if (!ok) {
                impl.hostError = error.empty() ? "host call failed" : error;
                return "host call failed";
            }
            for (uint32_t i = 0; i < nr; i++) {
                if (m3_GetRetType(ctx->function, i) == c_m3Type_i64)
                    ret[i] = results[i];
                else
                    *reinterpret_cast<uint32_t*>(&ret[i]) = static_cast<uint32_t>(results[i]);
            }
            return m3Err_none;
        }
    };

    WasmInstance::WasmInstance() : impl(std::make_unique<Impl>()) {
        impl->self = this;
    }

    WasmInstance::~WasmInstance() = default;

    bool WasmInstance::Load(const uint8_t* bytes, size_t size, const Resolver& resolver, std::string& error) {
        Impl& m = *impl;
        is64 = Uses64BitMemory(bytes, size);
        m.env = m3_NewEnvironment();
        m.runtime = m3_NewRuntime(m.env, 512 * 1024, nullptr);
        if (!m.env || !m.runtime) {
            error = "could not start the WebAssembly engine";
            return false;
        }
        IM3Module module = nullptr;
        if (M3Result r = m3_ParseModule(m.env, &module, bytes, static_cast<uint32_t>(size))) {
            error = std::string("parse: ") + r;
            return false;
        }
        if (M3Result r = m3_LoadModule(m.runtime, module)) {
            m3_FreeModule(module);
            error = std::string("load: ") + r;
            return false;
        }
        m.module = module;
        for (const KnownImport& k : kImports) {
            HostFn fn = resolver(k.module, k.name);
            if (!fn)
                continue;
            auto link = std::make_unique<Impl::Link>();
            link->owner = &m;
            link->fn = std::move(fn);
            if (!m3_LinkRawFunctionEx(module, k.module, k.name, nullptr, &Impl::Trampoline, link.get()))
                m.links.push_back(std::move(link));
        }
        return true;
    }

    bool WasmInstance::HasExport(const char* name) const {
        IM3Function fn = nullptr;
        return impl->runtime && !m3_FindFunction(&fn, impl->runtime, name) && fn;
    }

    bool WasmInstance::Call(const char* name, std::initializer_list<uint64_t> args, uint64_t* result,
                            std::string& error) {
        Impl& m = *impl;
        IM3Function fn = nullptr;
        if (M3Result r = m3_FindFunction(&fn, m.runtime, name); r || !fn) {
            error = std::string("missing export ") + name;
            return false;
        }
        const uint32_t argc = m3_GetArgCount(fn);
        if (argc != args.size() || argc > 8) {
            error = std::string(name) + " has the wrong signature";
            return false;
        }
        uint64_t storage[8] = {};
        const void* ptrs[8] = {};
        uint32_t i = 0;
        for (const uint64_t a : args) {
            if (m3_GetArgType(fn, i) == c_m3Type_i64)
                storage[i] = a;
            else
                *reinterpret_cast<uint32_t*>(&storage[i]) = static_cast<uint32_t>(a);
            ptrs[i] = &storage[i];
            i++;
        }
        m.hostError.clear();
        if (M3Result r = m3_Call(fn, argc, ptrs)) {
            error = m.hostError.empty() ? std::string(r) : m.hostError;
            return false;
        }
        if (result) {
            *result = 0;
            if (m3_GetRetCount(fn) > 0) {
                uint64_t out = 0;
                const void* outPtr[1] = {&out};
                m3_GetResults(fn, 1, outPtr);
                *result = m3_GetRetType(fn, 0) == c_m3Type_i64 ? out : static_cast<uint32_t>(out);
            }
        }
        return true;
    }

    uint8_t* WasmInstance::Memory(uint64_t ptr, uint64_t len) {
        uint8_t* base = nullptr;
        uint64_t bytes = 0;
        if (impl->frameMemory) {
            base = static_cast<uint8_t*>(impl->frameMemory);
            bytes = m3_GetMemorySizeAt(impl->frameMemory);
        } else if (impl->module) {
            size_t size = 0;
            base = m3_GetMemory(impl->module, &size, 0);
            bytes = size;
        }
        if (!base || ptr > bytes || len > bytes - ptr)
            return nullptr;
        return base + ptr;
    }

    void WasmInstance::Interrupt() {}

    std::string WasmEngineName() {
        return "wasm3";
    }
}
