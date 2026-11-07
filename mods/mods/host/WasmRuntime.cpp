#include "host/WasmRuntime.hpp"

#include <wasmedge/wasmedge.h>

#include <mutex>
#include <set>
#include <unordered_map>

extern "C" void PerlinWasmEdgeStop(WasmEdge_ExecutorContext* executor) noexcept;

namespace perlin::mods {
    namespace {
        constexpr uint32_t kMaxArgs = 16;
        constexpr uint32_t kMaxResults = 4;

        std::string ToString(WasmEdge_String s) {
            return std::string(s.Buf, s.Length);
        }

        std::string Message(WasmEdge_Result r) {
            const char* m = WasmEdge_ResultGetMessage(r);
            return m ? m : "unknown error";
        }

        void Quiet() {
            static std::once_flag once;
            std::call_once(once, [] { WasmEdge_LogOff(); });
        }

        uint64_t Widen(const WasmEdge_Value& v) {
            if (WasmEdge_ValTypeIsI64(v.Type))
                return static_cast<uint64_t>(WasmEdge_ValueGetI64(v));
            if (WasmEdge_ValTypeIsI32(v.Type))
                return static_cast<uint32_t>(WasmEdge_ValueGetI32(v));
            return 0;
        }

        WasmEdge_Value Narrow(WasmEdge_ValType type, uint64_t v) {
            if (WasmEdge_ValTypeIsI64(type))
                return WasmEdge_ValueGenI64(static_cast<int64_t>(v));
            return WasmEdge_ValueGenI32(static_cast<int32_t>(static_cast<uint32_t>(v)));
        }

        bool IntegerSignature(const WasmEdge_FunctionTypeContext* type, std::vector<WasmEdge_ValType>& params,
                              std::vector<WasmEdge_ValType>& results) {
            params.resize(WasmEdge_FunctionTypeGetParametersLength(type));
            results.resize(WasmEdge_FunctionTypeGetReturnsLength(type));
            WasmEdge_FunctionTypeGetParameters(type, params.data(), static_cast<uint32_t>(params.size()));
            WasmEdge_FunctionTypeGetReturns(type, results.data(), static_cast<uint32_t>(results.size()));
            if (params.size() > kMaxArgs || results.size() > kMaxResults)
                return false;
            for (const auto* list : {&params, &results})
                for (const WasmEdge_ValType& t : *list)
                    if (!WasmEdge_ValTypeIsI32(t) && !WasmEdge_ValTypeIsI64(t))
                        return false;
            return true;
        }
    }

    struct WasmInstance::Impl {
        struct Import {
            Impl* owner = nullptr;
            HostFn fn;
            std::string label;
            uint32_t argCount = 0;
            std::vector<WasmEdge_ValType> results;
        };

        WasmInstance* self = nullptr;
        WasmEdge_ConfigureContext* config = nullptr;
        WasmEdge_LoaderContext* loader = nullptr;
        WasmEdge_ValidatorContext* validator = nullptr;
        WasmEdge_ExecutorContext* executor = nullptr;
        WasmEdge_StoreContext* store = nullptr;
        WasmEdge_ASTModuleContext* ast = nullptr;
        std::vector<WasmEdge_ModuleInstanceContext*> hostModules;
        WasmEdge_ModuleInstanceContext* module = nullptr;
        WasmEdge_MemoryInstanceContext* memory = nullptr;
        std::vector<std::unique_ptr<Import>> imports;
        struct Export {
            WasmEdge_FunctionInstanceContext* fn = nullptr;
            bool ok = false;
            std::vector<WasmEdge_ValType> params, results;
        };
        std::unordered_map<std::string, Export> exports;
        std::mutex stopMutex;
        std::string hostError;
        WasmEdge_MemoryInstanceContext* frameMemory = nullptr;

