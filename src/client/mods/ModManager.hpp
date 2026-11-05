#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "perlin/abi.hpp"
#include "perlin/host.hpp"

namespace perlin::mods {
    struct ModInfo {
        std::string name;
        bool enabled = true;
        std::string state;
        std::string error;
        int loads = 0;
        std::string displayName, description, version, author, website, iconPath, path;
        std::vector<std::string> hooks;
        std::vector<std::string> permissions;
        uint64_t size = 0;
        double lastEventMs = 0;
        std::string arch;
        bool hasSettings = false;
        uint32_t requested = 0;
        bool needsApproval = false;
        int priority = 0;
        double frameRate = 0;
    };

    struct PluginCommand {
        std::string plugin, name, usage, description;
    };

    struct PluginKey {
        std::string plugin, id, label, binding;
        bool ctrl = false, shift = false, alt = false;
        abi::KeyContext context = abi::KeyContext::InGame;
    };

    struct UiLayer {
        std::string plugin, name;
        int z = 0;
        std::vector<uint8_t> commands;
        uint64_t serial = 0;
    };

    struct GfxLayer {
        std::string plugin, name;
        int z = 0;
        std::shared_ptr<const std::vector<uint8_t>> data;
        uint64_t serial = 0;
    };
    struct GfxTexture {
        std::string plugin;
        uint32_t id = 0;
        int width = 0, height = 0;
        bool linear = true;
        uint64_t version = 0;
        std::shared_ptr<const std::vector<uint8_t>> rgba;
        uint64_t rectFrom = 0;
        int rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
    };

    struct CallContext {
        std::string plugin;
        uint32_t permissions = 0;
        std::string storageRoot;
        void* user = nullptr;
    };

    using OpHandler = std::function<abi::Status(CallContext&, abi::Reader&, abi::Writer&)>;

    class ModManager {
    public:
        static ModManager& Instance();

        void Start(const std::string& directory);
        void Stop();
        void SetDisabled(const std::vector<std::string>& names);
        void SetApproved(const std::vector<std::string>& entries);
        static uint32_t DangerousMask();
        std::vector<ModInfo> List() const;
        std::string Directory() const { return directory; }
        void ReloadAll();
        void ReloadMod(const std::string& name);
        void SetAutoReload(bool value) { autoReload = value; }

        void SetEnvironment(const std::string& gameVersion, const std::string& platform);
        void Register(abi::Op op, OpHandler handler);
        template <abi::Op O, class Fn> void Handle(Fn&& fn) {
            Register(O, abi::host::Typed<O, CallContext>(std::forward<Fn>(fn)));
        }
        template <abi::Event E> bool Dispatch(const abi::payload_t<E>& payload, double budgetMs = 250.0) {
            return Dispatch(E, abi::host::Encode<E>(payload), budgetMs);
        }
        void Update();

        bool Dispatch(abi::Event event, const abi::Writer& payload, double budgetMs = 250.0);
        bool Dispatch(abi::Event event, double budgetMs = 250.0) {
            return Dispatch(event, abi::Writer{}, budgetMs);
        }
        bool WantsEvent(abi::Event event) const;
        void DispatchTo(const std::string& plugin, abi::Event event, const abi::Writer& payload,
                        double budgetMs = 250.0);

        std::vector<PluginCommand> Commands() const;
        std::vector<PluginKey> Keys() const;
        void SetKeyBinding(const std::string& plugin, const std::string& id, const std::string& binding);
        std::vector<UiLayer> Layers() const;
        uint64_t LayerVersion() const { return layerVersion.load(); }
        std::vector<GfxLayer> GfxLayers() const;
        std::vector<GfxTexture> GfxTextures() const;
        bool GrabsMouse() const { return grabMouse.load(); }
        bool GrabsKeyboard() const { return grabKeyboard.load(); }
        std::vector<std::string> GrabbingPlugins() const;
        void ReleaseGrabs();
        uint64_t CommandVersion() const { return commandVersion.load(); }
        bool HasPlugins() const;

        bool HasSettings(const std::string& plugin) const;
        std::vector<abi::SettingDef> Settings(const std::string& plugin) const;
        bool ChangeSetting(const std::string& plugin, const std::string& id, const std::string& value);
        void ResetSettings(const std::string& plugin);
        void SettingsClosed(const std::string& plugin);

        void PostMain(std::function<void()> task);
        std::function<bool(const std::string& path, int& width, int& height, std::vector<uint8_t>& rgba)> imageLoader;
        void Queue(abi::Event event, abi::Writer payload);
        bool Wants(abi::Event event) const { return (wanted.load() >> static_cast<uint32_t>(event)) & 1; }
        void Pump();
        bool HasPermission(const std::string& plugin, abi::Permission p) const;
        uint64_t Tick() const { return tickCounter.load(); }
        void SetTick(uint64_t t) { tickCounter = t; }
        void NoteLayerChange() { layerVersion++; }

        struct Plugin;
        std::mutex mutex;

    private:
        ModManager() = default;
        std::atomic<uint64_t> wanted{0};
        bool modsSettled = false;
        size_t settledCount = 0;
        void RefreshWanted();
        ~ModManager();
        void Scan();
        void Load(Plugin& mod);
        void Unload(Plugin& mod);
        void ReadMeta(Plugin& mod);
        void ApplyApproval(Plugin& mod);
        std::map<std::string, uint32_t> approved;
        bool Call(Plugin& mod, abi::Event event, const abi::Writer& payload, double budgetMs,
                  bool& cancelled);
        int32_t HostCall(Plugin& mod, uint32_t op, const uint8_t* in, uint32_t inLen, uint8_t* out,
                         uint32_t outCap);
        void Revoke(Plugin& mod, const char* why);
        void Retire(Plugin& mod);
        std::string KeysFile(const Plugin& mod) const;
        void RegisterCore();
        void RegisterGfxOps();
        void LoadKeys(Plugin& mod);
        void SaveKeys(const Plugin& mod) const;
        Plugin* Find(const std::string& name) const;
        static void Worker(ModManager* self, Plugin* mod);
        void WorkerLoad(Plugin& mod);
        void WorkerEvent(Plugin& mod);
        void WorkerFree(Plugin& mod);
        void Spawn(Plugin& mod);
        std::string SettingsFile(const Plugin& mod) const;
        void LoadSettings(Plugin& mod);
        void ApplySavedSettings(Plugin& mod);
        void SaveSettings(Plugin& mod);
        void RegisterSettingOps();

        std::string directory;
        std::atomic<bool> running{false};
        std::vector<std::unique_ptr<Plugin>> mods;
        std::vector<std::unique_ptr<Plugin>> graveyard;
        std::vector<std::string> disabled;
        std::unordered_map<uint32_t, std::pair<OpHandler, uint32_t>> ops;
        std::string gameVersion, platform;
        bool reloadAll = false;
        std::vector<std::string> reloadNames;
        std::atomic<bool> autoReload{true};
        std::atomic<uint64_t> layerVersion{1};
        std::atomic<bool> grabMouse{false}, grabKeyboard{false};
        void RefreshGrabs();
        std::atomic<uint64_t> commandVersion{1};
        std::atomic<uint64_t> tickCounter{0};
        std::chrono::steady_clock::time_point lastScan;
        std::vector<std::function<void()>> mainQueue;
        std::chrono::steady_clock::time_point lastFlush;
        friend struct HostBridge;
    };
}
