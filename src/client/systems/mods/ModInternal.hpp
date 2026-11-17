#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "client/systems/mods/ModManager.hpp"
#include "client/systems/mods/ModSupport.hpp"
#include "util/JsonValue.hpp"
#include "host/PluginCore.hpp"
#include "host/WasmRuntime.hpp"

namespace perlin::mods {
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    inline thread_local const void* tlsPlugin = nullptr;
    inline constexpr const char* kRevoked = "plugin revoked";

    struct ModManager::Plugin {
        ModManager* owner = nullptr;

        std::string name, path, storage;
        fs::file_time_type stamp = fs::file_time_type::min();
        uintmax_t size = 0;
        bool enabled = true;
        std::string error;
        int loads = 0;
        ModInfo meta;
        uint32_t permissions = static_cast<uint32_t>(abi::Permission::Storage);
        uint32_t requested = 0, safePermissions = static_cast<uint32_t>(abi::Permission::Storage);
        bool needsApproval = false;
        struct Registrations {
            struct Tex {
                int w = 0, h = 0;
                bool linear = true;
                uint64_t version = 0;
                std::shared_ptr<const std::vector<uint8_t>> rgba;
                uint64_t rectFrom = 0;
                int rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
            };
            std::vector<PluginCommand> commands;
            std::vector<PluginKey> keys;
            std::map<std::string, UiLayer> layers;
            std::map<std::string, GfxLayer> gfxLayers;
            std::map<uint32_t, Tex> textures;
            uint32_t nextTexture = 1;
            uint64_t textureBytes = 0;
            std::atomic<uint8_t> grab{0};
        } reg;
        std::map<std::string, std::string> savedKeys;
        std::vector<abi::SettingDef> settings;
        std::map<std::string, std::string> savedSettings;
        bool settingsDirty = false, restartPending = false;
        std::string runtimeIcon;
        std::atomic<bool> loaded{false};
        std::atomic<bool> stuck{false};
        std::atomic<bool> revoked{false};
        AtomicSubscriptions subs;
        std::atomic<double> lastEventMs{0};
        Clock::time_point lastFrameAt{};
        float frameDtAccum = 0;
        uint64_t serial = 0;

        std::thread thread;
        struct Job {
            enum class Kind { None, Load, Event, Quit };
            std::mutex mutex;
            std::condition_variable wake, finished;
            Kind kind = Kind::None;
            abi::Event event = abi::Event::Tick;
            std::vector<uint8_t> payload;
            bool done = false;
            uint32_t flags = 0;
            std::string error;
        } job;
        std::vector<uint8_t> wasm;
        std::mutex gate;

        std::unique_ptr<WasmInstance> instance;
        std::string arch;
        uint32_t apiRevision = kDefaultApiRevision;
        GuestExports exports;
    };

    struct HostBridge {
        static bool Call(ModManager::Plugin& plugin, WasmInstance::HostContext& ctx, const uint64_t* args,
                         uint32_t count, uint64_t* results);
        static WasmInstance::Resolver Imports(ModManager::Plugin& plugin);
        static bool CallEvent(ModManager::Plugin& mod, uint32_t id, const std::vector<uint8_t>& payload,
                              uint32_t& flags, std::string& error);
    };

    void ParseManifestSettings(const json::Value& list, std::vector<abi::SettingDef>& out);
}