        ~Impl() {
            {
                std::lock_guard lock(stopMutex);
                if (module)
                    WasmEdge_ModuleInstanceDelete(module);
                module = nullptr;
                for (WasmEdge_ModuleInstanceContext* m : hostModules)
                    WasmEdge_ModuleInstanceDelete(m);
                if (store)
                    WasmEdge_StoreDelete(store);
                if (executor)
                    WasmEdge_ExecutorDelete(executor);
                executor = nullptr;
            }
            if (ast)
                WasmEdge_ASTModuleDelete(ast);
            if (validator)
                WasmEdge_ValidatorDelete(validator);
            if (loader)
                WasmEdge_LoaderDelete(loader);
            if (config)
                WasmEdge_ConfigureDelete(config);
        }

        static WasmEdge_Result Trampoline(void* data, const WasmEdge_CallingFrameContext* frame,
                                          const WasmEdge_Value* params, WasmEdge_Value* returns) {
            auto* imp = static_cast<Import*>(data);
            Impl& impl = *imp->owner;
            uint64_t args[kMaxArgs] = {};
            uint64_t results[kMaxResults] = {};
            for (uint32_t i = 0; i < imp->argCount; i++)
                args[i] = Widen(params[i]);
            WasmEdge_MemoryInstanceContext* previous = impl.frameMemory;
            if (WasmEdge_MemoryInstanceContext* m = WasmEdge_CallingFrameGetMemoryInstance(frame, 0))
                impl.frameMemory = m;
            std::string error;
            HostContext ctx{*impl.self, error};
            const bool ok =
                imp->fn(ctx, args, imp->argCount, results, static_cast<uint32_t>(imp->results.size()));
            impl.frameMemory = previous;
            if (!ok) {
                impl.hostError = error.empty() ? imp->label + " failed" : error;
                return WasmEdge_ResultGen(WasmEdge_ErrCategory_UserLevelError, 1);
            }
            for (size_t i = 0; i < imp->results.size(); i++)
                returns[i] = Narrow(imp->results[i], results[i]);
            return WasmEdge_Result_Success;
        }
    };

    WasmInstance::WasmInstance() : impl(std::make_unique<Impl>()) {
        impl->self = this;
    }

    WasmInstance::~WasmInstance() = default;

