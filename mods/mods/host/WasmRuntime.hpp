#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace perlin::mods {
    class WasmInstance {
    public:
        struct HostContext {
            WasmInstance& instance;
            std::string& error;
        };
        using HostFn = std::function<bool(HostContext& ctx, const uint64_t* args, uint32_t argCount,
                                          uint64_t* results, uint32_t resultCount)>;

        using Resolver = std::function<HostFn(const std::string& module, const std::string& name)>;

        WasmInstance();
        ~WasmInstance();
        WasmInstance(const WasmInstance&) = delete;
        WasmInstance& operator=(const WasmInstance&) = delete;

        bool Load(const uint8_t* bytes, size_t size, const Resolver& resolver, std::string& error);

        bool Is64() const { return is64; }
        bool HasExport(const char* name) const;

        bool Call(const char* name, std::initializer_list<uint64_t> args, uint64_t* result,
                  std::string& error);

        uint8_t* Memory(uint64_t ptr, uint64_t len);

        void Interrupt();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        bool is64 = false;
    };

    std::string WasmEngineName();
}