    bool WasmInstance::Load(const uint8_t* bytes, size_t size, const Resolver& resolver, std::string& error) {
        Quiet();
        Impl& m = *impl;
        if (size > 0xFFFFFFFFu) {
            error = "module too large";
            return false;
        }
        m.config = WasmEdge_ConfigureCreate();
        WasmEdge_ConfigureRemoveProposal(m.config, WasmEdge_Proposal_Threads);
        WasmEdge_ConfigureSetMaxMemoryPage(m.config, 16384);
        m.loader = WasmEdge_LoaderCreate(m.config);
        m.validator = WasmEdge_ValidatorCreate(m.config);
        m.executor = WasmEdge_ExecutorCreate(m.config, nullptr);
        m.store = WasmEdge_StoreCreate();
        if (!m.loader || !m.validator || !m.executor || !m.store) {
            error = "could not start the WebAssembly engine";
            return false;
        }

        WasmEdge_Result r =
            WasmEdge_LoaderParseFromBuffer(m.loader, &m.ast, bytes, static_cast<uint32_t>(size));
        if (!WasmEdge_ResultOK(r)) {
            error = "parse: " + Message(r);
            return false;
        }
        r = WasmEdge_ValidatorValidate(m.validator, m.ast);
        if (!WasmEdge_ResultOK(r)) {
            error = "validate: " + Message(r);
            return false;
        }

        bool sawMemory = false;
        {
            const uint32_t n = WasmEdge_ASTModuleListExportsLength(m.ast);
            std::vector<const WasmEdge_ExportTypeContext*> list(n);
            WasmEdge_ASTModuleListExports(m.ast, list.data(), n);
            for (const WasmEdge_ExportTypeContext* e : list)
                if (WasmEdge_ExportTypeGetExternalType(e) == WasmEdge_ExternalType_Memory && !sawMemory) {
                    if (const WasmEdge_MemoryTypeContext* mt = WasmEdge_ExportTypeGetMemoryType(m.ast, e)) {
                        is64 = WasmEdge_LimitIs64Bit(WasmEdge_MemoryTypeGetLimit(mt));
                        sawMemory = true;
                    }
                }
        }

        {
            const uint32_t n = WasmEdge_ASTModuleListImportsLength(m.ast);
            std::vector<const WasmEdge_ImportTypeContext*> list(n);
            WasmEdge_ASTModuleListImports(m.ast, list.data(), n);
            std::vector<std::pair<std::string, WasmEdge_ModuleInstanceContext*>> hosts;
            std::set<std::string> linked;
            for (const WasmEdge_ImportTypeContext* imp : list) {
                const std::string modName = ToString(WasmEdge_ImportTypeGetModuleName(imp));
                const std::string name = ToString(WasmEdge_ImportTypeGetExternalName(imp));
                const std::string label = modName + "." + name;
                if (!linked.insert(label).second)
                    continue;
                const WasmEdge_ExternalType kind = WasmEdge_ImportTypeGetExternalType(imp);
                if (kind == WasmEdge_ExternalType_Memory) {
                    if (const WasmEdge_MemoryTypeContext* mt = WasmEdge_ImportTypeGetMemoryType(m.ast, imp))
                        if (!sawMemory)
                            is64 = WasmEdge_LimitIs64Bit(WasmEdge_MemoryTypeGetLimit(mt));
                    error = "imports its memory (" + label + "); plugins must define their own";
                    return false;
                }
                if (kind != WasmEdge_ExternalType_Function) {
                    error = "unsupported import " + label;
                    return false;
                }
                const WasmEdge_FunctionTypeContext* type = WasmEdge_ImportTypeGetFunctionType(m.ast, imp);
                std::vector<WasmEdge_ValType> params, results;
                if (!type || !IntegerSignature(type, params, results)) {
                    error = "import " + label + " has an unsupported signature";
                    return false;
                }
                HostFn fn = resolver(modName, name);
                if (!fn) {
                    error = "unknown import " + label;
                    return false;
                }
                auto entry = std::make_unique<Impl::Import>();
                entry->owner = &m;
                entry->fn = std::move(fn);
                entry->label = label;
                entry->argCount = static_cast<uint32_t>(params.size());
                entry->results = results;
                WasmEdge_ModuleInstanceContext* host = nullptr;
                for (auto& h : hosts)
                    if (h.first == modName)
                        host = h.second;
                if (!host) {
                    WasmEdge_String s =
                        WasmEdge_StringCreateByBuffer(modName.data(), static_cast<uint32_t>(modName.size()));
                    host = WasmEdge_ModuleInstanceCreate(s);
                    WasmEdge_StringDelete(s);
                    if (!host) {
                        error = "could not create host module " + modName;
                        return false;
                    }
                    hosts.emplace_back(modName, host);
                    m.hostModules.push_back(host);
                }
                WasmEdge_FunctionInstanceContext* f =
                    WasmEdge_FunctionInstanceCreate(type, &Impl::Trampoline, entry.get(), 0);
                if (!f) {
                    error = "could not create " + label;
                    return false;
                }
                WasmEdge_String s =
                    WasmEdge_StringCreateByBuffer(name.data(), static_cast<uint32_t>(name.size()));
                r = WasmEdge_ModuleInstanceAddFunction(host, s, f);
                WasmEdge_StringDelete(s);
                if (!WasmEdge_ResultOK(r)) {
                    WasmEdge_FunctionInstanceDelete(f);
                    error = "link " + label + ": " + Message(r);
                    return false;
                }
                m.imports.push_back(std::move(entry));
            }
            for (auto& h : hosts) {
                r = WasmEdge_ExecutorRegisterImport(m.executor, m.store, h.second);
                if (!WasmEdge_ResultOK(r)) {
                    error = "link " + h.first + ": " + Message(r);
                    return false;
                }
            }
        }

        r = WasmEdge_ExecutorInstantiate(m.executor, &m.module, m.store, m.ast);
        if (!WasmEdge_ResultOK(r)) {
            error = "instantiate: " + (m.hostError.empty() ? Message(r) : m.hostError);
            return false;
        }
        {
            WasmEdge_String s = WasmEdge_StringCreateByCString("memory");
            m.memory = WasmEdge_ModuleInstanceFindMemory(m.module, s);
            WasmEdge_StringDelete(s);
        }
        if (!m.memory) {
            const uint32_t n = WasmEdge_ModuleInstanceListMemoryLength(m.module);
            if (n) {
                std::vector<WasmEdge_String> names(n);
                WasmEdge_ModuleInstanceListMemory(m.module, names.data(), n);
                m.memory = WasmEdge_ModuleInstanceFindMemory(m.module, names[0]);
            }
        }
        if (!sawMemory && !m.memory) {
            error = "plugin does not export its memory";
            return false;
        }
        return true;
    }

    bool WasmInstance::HasExport(const char* name) const {
        if (!impl->module)
            return false;
        WasmEdge_String s = WasmEdge_StringCreateByCString(name);
        const bool found = WasmEdge_ModuleInstanceFindFunction(impl->module, s) != nullptr;
        WasmEdge_StringDelete(s);
        return found;
    }

    bool WasmInstance::Call(const char* name, std::initializer_list<uint64_t> args, uint64_t* result,
                            std::string& error) {
        Impl& m = *impl;
        if (!m.module) {
            error = "not loaded";
            return false;
        }
        auto it = m.exports.find(name);
        if (it == m.exports.end()) {
            Impl::Export e;
            WasmEdge_String s = WasmEdge_StringCreateByCString(name);
            e.fn = WasmEdge_ModuleInstanceFindFunction(m.module, s);
            WasmEdge_StringDelete(s);
            e.ok =
                e.fn && IntegerSignature(WasmEdge_FunctionInstanceGetFunctionType(e.fn), e.params, e.results);
            it = m.exports.emplace(name, std::move(e)).first;
        }
        const Impl::Export& e = it->second;
        if (!e.fn) {
            error = std::string("missing export ") + name;
            return false;
        }
        WasmEdge_FunctionInstanceContext* fn = e.fn;
        const std::vector<WasmEdge_ValType>& params = e.params;
        const std::vector<WasmEdge_ValType>& results = e.results;
        if (!e.ok || params.size() != args.size()) {
            error = std::string(name) + " has the wrong signature";
            return false;
        }
        WasmEdge_Value in[kMaxArgs];
        WasmEdge_Value out[kMaxResults];
        size_t i = 0;
        for (const uint64_t a : args) {
            in[i] = Narrow(params[i], a);
            i++;
        }
        m.hostError.clear();
        const WasmEdge_Result r =
            WasmEdge_ExecutorInvoke(m.executor, fn, in, static_cast<uint32_t>(args.size()), out,
                                    static_cast<uint32_t>(results.size()));
        if (!WasmEdge_ResultOK(r)) {
            if (!m.hostError.empty())
                error = m.hostError;
            else if (WasmEdge_ResultGetCode(r) == WasmEdge_ErrCode_Interrupted)
                error = "interrupted";
            else
                error = Message(r);
            return false;
        }
        if (result)
            *result = results.empty() ? 0 : Widen(out[0]);
        return true;
    }

    uint8_t* WasmInstance::Memory(uint64_t ptr, uint64_t len) {
        WasmEdge_MemoryInstanceContext* mem = impl->frameMemory ? impl->frameMemory : impl->memory;
        if (!mem)
            return nullptr;
        const uint64_t bytes = WasmEdge_MemoryInstanceGetPageSize(mem) * 65536ull;
        if (ptr > bytes || len > bytes - ptr)
            return nullptr;
        if (len == 0) {
            static uint8_t empty = 0;
            return &empty;
        }
        return WasmEdge_MemoryInstanceGetPointer(mem, ptr, len);
    }

    void WasmInstance::Interrupt() {
        std::lock_guard lock(impl->stopMutex);
        if (impl->executor)
            PerlinWasmEdgeStop(impl->executor);
    }

    std::string WasmEngineName() {
        return std::string("WasmEdge ") + WasmEdge_VersionGet();
    }
}
