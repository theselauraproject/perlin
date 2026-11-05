#include "client/mods/ModManager.hpp"

#include "client/mods/WasmRuntime.hpp"

#include "client/ui/Json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include "util/Log.hpp"

namespace perlin::mods {
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    namespace {
        thread_local const void* tlsPlugin = nullptr;
        const char* const kRevoked = "plugin revoked";
        const char* const kExit = "plugin exited";

        int64_t UnixMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        const char* EventName(uint32_t id) {
            switch (static_cast<abi::Event>(id)) {
            case abi::Event::Tick:
                return "tick";
            case abi::Event::Frame:
                return "frame";
            case abi::Event::ChatReceived:
                return "chat";
            case abi::Event::ChatSending:
                return "chat_sending";
            case abi::Event::Connected:
                return "connected";
            case abi::Event::Disconnected:
                return "disconnected";
            case abi::Event::Key:
                return "key";
            case abi::Event::Command:
                return "command";
            case abi::Event::Damage:
                return "damage";
            case abi::Event::Death:
                return "death";
            case abi::Event::Respawned:
                return "respawned";
            case abi::Event::DimensionChanged:
                return "dimension";
            case abi::Event::ScreenChanged:
                return "screen";
            case abi::Event::SlotChanged:
                return "slot";
            case abi::Event::EntityAdded:
                return "entity_added";
            case abi::Event::EntityRemoved:
                return "entity_removed";
            case abi::Event::Sound:
                return "sound";
            case abi::Event::HttpResponse:
                return "http";
            case abi::Event::Packet:
                return "packet";
            case abi::Event::Title:
                return "title";
            case abi::Event::SettingChanged:
                return "setting_changed";
            case abi::Event::Look:
                return "look";
            case abi::Event::MouseMove:
                return "mouse_move";
            case abi::Event::MouseButton:
                return "mouse_button";
            case abi::Event::MouseWheel:
                return "mouse_wheel";
            case abi::Event::KeyRaw:
                return "key_raw";
            case abi::Event::TextInput:
                return "text_input";
            case abi::Event::ScreenRender:
                return "screen_render";
            case abi::Event::SlotClick:
                return "slot_click";
            case abi::Event::Interact:
                return "interact";
            case abi::Event::ChunkLoaded:
                return "chunk_loaded";
            case abi::Event::ChunkUnloaded:
                return "chunk_unloaded";
            case abi::Event::BlockChanged:
                return "block_changed";
            case abi::Event::ResourcesReloaded:
                return "resources_reloaded";
            case abi::Event::LevelLoaded:
                return "level_loaded";
            case abi::Event::LevelUnloaded:
                return "level_unloaded";
            case abi::Event::ModsLoaded:
                return "mods_loaded";
            case abi::Event::ClientStopping:
                return "client_stopping";
            case abi::Event::Toast:
                return "toast";
            case abi::Event::ModMessage:
                return "mod_message";
            case abi::Event::Tooltip:
                return "tooltip";
            case abi::Event::ScreenButton:
                return "screen_button";
            case abi::Event::WindowFocus:
                return "window_focus";
            case abi::Event::ServerPing:
                return "server_ping";
            default:
                return nullptr;
            }
        }

        bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            out.assign(std::istreambuf_iterator<char>(in), {});
            return !out.empty();
        }

        std::string EmbeddedManifest(const std::string& path) {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return {};
            char magic[8];
            if (!in.read(magic, 8) || std::memcmp(magic, "\0asm", 4) != 0)
                return {};
            const auto leb = [&](uint64_t& value) {
                value = 0;
                for (int shift = 0; shift <= 63; shift += 7) {
                    const int c = in.get();
                    if (c == EOF)
                        return false;
                    value |= static_cast<uint64_t>(c & 0x7F) << shift;
                    if (!(c & 0x80))
                        return true;
                }
                return false;
            };
            for (int guard = 0; guard < 256; guard++) {
                const int id = in.get();
                if (id == EOF)
                    break;
                uint64_t size = 0;
                if (!leb(size))
                    break;
                const std::streamoff start = in.tellg();
                if (id == 0) {
                    uint64_t nameLen = 0;
                    if (!leb(nameLen) || nameLen > size || nameLen > 64)
                        break;
                    std::string name(static_cast<size_t>(nameLen), '\0');
                    if (nameLen && !in.read(name.data(), static_cast<std::streamsize>(nameLen)))
                        break;
                    if (name == "perlin_manifest") {
                        const uint64_t rest = size - (static_cast<uint64_t>(in.tellg() - start));
                        if (rest == 0 || rest > (64u << 10))
                            return {};
                        std::string text(static_cast<size_t>(rest), '\0');
                        if (!in.read(text.data(), static_cast<std::streamsize>(rest)))
                            return {};
                        return text;
                    }
                }
                in.clear();
                in.seekg(start + static_cast<std::streamoff>(size));
                if (!in)
                    break;
            }
            return {};
        }

        bool SafeRelative(const std::string& rel, fs::path& out) {
            if (rel.empty() || rel.size() > 240 || rel.contains('\\') || rel.contains('\0'))
                return false;
            const fs::path path = fs::path(rel).lexically_normal();
            if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
                return false;
            for (const auto& part : path) {
                const std::string s = part.string();
                if (s == ".." || s.empty())
                    return false;
            }
            out = path;
            return true;
        }

        void ParseManifestSettings(const ui::Json& list, std::vector<abi::SettingDef>& out);

        std::string Escape(const std::string& s) {
            std::string out;
            for (const char c : s) {
                if (c == '"' || c == '\\')
                    out += '\\';
                if (static_cast<unsigned char>(c) < 0x20)
                    continue;
                out += c;
            }
            return out;
        }
    }

    struct ModManager::Plugin {
        enum class Kind { None, Load, Event, Quit };

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
        std::vector<PluginCommand> commands;
        std::vector<PluginKey> keys;
        std::map<std::string, UiLayer> layers;
        std::map<std::string, GfxLayer> gfxLayers;
        struct Tex {
            int w = 0, h = 0;
            bool linear = true;
            uint64_t version = 0;
            std::shared_ptr<const std::vector<uint8_t>> rgba;
            uint64_t rectFrom = 0;
            int rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
        };
        std::map<uint32_t, Tex> textures;
        uint32_t nextTexture = 1;
        uint64_t textureBytes = 0;
        std::atomic<uint8_t> grab{0};
        std::map<std::string, std::string> savedKeys;
        std::vector<abi::SettingDef> settings;
        std::map<std::string, std::string> savedSettings;
        bool settingsDirty = false, restartPending = false;
        std::string runtimeIcon;
        std::atomic<bool> loaded{false};
        std::atomic<bool> stuck{false};
        std::atomic<bool> revoked{false};
        std::atomic<uint64_t> subs{~0ull};
        std::atomic<double> lastEventMs{0};
        uint64_t serial = 0;

        std::thread thread;
        std::mutex jobMutex;
        std::condition_variable jobCv, doneCv;
        Kind kind = Kind::None;
        abi::Event event = abi::Event::Tick;
        std::vector<uint8_t> payload;
        std::vector<uint8_t> wasm;
        bool done = false;
        uint32_t flags = 0;
        std::string jobError;
        std::mutex gate;

        std::unique_ptr<WasmInstance> instance;
        std::string arch;
        bool hasAlloc = false, hasFree = false, hasSubs = false;
    };

    struct HostBridge {
        static ModManager* manager;

        static bool Call(ModManager::Plugin& plugin, WasmInstance::HostContext& ctx, const uint64_t* a,
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
            const int32_t rc = manager->HostCall(plugin, op, in, static_cast<uint32_t>(a[2]), out,
                                                 static_cast<uint32_t>(a[4]));
            if (rc == INT32_MIN) {
                ctx.error = kRevoked;
                return false;
            }
            r[0] = static_cast<uint32_t>(rc);
            return true;
        }
    };
    ModManager* HostBridge::manager = nullptr;

    namespace {
        constexpr uint32_t kBadf = 8, kFault = 21, kNosys = 52, kNotcapable = 76, kSpipe = 70;

        using Wasi = WasmInstance::HostFn;

        bool PutSize(WasmInstance& w, uint64_t at, uint64_t value) {
            const uint64_t width = w.Is64() ? 8 : 4;
            uint8_t* p = w.Memory(at, width);
            if (!p)
                return false;
            std::memcpy(p, &value, width);
            return true;
        }

        Wasi Const(uint32_t code) {
            return [code](WasmInstance::HostContext&, const uint64_t*, uint32_t, uint64_t* r, uint32_t nr) {
                if (nr)
                    r[0] = code;
                return true;
            };
        }

        Wasi WasiFunction(ModManager::Plugin& plugin, const std::string& name) {
            using Ctx = WasmInstance::HostContext;
            if (name == "args_sizes_get" || name == "environ_sizes_get")
                return [](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
                    r[0] =
                        n >= 2 && PutSize(c.instance, a[0], 0) && PutSize(c.instance, a[1], 0) ? 0 : kFault;
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
                    const uint64_t ns =
                        a[0] == 0
                            ? static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                        std::chrono::system_clock::now().time_since_epoch())
                                                        .count())
                            : static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                        Clock::now().time_since_epoch())
                                                        .count());
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
                return [&plugin](Ctx& c, const uint64_t* a, uint32_t n, uint64_t* r, uint32_t) {
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
                    if (!text.empty())
                        std::cout << "[mod:" << plugin.name << "] " << text << std::endl;
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
                    c.error = kExit;
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

        WasmInstance::Resolver PluginImports(ModManager::Plugin& plugin) {
            return [&plugin](const std::string& module, const std::string& name) -> WasmInstance::HostFn {
                if (module == "perlin" && name == "call")
                    return [&plugin](WasmInstance::HostContext& c, const uint64_t* a, uint32_t n, uint64_t* r,
                                     uint32_t nr) {
                        if (nr != 1) {
                            c.error = "perlin.call must return i32";
                            return false;
                        }
                        return HostBridge::Call(plugin, c, a, n, r);
                    };
                if (module == "wasi_snapshot_preview1")
                    return WasiFunction(plugin, name);
                if (module == "env" && name == "emscripten_notify_memory_growth")
                    return Const(0);
                return {};
            };
        }
    }

    ModManager& ModManager::Instance() {
        static ModManager* instance = new ModManager();
        return *instance;
    }

    ModManager::~ModManager() {
        Stop();
    }

    void ModManager::SetEnvironment(const std::string& version, const std::string& platformName) {
        std::lock_guard lock(mutex);
        gameVersion = version;
        platform = platformName;
    }

    void ModManager::Register(abi::Op op, OpHandler handler) {
        ops[static_cast<uint32_t>(op)] = {std::move(handler), abi::PermissionOf(op)};
    }

    void ModManager::SetDisabled(const std::vector<std::string>& names) {
        std::lock_guard lock(mutex);
        disabled = names;
        lastScan = {};
    }

    void ModManager::ApplyApproval(Plugin& mod) {
        const auto granted = approved.find(mod.name);
        const uint32_t allowed = granted == approved.end() ? 0u : granted->second;
        mod.needsApproval = (mod.requested & ~allowed) != 0;
        mod.permissions = mod.safePermissions | (allowed & mod.requested);
        mod.meta.requested = mod.requested;
        mod.meta.needsApproval = mod.needsApproval;
    }

    uint32_t ModManager::DangerousMask() {
        return static_cast<uint32_t>(abi::Permission::Network) |
               static_cast<uint32_t>(abi::Permission::Packets) |
               static_cast<uint32_t>(abi::Permission::Control) |
               static_cast<uint32_t>(abi::Permission::Files) | static_cast<uint32_t>(abi::Permission::Native);
    }

    void ModManager::SetApproved(const std::vector<std::string>& entries) {
        std::lock_guard lock(mutex);
        approved.clear();
        for (const std::string& e : entries) {
            const size_t eq = e.rfind('=');
            if (eq == std::string::npos)
                continue;
            approved[e.substr(0, eq)] = static_cast<uint32_t>(std::strtoul(e.c_str() + eq + 1, nullptr, 10));
        }
        lastScan = {};
    }

    void ModManager::ReloadAll() {
        std::lock_guard lock(mutex);
        reloadAll = true;
        lastScan = {};
    }

    void ModManager::ReloadMod(const std::string& name) {
        std::lock_guard lock(mutex);
        reloadNames.push_back(name);
        lastScan = {};
    }

    void ModManager::Start(const std::string& dir) {
        HostBridge::manager = this;
        RegisterCore();
        directory = dir;
        std::error_code ec;
        fs::create_directories(directory, ec);
        running = true;
        lastScan = {};
    }

    void ModManager::Stop() {
        if (running && !mods.empty())
            Dispatch(abi::Event::ClientStopping, 500.0);
        if (!running.exchange(false) && mods.empty())
            return;
        std::vector<std::unique_ptr<Plugin>> taken;
        {
            std::lock_guard lock(mutex);
            taken.swap(mods);
        }
        for (auto& mod : taken)
            Unload(*mod);
        for (auto& mod : taken)
            if (mod->stuck)
                graveyard.push_back(std::move(mod));
        layerVersion++;
    }

    bool ModManager::HasPlugins() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        for (const auto& mod : mods)
            if (mod->loaded)
                return true;
        return false;
    }

    bool ModManager::HasPermission(const std::string& plugin, abi::Permission p) const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        for (const auto& mod : mods)
            if (mod->name == plugin)
                return (mod->permissions & static_cast<uint32_t>(p)) != 0;
        return false;
    }

    ModManager::Plugin* ModManager::Find(const std::string& name) const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        for (const auto& mod : mods)
            if (mod->name == name)
                return mod.get();
        return nullptr;
    }

    std::vector<ModInfo> ModManager::List() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<ModInfo> out;
        for (const auto& mod : mods) {
            ModInfo info = mod->meta;
            info.name = mod->name;
            info.enabled = mod->enabled;
            info.loads = mod->loads;
            info.error = mod->error;
            info.lastEventMs = mod->lastEventMs.load();
            info.arch = mod->arch;
            info.hasSettings = mod->loaded && !mod->settings.empty();
            if (!mod->runtimeIcon.empty())
                info.iconPath = mod->runtimeIcon;
            info.state = mod->needsApproval    ? "approval"
                         : !mod->enabled       ? "disabled"
                         : mod->stuck          ? "unresponsive"
                         : !mod->error.empty() ? "error"
                         : mod->loaded         ? "running"
                                               : "loading";
            info.hooks.clear();
            if (mod->loaded) {
                const uint64_t subs = mod->subs.load();
                for (uint32_t id = 1; id < 64; id++)
                    if ((subs & (1ull << id)) && id != static_cast<uint32_t>(abi::Event::HttpResponse))
                        if (const char* n = EventName(id))
                            info.hooks.push_back(n);
            }
            out.push_back(std::move(info));
        }
        std::sort(out.begin(), out.end(), [](const ModInfo& a, const ModInfo& b) { return a.name < b.name; });
        return out;
    }

    std::vector<PluginCommand> ModManager::Commands() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<PluginCommand> out;
        for (const auto& mod : mods)
            if (mod->loaded)
                out.insert(out.end(), mod->commands.begin(), mod->commands.end());
        return out;
    }

    std::vector<PluginKey> ModManager::Keys() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<PluginKey> out;
        for (const auto& mod : mods)
            if (mod->loaded)
                out.insert(out.end(), mod->keys.begin(), mod->keys.end());
        return out;
    }

    std::vector<UiLayer> ModManager::Layers() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<UiLayer> out;
        for (const auto& mod : mods)
            if (mod->loaded)
                for (const auto& entry : mod->layers)
                    out.push_back(entry.second);
        std::stable_sort(out.begin(), out.end(),
                         [](const UiLayer& a, const UiLayer& b) { return a.z < b.z; });
        return out;
    }

    std::vector<GfxLayer> ModManager::GfxLayers() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<GfxLayer> out;
        for (const auto& mod : mods)
            if (mod->loaded && !mod->revoked)
                for (const auto& entry : mod->gfxLayers)
                    out.push_back(entry.second);
        std::stable_sort(out.begin(), out.end(),
                         [](const GfxLayer& a, const GfxLayer& b) { return a.z < b.z; });
        return out;
    }

    std::vector<GfxTexture> ModManager::GfxTextures() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<GfxTexture> out;
        for (const auto& mod : mods)
            if (mod->loaded && !mod->revoked)
                for (auto& entry : mod->textures) {
                    Plugin::Tex& t = entry.second;
                    GfxTexture g{mod->name, entry.first, t.w, t.h, t.linear, t.version, t.rgba};
                    g.rectFrom = t.rectFrom;
                    g.rx0 = t.rx0;
                    g.ry0 = t.ry0;
                    g.rx1 = t.rx1;
                    g.ry1 = t.ry1;
                    out.push_back(std::move(g));
                    t.rectFrom = t.version;
                    t.rx0 = t.ry0 = t.rx1 = t.ry1 = 0;
                }
        return out;
    }

    std::vector<std::string> ModManager::GrabbingPlugins() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<std::string> out;
        for (const auto& mod : mods)
            if (mod->loaded && !mod->revoked && !mod->stuck && mod->grab.load())
                out.push_back(mod->name);
        return out;
    }

    void ModManager::RefreshGrabs() {
        bool mouse = false, keyboard = false;
        for (const auto& mod : mods)
            if (mod->loaded && !mod->revoked && !mod->stuck) {
                const uint8_t g = mod->grab.load();
                mouse = mouse || (g & 1);
                keyboard = keyboard || (g & 2);
            }
        grabMouse = mouse;
        grabKeyboard = keyboard;
    }

    void ModManager::ReleaseGrabs() {
        std::lock_guard lock(mutex);
        for (const auto& mod : mods)
            mod->grab = 0;
        RefreshGrabs();
    }

    std::string ModManager::KeysFile(const Plugin& mod) const {
        return (fs::path(directory) / (mod.name + ".keys.json")).string();
    }

    void ModManager::LoadKeys(Plugin& mod) {
        mod.savedKeys.clear();
        std::ifstream in(KeysFile(mod), std::ios::binary);
        if (!in)
            return;
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        ui::Json json;
        std::string error;
        if (!ui::Json::Parse(text, json, error) || !json.IsObject())
            return;
        for (const auto& entry : json.object)
            if (entry.second.IsString())
                mod.savedKeys[entry.first] = entry.second.string;
    }

    void ModManager::SaveKeys(const Plugin& mod) const {
        std::string text = "{\n";
        bool first = true;
        for (const auto& entry : mod.savedKeys) {
            if (!first)
                text += ",\n";
            first = false;
            text += "  \"" + Escape(entry.first) + "\": \"" + Escape(entry.second) + "\"";
        }
        text += "\n}\n";
        std::ofstream out(KeysFile(mod), std::ios::binary | std::ios::trunc);
        out << text;
    }

    void ModManager::SetKeyBinding(const std::string& plugin, const std::string& id,
                                   const std::string& binding) {
        std::lock_guard lock(mutex);
        for (auto& mod : mods) {
            if (mod->name != plugin)
                continue;
            for (auto& key : mod->keys)
                if (key.id == id)
                    key.binding = binding;
            mod->savedKeys[id] = binding;
            SaveKeys(*mod);
        }
    }

    int32_t ModManager::HostCall(Plugin& mod, uint32_t op, const uint8_t* in, uint32_t inLen, uint8_t* out,
                                 uint32_t outCap) {
        std::lock_guard gateLock(mod.gate);
        if (mod.revoked)
            return INT32_MIN;
        const auto it = ops.find(op);
        if (it == ops.end())
            return static_cast<int32_t>(abi::Status::UnknownOp);
        if ((mod.permissions & it->second.second) != it->second.second)
            return static_cast<int32_t>(abi::Status::Denied);
        CallContext ctx{mod.name, mod.permissions, mod.storage, &mod};
        abi::Reader reader(in, inLen);
        abi::Writer writer;
        const abi::Status status = it->second.first(ctx, reader, writer);
        if (status != abi::Status::Ok)
            return static_cast<int32_t>(status);
        if (writer.Size() > outCap)
            return -(abi::kNeedMoreBase + static_cast<int32_t>(std::min<size_t>(writer.Size(), 1u << 28)));
        if (writer.Size())
            std::memcpy(out, writer.Data(), writer.Size());
        return static_cast<int32_t>(writer.Size());
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
            mod.commands.clear();
            mod.keys.clear();
            mod.layers.clear();
            mod.gfxLayers.clear();
            mod.textures.clear();
            mod.textureBytes = 0;
            mod.grab = 0;
            RefreshGrabs();
            mod.settings.clear();
            mod.runtimeIcon.clear();
        }
        layerVersion++;
        commandVersion++;
        Log("mods", "{}: {}", mod.name, why);
    }

    void ModManager::Spawn(Plugin& mod) {
        mod.kind = Plugin::Kind::None;
        mod.thread = std::thread(&ModManager::Worker, this, &mod);
    }

    void ModManager::Worker(ModManager* self, Plugin* mod) {
        tlsPlugin = mod;
        for (;;) {
            std::unique_lock lock(mod->jobMutex);
            mod->jobCv.wait(lock, [&] { return mod->kind != Plugin::Kind::None; });
            const Plugin::Kind kind = mod->kind;
            mod->kind = Plugin::Kind::None;
            if (kind == Plugin::Kind::Quit)
                break;
            lock.unlock();
            if (kind == Plugin::Kind::Load)
                self->WorkerLoad(*mod);
            else
                self->WorkerEvent(*mod);
            lock.lock();
            mod->done = true;
            mod->doneCv.notify_all();
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
        mod.hasAlloc = mod.hasFree = mod.hasSubs = false;
    }

    namespace {
        bool CallEventFn(ModManager::Plugin& mod, uint32_t id, const std::vector<uint8_t>& payload,
                         uint32_t& flags, std::string& error) {
            WasmInstance& w = *mod.instance;
            uint64_t ptr = 0;
            const uint64_t len = payload.size();
            if (len && !mod.hasAlloc) {
                error = "plugin has no perlin_alloc export";
                return false;
            }
            if (len) {
                if (!w.Call("perlin_alloc", {len}, &ptr, error)) {
                    error = "alloc: " + error;
                    return false;
                }
                uint8_t* dst = ptr ? w.Memory(ptr, len) : nullptr;
                if (!dst) {
                    error = "alloc returned a bad pointer";
                    return false;
                }
                std::memcpy(dst, payload.data(), payload.size());
            }
            uint64_t result = 0;
            if (!w.Call("perlin_event", {id, ptr, len}, &result, error)) {
                error = std::string(EventName(id) ? EventName(id) : "event") + ": " + error;
                return false;
            }
            flags = static_cast<uint32_t>(result);
            std::string ignored;
            if (len && mod.hasFree)
                w.Call("perlin_free", {ptr}, nullptr, ignored);
            return true;
        }

        void ReadSubscriptions(ModManager::Plugin& mod) {
            uint64_t subs = 0;
            std::string ignored;
            if (!mod.instance->Call("perlin_subscriptions", {}, &subs, ignored))
                return;
            uint64_t mask = static_cast<uint32_t>(subs);
            uint64_t hi = 0;
            if (mod.instance->HasExport("perlin_subscriptions_hi") &&
                mod.instance->Call("perlin_subscriptions_hi", {}, &hi, ignored))
                mask |= static_cast<uint64_t>(static_cast<uint32_t>(hi)) << 32;
            mod.subs = mask;
        }
    }

    void ModManager::WorkerLoad(Plugin& mod) {
        mod.jobError.clear();
        {
            std::lock_guard gateLock(mod.gate);
            mod.instance = std::make_unique<WasmInstance>();
            if (mod.revoked)
                mod.instance->Interrupt();
        }
        std::string error;
        if (!mod.instance->Load(mod.wasm.data(), mod.wasm.size(), PluginImports(mod), error)) {
            mod.jobError = error;
            return;
        }
        if (!mod.instance->HasExport("perlin_event")) {
            mod.jobError = "not a Perlin plugin (missing perlin_event export)";
            return;
        }
        mod.hasAlloc = mod.instance->HasExport("perlin_alloc");
        mod.hasFree = mod.instance->HasExport("perlin_free");
        mod.hasSubs = mod.instance->HasExport("perlin_subscriptions");
        {
            std::lock_guard lock(mutex);
            mod.arch = mod.instance->Is64() ? "wasm64" : "wasm32";
        }
        Log("mods", "{}: {} on {}", mod.name, mod.arch, WasmEngineName());
        if (mod.instance->HasExport("_initialize") &&
            !mod.instance->Call("_initialize", {}, nullptr, error)) {
            mod.jobError = "_initialize: " + error;
            return;
        }
        uint32_t flags = 0;
        if (!CallEventFn(mod, static_cast<uint32_t>(abi::Event::Load), {}, flags, error)) {
            mod.jobError = error;
            return;
        }
        if (mod.hasSubs)
            ReadSubscriptions(mod);
    }

    void ModManager::WorkerEvent(Plugin& mod) {
        mod.jobError.clear();
        uint32_t flags = 0;
        std::string error;
        if (!CallEventFn(mod, static_cast<uint32_t>(mod.event), mod.payload, flags, error)) {
            mod.jobError = error;
            return;
        }
        mod.flags = flags;
        if (mod.hasSubs && mod.event != abi::Event::Frame && mod.event != abi::Event::Unload)
            ReadSubscriptions(mod);
    }

    bool ModManager::Call(Plugin& mod, abi::Event event, const abi::Writer& payload, double budgetMs,
                          bool& cancelled) {
        if (!mod.loaded || mod.stuck || tlsPlugin)
            return false;
        const auto started = Clock::now();
        {
            std::lock_guard lock(mod.jobMutex);
            mod.event = event;
            mod.payload = payload.data;
            mod.done = false;
            mod.flags = 0;
            mod.kind = Plugin::Kind::Event;
        }
        mod.jobCv.notify_all();
        std::unique_lock lock(mod.jobMutex);
        const bool finished = mod.doneCv.wait_for(lock, std::chrono::duration<double, std::milli>(budgetMs),
                                                  [&] { return mod.done; });
        if (!finished) {
            lock.unlock();
            Revoke(mod, "unresponsive: an event ran too long, the plugin was stopped");
            return false;
        }
        const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        mod.lastEventMs = elapsed;
        if (!mod.jobError.empty()) {
            const std::string message = mod.jobError;
            lock.unlock();
            std::lock_guard guard(mutex);
            mod.error = message;
            mod.loaded = false;
            mod.commands.clear();
            mod.keys.clear();
            mod.layers.clear();
            mod.gfxLayers.clear();
            mod.textures.clear();
            mod.textureBytes = 0;
            mod.grab = 0;
            RefreshGrabs();
            mod.settings.clear();
            mod.runtimeIcon.clear();
            layerVersion++;
            commandVersion++;
            Log("mods", "{} stopped: {}", mod.name, message);
            return false;
        }
        cancelled = (mod.flags & abi::kEventCancel) != 0;
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
        const uint64_t bit = 1ull << static_cast<uint32_t>(event);
        for (Plugin* mod : list)
            if (mod->subs.load() & bit)
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
        std::stable_sort(list.begin(), list.end(), [](const Plugin* a, const Plugin* b) {
            return a->meta.priority > b->meta.priority;
        });
        const uint64_t bit = 1ull << static_cast<uint32_t>(event);
        bool cancelled = false;
        for (Plugin* mod : list) {
            if (!(mod->subs.load() & bit))
                continue;
            bool c = false;
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

    void ModManager::Unload(Plugin& mod) {
        if (mod.loaded && !mod.stuck) {
            bool c = false;
            Call(mod, abi::Event::Unload, abi::Writer{}, 500.0, c);
        }
        SaveSettings(mod);
        mod.loaded = false;
        if (mod.thread.joinable()) {
            if (mod.stuck) {
                std::unique_lock lock(mod.jobMutex);
                if (mod.doneCv.wait_for(lock, std::chrono::milliseconds(500), [&] { return mod.done; })) {
                    mod.kind = Plugin::Kind::Quit;
                    lock.unlock();
                    mod.jobCv.notify_all();
                    mod.thread.join();
                } else {
                    lock.unlock();
                    mod.thread.detach();
                }
            } else {
                {
                    std::lock_guard lock(mod.jobMutex);
                    mod.kind = Plugin::Kind::Quit;
                }
                mod.jobCv.notify_all();
                mod.thread.join();
            }
        }
        {
            std::lock_guard lock(mutex);
            mod.commands.clear();
            mod.keys.clear();
            mod.layers.clear();
            mod.gfxLayers.clear();
            mod.textures.clear();
            mod.textureBytes = 0;
            mod.grab = 0;
            RefreshGrabs();
            mod.settings.clear();
            mod.runtimeIcon.clear();
        }
        layerVersion++;
        commandVersion++;
    }

    void ModManager::ReadMeta(Plugin& mod) {
        ModInfo meta;
        meta.path = mod.path;
        meta.size = mod.size;
        meta.displayName = mod.name;
        uint32_t permissions = static_cast<uint32_t>(abi::Permission::Storage);
        const fs::path base = fs::path(mod.path).replace_extension("");
        std::error_code ec;
        std::string manifestIcon;
        std::vector<abi::SettingDef> manifestSettings;
        std::string manifestText;
        {
            std::ifstream in(base.string() + ".json", std::ios::binary);
            if (in)
                manifestText.assign((std::istreambuf_iterator<char>(in)), {});
            else
                manifestText = EmbeddedManifest(mod.path);
        }
        if (!manifestText.empty()) {
            const std::string& text = manifestText;
            ui::Json json;
            std::string error;
            if (ui::Json::Parse(text, json, error) && json.IsObject()) {
                const auto get = [&](const char* key) {
                    const ui::Json* v = json.Find(key);
                    return v ? v->StringOr("") : std::string();
                };
                if (!get("name").empty())
                    meta.displayName = get("name");
                meta.description = get("description");
                meta.version = get("version");
                meta.author = get("author");
                meta.website = get("website");
                if (const ui::Json* v = json.Find("priority"))
                    meta.priority = static_cast<int>(std::clamp(v->NumberOr(0), -1000.0, 1000.0));
                manifestIcon = get("icon");
                if (const ui::Json* list = json.Find("settings"); list && list->IsArray())
                    ParseManifestSettings(*list, manifestSettings);
                if (const ui::Json* list = json.Find("permissions"); list && list->IsArray())
                    for (const ui::Json& item : list->array) {
                        const std::string p = item.StringOr("");
                        if (p == "network")
                            permissions |= static_cast<uint32_t>(abi::Permission::Network);
                        else if (p == "packets")
                            permissions |= static_cast<uint32_t>(abi::Permission::Packets);
                        else if (p == "control")
                            permissions |= static_cast<uint32_t>(abi::Permission::Control);
                        else if (p == "files")
                            permissions |= static_cast<uint32_t>(abi::Permission::Files);
                        else if (p == "native")
                            permissions |= static_cast<uint32_t>(abi::Permission::Native);
                    }
            }
        }
        if (permissions & static_cast<uint32_t>(abi::Permission::Network))
            meta.permissions.push_back("network");
        if (permissions & static_cast<uint32_t>(abi::Permission::Packets))
            meta.permissions.push_back("packets");
        if (permissions & static_cast<uint32_t>(abi::Permission::Control))
            meta.permissions.push_back("control");
        if (permissions & static_cast<uint32_t>(abi::Permission::Files))
            meta.permissions.push_back("files");
        if (permissions & static_cast<uint32_t>(abi::Permission::Native))
            meta.permissions.push_back("native");
        meta.permissions.push_back("storage");
        {
            std::vector<fs::path> candidates;
            fs::path rel;
            if (!manifestIcon.empty() && SafeRelative(manifestIcon, rel))
                candidates.push_back(fs::path(directory) / rel);
            candidates.push_back(base.string() + ".png");
            candidates.push_back(fs::path(directory) / (mod.name + ".data") / "pack_icon.png");
            candidates.push_back(fs::path(directory) / (mod.name + ".data") / "icon.png");
            for (const fs::path& c : candidates)
                if (fs::is_regular_file(c, ec)) {
                    meta.iconPath = c.string();
                    break;
                }
        }
        std::lock_guard lock(mutex);
        mod.settings = std::move(manifestSettings);
        mod.requested = permissions & DangerousMask();
        mod.safePermissions = permissions & ~DangerousMask();
        mod.meta = std::move(meta);
        ApplyApproval(mod);
    }

    void ModManager::Load(Plugin& mod) {
        mod.error.clear();
        mod.revoked = false;
        mod.stuck = false;
        ReadMeta(mod);
        mod.storage = (fs::path(directory) / (mod.name + ".data")).string();
        LoadKeys(mod);
        LoadSettings(mod);
        ApplySavedSettings(mod);
        if (!ReadFile(mod.path, mod.wasm)) {
            std::lock_guard lock(mutex);
            mod.error = "cannot read file";
            return;
        }
        mod.subs = ~0ull;
        Spawn(mod);
        {
            std::lock_guard lock(mod.jobMutex);
            mod.done = false;
            mod.kind = Plugin::Kind::Load;
        }
        mod.jobCv.notify_all();
        std::unique_lock lock(mod.jobMutex);
        const bool finished =
            mod.doneCv.wait_for(lock, std::chrono::milliseconds(4000), [&] { return mod.done; });
        if (!finished) {
            lock.unlock();
            Revoke(mod, "unresponsive: the plugin did not finish loading");
            return;
        }
        if (!mod.jobError.empty()) {
            const std::string message = mod.jobError;
            lock.unlock();
            {
                std::lock_guard guard(mutex);
                mod.error = message;
            }
            Log("mods", "{}: {}", mod.name, message);
            Unload(mod);
            return;
        }
        lock.unlock();
        {
            std::lock_guard guard(mutex);
            mod.loads++;
            mod.loaded = true;
        }
        Log("mods", "loaded {}{}", mod.name, (mod.loads > 1 ? " (reloaded)" : ""));
        layerVersion++;
    }

    void ModManager::Scan() {
        std::error_code ec;
        std::vector<std::string> present;
        std::vector<std::string> forced;
        bool all;
        std::vector<std::string> off;
        {
            std::lock_guard lock(mutex);
            forced.swap(reloadNames);
            all = reloadAll;
            reloadAll = false;
            off = disabled;
        }
        for (const auto& entry : fs::directory_iterator(directory, ec)) {
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".wasm")
                continue;
            const std::string name = entry.path().stem().string();
            present.push_back(name);
            const auto stamp = entry.last_write_time(ec);
            const uintmax_t size = entry.file_size(ec);
            Plugin* mod = Find(name);
            if (!mod) {
                auto created = std::make_unique<Plugin>();
                created->name = name;
                created->path = entry.path().string();
                mod = created.get();
                std::lock_guard lock(mutex);
                mods.push_back(std::move(created));
            }
            if (mod->meta.path.empty())
                ReadMeta(*mod);
            {
                std::lock_guard lock(mutex);
                ApplyApproval(*mod);
            }
            mod->enabled = std::find(off.begin(), off.end(), name) == off.end() && !mod->needsApproval;
            const bool first = mod->stamp == fs::file_time_type::min();
            const bool force = all || std::find(forced.begin(), forced.end(), name) != forced.end();
            const bool changed = stamp != mod->stamp || size != mod->size;
            const bool alive = mod->loaded || mod->thread.joinable();
            if (!mod->enabled) {
                if (alive || mod->stuck)
                    Unload(*mod);
                if (mod->stuck) {
                    mod->stuck = false;
                    mod->revoked = false;
                }
                mod->stamp = stamp;
                mod->size = size;
                {
                    std::lock_guard lock(mutex);
                    mod->error.clear();
                }
                ReadMeta(*mod);
                continue;
            }
            const bool needLoad =
                !mod->loaded && mod->error.empty() && !mod->stuck && !mod->thread.joinable();
            const bool reload = force || (changed && (autoReload || first || !mod->loaded));
            if (!reload && !needLoad)
                continue;
            if (reload && !first)
                Log("mods", "{}{}", name, (force ? " reloading" : " changed, reloading"));
            if (alive || mod->stuck) {
                const bool wasStuck = mod->stuck;
                Unload(*mod);
                if (wasStuck) {
                    std::lock_guard lock(mutex);
                    for (auto it = mods.begin(); it != mods.end(); ++it)
                        if (it->get() == mod) {
                            auto fresh = std::make_unique<Plugin>();
                            fresh->name = mod->name;
                            fresh->path = mod->path;
                            fresh->loads = mod->loads;
                            fresh->enabled = mod->enabled;
                            graveyard.push_back(std::move(*it));
                            *it = std::move(fresh);
                            mod = it->get();
                            break;
                        }
                }
            }
            mod->stamp = stamp;
            mod->size = size;
            Load(*mod);
        }
        std::vector<std::unique_ptr<Plugin>> gone;
        {
            std::lock_guard lock(mutex);
            for (auto it = mods.begin(); it != mods.end();) {
                if (std::find(present.begin(), present.end(), (*it)->name) == present.end()) {
                    gone.push_back(std::move(*it));
                    it = mods.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& mod : gone) {
            Unload(*mod);
            if (mod->stuck)
                graveyard.push_back(std::move(mod));
        }
    }

    void ModManager::Update() {
        if (!running || tlsPlugin)
            return;
        const auto now = Clock::now();
        if (lastScan != Clock::time_point{} && now - lastScan < std::chrono::milliseconds(500))
            return;
        lastScan = now;
        Scan();
        RefreshWanted();
        size_t loaded = 0;
        bool pending = false;
        {
            std::lock_guard lock(mutex);
            for (const auto& mod : mods) {
                if (mod->loaded)
                    loaded++;
                else if (mod->enabled && !mod->needsApproval && mod->error.empty())
                    pending = true;
            }
        }
        if (!pending && loaded && (!modsSettled || loaded != settledCount)) {
            modsSettled = true;
            settledCount = loaded;
            Dispatch(abi::Event::ModsLoaded);
        }
    }

    void ModManager::RefreshWanted() {
        uint64_t mask = 0;
        std::lock_guard lock(mutex);
        for (const auto& mod : mods)
            if (mod->loaded && !mod->stuck)
                mask |= mod->subs.load();
        wanted = mask;
    }

    void ModManager::Queue(abi::Event event, abi::Writer payload) {
        if (!Wants(event))
            return;
        PostMain([this, event, payload = std::move(payload)] { Dispatch(event, payload); });
    }

    namespace {
        std::string FormatNumber(double v) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.6g", v);
            return buf;
        }

        bool ParseNumber(const std::string& text, double& out) {
            if (text.empty())
                return false;
            char* end = nullptr;
            out = std::strtod(text.c_str(), &end);
            return end && *end == 0 && std::isfinite(out);
        }

        bool NormalizeSetting(const abi::SettingDef& def, const std::string& in, std::string& out) {
            switch (def.kind) {
            case abi::SettingKind::Toggle: {
                std::string v = in;
                std::transform(v.begin(), v.end(), v.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (v == "true" || v == "1" || v == "on" || v == "yes") {
                    out = "true";
                    return true;
                }
                if (v == "false" || v == "0" || v == "off" || v == "no") {
                    out = "false";
                    return true;
                }
                return false;
            }
            case abi::SettingKind::Integer:
            case abi::SettingKind::Number: {
                double v = 0;
                if (!ParseNumber(in, v))
                    return false;
                const double lo = std::min(def.min, def.max), hi = std::max(def.min, def.max);
                v = std::clamp(v, lo, hi);
                double step = def.step;
                if (def.kind == abi::SettingKind::Integer)
                    step = std::max(1.0, std::round(step));
                if (step > 0)
                    v = std::clamp(lo + std::round((v - lo) / step) * step, lo, hi);
                if (def.kind == abi::SettingKind::Integer)
                    v = std::round(v);
                out = FormatNumber(v);
                return true;
            }
            case abi::SettingKind::Choice:
                if (std::find(def.choices.begin(), def.choices.end(), in) == def.choices.end())
                    return false;
                out = in;
                return true;
            default:
                return false;
            }
        }

        bool ValidSettingId(const std::string& id) {
            if (id.empty() || id.size() > 64)
                return false;
            for (const char c : id)
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-'))
                    return false;
            return true;
        }
    }

    namespace {
        std::string SnakeToPascal(const std::string& in) {
            std::string out;
            bool up = true;
            for (const char c : in) {
                if (c == '_' || c == '-' || c == ' ') {
                    up = true;
                    continue;
                }
                out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
                up = false;
            }
            return out;
        }

        void ParseManifestSettings(const ui::Json& list, std::vector<abi::SettingDef>& out) {
            int anonymous = 0;
            for (const ui::Json& item : list.array) {
                if (!item.IsObject() || out.size() >= 160)
                    continue;
                const auto text = [&](const char* key) {
                    const ui::Json* v = item.Find(key);
                    return v ? v->StringOr("") : std::string();
                };
                abi::SettingDef def;
                const std::string type = text("type");
                if (type == "toggle")
                    def.kind = abi::SettingKind::Toggle;
                else if (type == "integer")
                    def.kind = abi::SettingKind::Integer;
                else if (type == "number")
                    def.kind = abi::SettingKind::Number;
                else if (type == "choice")
                    def.kind = abi::SettingKind::Choice;
                else if (type == "button")
                    def.kind = abi::SettingKind::Button;
                else if (type == "label")
                    def.kind = abi::SettingKind::Label;
                else if (type == "header")
                    def.kind = abi::SettingKind::Header;
                else
                    continue;
                def.id = text("id");
                const bool decor =
                    def.kind == abi::SettingKind::Label || def.kind == abi::SettingKind::Header;
                if (def.id.empty() && decor)
                    def.id = "_" + type + std::to_string(anonymous++);
                if (!ValidSettingId(def.id))
                    continue;
                def.label = text("label");
                if (def.label.empty() && def.kind == abi::SettingKind::Label)
                    def.label = text("text");
                def.description = text("description");
                def.unit = text("unit");
                if (const ui::Json* icon = item.Find("icon")) {
                    if (icon->IsString()) {
                        def.icon = icon->string;
                    } else if (icon->IsObject()) {
                        for (const char* kind : {"item", "texture", "file"})
                            if (const ui::Json* v = icon->Find(kind); v && v->IsString())
                                def.icon = std::string(kind) + ":" + v->string;
                    }
                }
                def.min = item.Find("min") ? item.Find("min")->NumberOr(0) : 0;
                def.max = item.Find("max") ? item.Find("max")->NumberOr(1) : 1;
                def.step = item.Find("step") ? item.Find("step")->NumberOr(0) : 0;
                if (const ui::Json* r = item.Find("restart"))
                    def.restart = r->BoolOr(false);
                if (const ui::Json* choices = item.Find("choices"); choices && choices->IsArray())
                    for (const ui::Json& c : choices->array) {
                        if (def.choices.size() >= 64)
                            break;
                        if (c.IsString()) {
                            def.choices.push_back(c.string);
                            def.choiceLabels.push_back("");
                        } else if (c.IsObject()) {
                            const ui::Json* value = c.Find("value");
                            const ui::Json* label = c.Find("label");
                            if (value && value->IsString()) {
                                def.choices.push_back(value->string);
                                def.choiceLabels.push_back(label ? label->StringOr("") : std::string());
                            }
                        }
                    }
                if (def.kind == abi::SettingKind::Choice && def.choices.empty())
                    continue;
                std::string fallback;
                if (const ui::Json* d = item.Find("default")) {
                    if (d->IsBool())
                        fallback = d->boolean ? "true" : "false";
                    else if (d->IsNumber())
                        fallback = FormatNumber(d->number);
                    else if (d->IsString())
                        fallback = d->string;
                }
                if (def.kind == abi::SettingKind::Choice && fallback.empty())
                    fallback = def.choices.front();
                if (def.kind == abi::SettingKind::Toggle && fallback.empty())
                    fallback = "false";
                if (def.kind == abi::SettingKind::Integer || def.kind == abi::SettingKind::Number) {
                    if (fallback.empty())
                        fallback = FormatNumber(def.min);
                }
                if (!decor && def.kind != abi::SettingKind::Button) {
                    std::string normal;
                    if (!NormalizeSetting(def, fallback, normal))
                        continue;
                    fallback = normal;
                }
                def.value = def.fallback = fallback;
                out.push_back(std::move(def));
            }
        }
    }

    void ModManager::ApplySavedSettings(Plugin& mod) {
        std::lock_guard lock(mutex);
        for (abi::SettingDef& def : mod.settings) {
            const auto saved = mod.savedSettings.find(def.id);
            std::string normal;
            if (saved != mod.savedSettings.end() && NormalizeSetting(def, saved->second, normal))
                def.value = normal;
        }
    }

    std::string ModManager::SettingsFile(const Plugin& mod) const {
        return (fs::path(directory) / (mod.name + ".settings.json")).string();
    }

    void ModManager::LoadSettings(Plugin& mod) {
        mod.savedSettings.clear();
        std::ifstream in(SettingsFile(mod), std::ios::binary);
        if (!in)
            return;
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        ui::Json json;
        std::string error;
        if (!ui::Json::Parse(text, json, error) || !json.IsObject())
            return;
        for (const auto& entry : json.object)
            if (entry.second.IsString())
                mod.savedSettings[entry.first] = entry.second.string;
    }

    void ModManager::SaveSettings(Plugin& mod) {
        if (!mod.settingsDirty)
            return;
        mod.settingsDirty = false;
        std::string text = "{\n";
        bool first = true;
        for (const auto& entry : mod.savedSettings) {
            if (!first)
                text += ",\n";
            first = false;
            text += "  \"" + Escape(entry.first) + "\": \"" + Escape(entry.second) + "\"";
        }
        text += "\n}\n";
        std::ofstream out(SettingsFile(mod), std::ios::binary | std::ios::trunc);
        out << text;
    }

    bool ModManager::HasSettings(const std::string& plugin) const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        for (const auto& mod : mods)
            if (mod->name == plugin)
                return mod->loaded && !mod->settings.empty();
        return false;
    }

    std::vector<abi::SettingDef> ModManager::Settings(const std::string& plugin) const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        for (const auto& mod : mods)
            if (mod->name == plugin)
                return mod->settings;
        return {};
    }

    bool ModManager::ChangeSetting(const std::string& plugin, const std::string& id,
                                   const std::string& value) {
        Plugin* mod = Find(plugin);
        if (!mod)
            return false;
        abi::EventSettingChanged event;
        event.id = id;
        {
            std::lock_guard lock(mutex);
            abi::SettingDef* def = nullptr;
            for (auto& d : mod->settings)
                if (d.id == id)
                    def = &d;
            if (!def)
                return false;
            event.kind = def->kind;
            if (def->kind == abi::SettingKind::Button) {
                event.value = "pressed";
            } else {
                std::string normal;
                if (!NormalizeSetting(*def, value, normal) || normal == def->value)
                    return false;
                def->value = normal;
                mod->savedSettings[id] = normal;
                mod->settingsDirty = true;
                if (def->restart)
                    mod->restartPending = true;
                event.value = normal;
            }
        }
        if (mod->loaded && (mod->subs.load() & (1ull << static_cast<uint32_t>(abi::Event::SettingChanged)))) {
            abi::Writer w;
            w.write(event);
            bool cancelled = false;
            Call(*mod, abi::Event::SettingChanged, w, 250.0, cancelled);
        }
        return true;
    }

    void ModManager::ResetSettings(const std::string& plugin) {
        for (const abi::SettingDef& def : Settings(plugin))
            if (def.kind != abi::SettingKind::Button && def.kind != abi::SettingKind::Label &&
                def.kind != abi::SettingKind::Header)
                ChangeSetting(plugin, def.id, def.fallback);
    }

    void ModManager::SettingsClosed(const std::string& plugin) {
        Plugin* mod = Find(plugin);
        if (!mod)
            return;
        SaveSettings(*mod);
        bool reload = false;
        {
            std::lock_guard lock(mutex);
            reload = mod->restartPending;
            mod->restartPending = false;
        }
        if (reload)
            ReloadMod(plugin);
    }

    void ModManager::RegisterSettingOps() {
        Register(abi::Op::SettingGet, [this](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            const std::string id = r.str();
            if (!r.Ok())
                return abi::Status::BadArguments;
            auto* mod = static_cast<Plugin*>(ctx.user);
            std::lock_guard lock(mutex);
            for (const auto& d : mod->settings)
                if (d.id == id) {
                    w.raw(d.value.data(), d.value.size());
                    return abi::Status::Ok;
                }
            return abi::Status::NotFound;
        });

        Register(abi::Op::SettingSet, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string id = r.str();
            const std::string value = r.str();
            if (!r.Ok())
                return abi::Status::BadArguments;
            auto* mod = static_cast<Plugin*>(ctx.user);
            std::lock_guard lock(mutex);
            for (auto& d : mod->settings)
                if (d.id == id) {
                    std::string normal;
                    if (!NormalizeSetting(d, value, normal))
                        return abi::Status::BadArguments;
                    d.value = normal;
                    mod->savedSettings[id] = normal;
                    mod->settingsDirty = true;
                    return abi::Status::Ok;
                }
            return abi::Status::NotFound;
        });

        Register(abi::Op::SettingList, [this](CallContext& ctx, abi::Reader&, abi::Writer& w) {
            auto* mod = static_cast<Plugin*>(ctx.user);
            std::lock_guard lock(mutex);
            std::vector<abi::SettingEntry> out;
            for (const auto& d : mod->settings)
                if (d.kind != abi::SettingKind::Button && d.kind != abi::SettingKind::Label &&
                    d.kind != abi::SettingKind::Header)
                    out.push_back({d.id, d.value});
            w.write(out);
            return abi::Status::Ok;
        });

        Register(abi::Op::ModList, [this](CallContext& ctx, abi::Reader&, abi::Writer& w) {
            std::vector<abi::ModEntry> out;
            for (const ModInfo& info : List()) {
                abi::ModEntry e;
                e.name = info.name;
                e.displayName = info.displayName;
                e.description = info.description;
                e.version = info.version;
                e.author = info.author;
                e.state = info.state;
                e.enabled = info.enabled;
                e.self = info.name == ctx.plugin;
                e.hasSettings = info.hasSettings;
                e.website = info.website;
                e.arch = info.arch;
                e.hooks = info.hooks;
                e.priority = info.priority;
                for (const std::string& p : info.permissions)
                    e.permissions |= p == "network"   ? abi::Permission::Network
                                     : p == "packets" ? abi::Permission::Packets
                                     : p == "control" ? abi::Permission::Control
                                     : p == "files"   ? abi::Permission::Files
                                     : p == "native"  ? abi::Permission::Native
                                     : p == "storage" ? abi::Permission::Storage
                                                      : abi::Permission::None;
                out.push_back(std::move(e));
            }
            w.write(out);
            return abi::Status::Ok;
        });

        Register(abi::Op::IconSet, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string rel = r.str();
            fs::path safe;
            if (!r.Ok() || !SafeRelative(rel, safe))
                return abi::Status::BadArguments;
            const fs::path full = fs::path(ctx.storageRoot) / safe;
            std::error_code ec;
            if (full.extension() != ".png" || !fs::is_regular_file(full, ec))
                return abi::Status::NotFound;
            std::lock_guard lock(mutex);
            static_cast<Plugin*>(ctx.user)->runtimeIcon = full.string();
            return abi::Status::Ok;
        });
    }

    void ModManager::PostMain(std::function<void()> task) {
        std::lock_guard lock(mutex);
        if (mainQueue.size() < 512)
            mainQueue.push_back(std::move(task));
    }

    void ModManager::Pump() {
        if (tlsPlugin)
            return;
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard lock(mutex);
            batch.swap(mainQueue);
        }
        for (auto& task : batch)
            task();
        const auto now = Clock::now();
        if (now - lastFlush > std::chrono::milliseconds(500)) {
            lastFlush = now;
            std::lock_guard lock(mutex);
            for (auto& mod : mods)
                SaveSettings(*mod);
        }
    }

    void ModManager::RegisterCore() {
        Register(abi::Op::Log, [](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const int32_t level = r.get<int32_t>();
            const std::string text = r.str();
            if (!r.Ok())
                return abi::Status::BadArguments;
            static const char* const names[] = {"debug", "info", "warn", "error"};
            std::cout << "[mod:" << ctx.plugin << ":" << names[std::clamp(level, 0, 3)] << "] "
                      << text.substr(0, 2000) << std::endl;
            return abi::Status::Ok;
        });

        Register(abi::Op::Clock, [](CallContext&, abi::Reader&, abi::Writer& w) {
            w.put<int64_t>(UnixMs());
            return abi::Status::Ok;
        });

        Register(abi::Op::Meta, [this](CallContext& ctx, abi::Reader&, abi::Writer& w) {
            abi::Meta meta;
            {
                std::lock_guard lock(mutex);
                meta.gameVersion = gameVersion;
                meta.platform = platform;
            }
            meta.plugin = ctx.plugin;
            meta.permissions = abi::Permissions::from_bits(ctx.permissions);
            meta.nowMs = UnixMs();
            meta.tick = tickCounter.load();
            w.write(meta);
            return abi::Status::Ok;
        });
        const auto resolve = [](CallContext& ctx, const std::string& rel, fs::path& out,
                                bool allowEmpty = false) {
            fs::path relative;
            if (rel.empty() && allowEmpty)
                relative = "";
            else if (!SafeRelative(rel, relative))
                return false;
            out = fs::path(ctx.storageRoot) / relative;
            return true;
        };

        Register(abi::Op::StorageRead, [resolve](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            fs::path path;
            if (!resolve(ctx, r.str(), path))
                return abi::Status::BadArguments;
            std::error_code ec;
            if (!fs::is_regular_file(path, ec))
                return abi::Status::NotFound;
            if (fs::file_size(path, ec) > (16u << 20))
                return abi::Status::Failed;
            std::vector<uint8_t> bytes;
            std::ifstream in(path, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), {});
            w.raw(bytes.data(), bytes.size());
            return abi::Status::Ok;
        });

        Register(abi::Op::StorageWrite, [resolve](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            fs::path path;
            if (!resolve(ctx, r.str(), path))
                return abi::Status::BadArguments;
            const std::vector<uint8_t> bytes = r.bytes();
            if (!r.Ok() || bytes.size() > (16u << 20))
                return abi::Status::BadArguments;
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
                return abi::Status::Failed;
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            return out ? abi::Status::Ok : abi::Status::Failed;
        });

        Register(abi::Op::StorageExists, [resolve](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            fs::path path;
            if (!resolve(ctx, r.str(), path))
                return abi::Status::BadArguments;
            std::error_code ec;
            w.boolean(fs::exists(path, ec));
            return abi::Status::Ok;
        });

        Register(abi::Op::StorageRemove, [resolve](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            fs::path path;
            if (!resolve(ctx, r.str(), path))
                return abi::Status::BadArguments;
            std::error_code ec;
            if (!fs::exists(path, ec))
                return abi::Status::NotFound;
            fs::remove_all(path, ec);
            return ec ? abi::Status::Failed : abi::Status::Ok;
        });

        Register(abi::Op::StorageMakeDir, [resolve](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            fs::path path;
            if (!resolve(ctx, r.str(), path))
                return abi::Status::BadArguments;
            std::error_code ec;
            fs::create_directories(path, ec);
            return ec ? abi::Status::Failed : abi::Status::Ok;
        });

        Register(abi::Op::StorageList, [resolve](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            fs::path path;
            if (!resolve(ctx, r.str(), path, true))
                return abi::Status::BadArguments;
            std::error_code ec;
            std::vector<std::string> names;
            if (fs::is_directory(path, ec))
                for (const auto& entry : fs::directory_iterator(path, ec)) {
                    std::string name = entry.path().filename().string();
                    if (entry.is_directory(ec))
                        name += "/";
                    names.push_back(std::move(name));
                    if (names.size() >= 4096)
                        break;
                }
            std::sort(names.begin(), names.end());
            w.put<uint32_t>(static_cast<uint32_t>(names.size()));
            for (const auto& name : names)
                w.str(name);
            return abi::Status::Ok;
        });

        Register(abi::Op::CommandRegister, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            PluginCommand cmd;
            cmd.plugin = ctx.plugin;
            cmd.name = r.str();
            cmd.usage = r.str();
            cmd.description = r.str();
            if (!r.Ok() || cmd.name.empty() || cmd.name.size() > 64 ||
                cmd.name.find_first_of(" \t/") != std::string::npos)
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            for (auto& c : mod->commands)
                if (c.name == cmd.name) {
                    c = cmd;
                    commandVersion++;
                    return abi::Status::Ok;
                }
            if (mod->commands.size() >= 128)
                return abi::Status::Failed;
            mod->commands.push_back(std::move(cmd));
            commandVersion++;
            return abi::Status::Ok;
        });

        Register(abi::Op::CommandUnregister, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            std::erase_if(mod->commands, [&](const PluginCommand& c) { return c.name == name; });
            commandVersion++;
            return abi::Status::Ok;
        });

        Register(abi::Op::KeyRegister, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            PluginKey key;
            key.plugin = ctx.plugin;
            key.id = r.str();
            key.label = r.str();
            const std::string fallback = r.str();
            if (!r.Ok() || key.id.empty() || key.id.size() > 64)
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            const auto saved = mod->savedKeys.find(key.id);
            key.binding = saved != mod->savedKeys.end() ? saved->second : fallback;
            for (auto& k : mod->keys)
                if (k.id == key.id) {
                    k.label = key.label;
                    return abi::Status::Ok;
                }
            if (mod->keys.size() >= 64)
                return abi::Status::Failed;
            mod->keys.push_back(std::move(key));
            return abi::Status::Ok;
        });

        Register(abi::Op::KeyRegisterEx, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::KeyBindEx in = r.read<abi::KeyBindEx>();
            if (!r.Ok() || in.id.empty() || in.id.size() > 64)
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            PluginKey key{ctx.plugin, in.id, in.label, in.key, in.ctrl, in.shift, in.alt, in.context};
            const auto saved = mod->savedKeys.find(key.id);
            if (saved != mod->savedKeys.end())
                key.binding = saved->second;
            for (auto& k : mod->keys)
                if (k.id == key.id) {
                    k.label = key.label;
                    k.ctrl = key.ctrl;
                    k.shift = key.shift;
                    k.alt = key.alt;
                    k.context = key.context;
                    return abi::Status::Ok;
                }
            if (mod->keys.size() >= 64)
                return abi::Status::Failed;
            mod->keys.push_back(std::move(key));
            return abi::Status::Ok;
        });

        Register(abi::Op::ModSend, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::ModMessageData in = r.read<abi::ModMessageData>();
            if (!r.Ok() || in.channel.empty() || in.channel.size() > 128 || in.body.size() > (1u << 20))
                return abi::Status::BadArguments;
            abi::EventModMessage e{ctx.plugin, in.channel, in.body};
            PostMain([this, e = std::move(e), to = in.to] {
                abi::Writer w;
                w.write(e);
                if (!to.empty()) {
                    DispatchTo(to, abi::Event::ModMessage, w);
                    return;
                }
                std::vector<std::string> names;
                {
                    std::lock_guard lock(mutex);
                    for (const auto& mod : mods)
                        if (mod->loaded && mod->name != e.from &&
                            (mod->subs.load() & (1ull << static_cast<uint32_t>(abi::Event::ModMessage))))
                            names.push_back(mod->name);
                }
                for (const std::string& name : names)
                    DispatchTo(name, abi::Event::ModMessage, w);
            });
            return abi::Status::Ok;
        });

        Register(abi::Op::UiSubmit, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            const int32_t z = r.get<int32_t>();
            std::vector<uint8_t> commands = r.bytes();
            if (!r.Ok() || name.empty() || name.size() > 64 || commands.size() > (512u << 10))
                return abi::Status::BadArguments;
            {
                std::lock_guard lock(mutex);
                auto* mod = static_cast<Plugin*>(ctx.user);
                if (mod->layers.size() >= 16 && !mod->layers.contains(name))
                    return abi::Status::Failed;
                UiLayer& layer = mod->layers[name];
                layer.plugin = ctx.plugin;
                layer.name = name;
                layer.z = z;
                layer.commands = std::move(commands);
                layer.serial = ++mod->serial;
            }
            layerVersion++;
            return abi::Status::Ok;
        });

        Register(abi::Op::UiClear, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            {
                std::lock_guard lock(mutex);
                static_cast<Plugin*>(ctx.user)->layers.erase(name);
            }
            layerVersion++;
            return abi::Status::Ok;
        });

        RegisterGfxOps();
        RegisterSettingOps();
    }

    namespace {
        bool ValidGfx(const std::vector<uint8_t>& blob) {
            if (blob.size() < sizeof(abi::GfxHeader) || blob.size() > abi::kGfxMaxBytes)
                return false;
            abi::GfxHeader h;
            std::memcpy(&h, blob.data(), sizeof(h));
            const bool wide = (h.flags & abi::kGfxIndex32) != 0;
            if (h.vertexCount > abi::kGfxMaxVertices || h.indexCount > 4u * abi::kGfxMaxVertices ||
                h.commandCount > 65536)
                return false;
            const uint64_t need = sizeof(h) + static_cast<uint64_t>(h.vertexCount) * sizeof(abi::GfxVertex) +
                                  static_cast<uint64_t>(h.indexCount) * (wide ? 4 : 2) +
                                  static_cast<uint64_t>(h.commandCount) * sizeof(abi::GfxCmd);
            if (need != blob.size())
                return false;
            const uint8_t* idx =
                blob.data() + sizeof(h) + static_cast<size_t>(h.vertexCount) * sizeof(abi::GfxVertex);
            const uint8_t* cmds = idx + static_cast<size_t>(h.indexCount) * (wide ? 4 : 2);
            for (uint32_t i = 0; i < h.commandCount; i++) {
                abi::GfxCmd c;
                std::memcpy(&c, cmds + static_cast<size_t>(i) * sizeof(c), sizeof(c));
                if (c.indexCount % 3 != 0 ||
                    static_cast<uint64_t>(c.indexOffset) + c.indexCount > h.indexCount ||
                    static_cast<uint64_t>(c.vertexOffset) > h.vertexCount)
                    return false;
                for (float v : c.clip)
                    if (!std::isfinite(v))
                        return false;
                for (uint32_t k = 0; k < c.indexCount; k++) {
                    uint32_t at = 0;
                    if (wide) {
                        std::memcpy(&at, idx + (static_cast<size_t>(c.indexOffset) + k) * 4, 4);
                    } else {
                        uint16_t s = 0;
                        std::memcpy(&s, idx + (static_cast<size_t>(c.indexOffset) + k) * 2, 2);
                        at = s;
                    }
                    if (static_cast<uint64_t>(at) + c.vertexOffset >= h.vertexCount)
                        return false;
                }
            }
            return true;
        }
    }

    void ModManager::RegisterGfxOps() {
        Register(abi::Op::GfxImageLoad, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const std::string path = r.str();
            if (!r.Ok() || path.empty() || path.contains(".."))
                return abi::Status::BadArguments;
            if (!imageLoader)
                return abi::Status::Unavailable;
            int width = 0, height = 0;
            std::vector<uint8_t> rgba;
            if (!imageLoader(path, width, height, rgba) || width <= 0 || height <= 0 ||
                width > static_cast<int32_t>(abi::kGfxMaxTextureSide) ||
                height > static_cast<int32_t>(abi::kGfxMaxTextureSide) ||
                rgba.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
                return abi::Status::NotFound;
            w.put<int32_t>(width).put<int32_t>(height).bytes(rgba.data(), rgba.size());
            return abi::Status::Ok;
        });

        Register(abi::Op::GfxTextureCreate, [this](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            const int32_t width = r.get<int32_t>(), height = r.get<int32_t>();
            const bool linear = r.boolean();
            std::vector<uint8_t> rgba = r.bytes();
            if (!r.Ok() || width <= 0 || height <= 0 ||
                width > static_cast<int32_t>(abi::kGfxMaxTextureSide) ||
                height > static_cast<int32_t>(abi::kGfxMaxTextureSide) ||
                rgba.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            if (mod->textures.size() >= abi::kGfxMaxTextures ||
                mod->textureBytes + rgba.size() > abi::kGfxMaxTextureBytes)
                return abi::Status::Failed;
            const uint32_t id = mod->nextTexture++;
            Plugin::Tex tex;
            tex.w = width;
            tex.h = height;
            tex.linear = linear;
            tex.version = 1;
            mod->textureBytes += rgba.size();
            tex.rgba = std::make_shared<const std::vector<uint8_t>>(std::move(rgba));
            mod->textures[id] = std::move(tex);
            w.put<uint32_t>(id);
            return abi::Status::Ok;
        });

        Register(abi::Op::GfxTextureUpdate, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const uint32_t id = r.get<uint32_t>();
            const int32_t x = r.get<int32_t>(), y = r.get<int32_t>(), width = r.get<int32_t>(),
                          height = r.get<int32_t>();
            std::vector<uint8_t> rgba = r.bytes();
            if (!r.Ok() || width <= 0 || height <= 0 || x < 0 || y < 0 ||
                rgba.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            const auto it = mod->textures.find(id);
            if (it == mod->textures.end())
                return abi::Status::NotFound;
            Plugin::Tex& tex = it->second;
            if (static_cast<int64_t>(x) + width > tex.w || static_cast<int64_t>(y) + height > tex.h)
                return abi::Status::BadArguments;
            std::vector<uint8_t>* target = nullptr;
            if (tex.rgba && tex.rgba.use_count() == 1) {
                target = const_cast<std::vector<uint8_t>*>(tex.rgba.get());
            } else {
                auto copy = std::make_shared<std::vector<uint8_t>>(*tex.rgba);
                target = copy.get();
                tex.rgba = std::move(copy);
            }
            for (int32_t row = 0; row < height; row++)
                std::memcpy(target->data() + (static_cast<size_t>(y + row) * static_cast<size_t>(tex.w) +
                                              static_cast<size_t>(x)) *
                                                 4,
                            rgba.data() + static_cast<size_t>(row) * static_cast<size_t>(width) * 4,
                            static_cast<size_t>(width) * 4);
            if (tex.rx1 <= tex.rx0 || tex.ry1 <= tex.ry0) {
                tex.rx0 = x;
                tex.ry0 = y;
                tex.rx1 = x + width;
                tex.ry1 = y + height;
            } else {
                tex.rx0 = std::min(tex.rx0, x);
                tex.ry0 = std::min(tex.ry0, y);
                tex.rx1 = std::max(tex.rx1, x + width);
                tex.ry1 = std::max(tex.ry1, y + height);
            }
            tex.version++;
            return abi::Status::Ok;
        });

        Register(abi::Op::GfxTextureDestroy, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const uint32_t id = r.get<uint32_t>();
            if (!r.Ok())
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            const auto it = mod->textures.find(id);
            if (it == mod->textures.end())
                return abi::Status::NotFound;
            mod->textureBytes -=
                std::min<uint64_t>(mod->textureBytes, it->second.rgba ? it->second.rgba->size() : 0);
            mod->textures.erase(it);
            return abi::Status::Ok;
        });

        Register(abi::Op::GfxSubmit, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            const int32_t z = r.get<int32_t>();
            std::vector<uint8_t> blob = r.bytes();
            if (!r.Ok() || name.empty() || name.size() > 64 || !ValidGfx(blob))
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            if (mod->gfxLayers.size() >= abi::kGfxMaxLayers && !mod->gfxLayers.contains(name))
                return abi::Status::Failed;
            GfxLayer& layer = mod->gfxLayers[name];
            layer.plugin = ctx.plugin;
            layer.name = name;
            layer.z = z;
            layer.data = std::make_shared<const std::vector<uint8_t>>(std::move(blob));
            layer.serial = ++mod->serial;
            return abi::Status::Ok;
        });

        Register(abi::Op::GfxClear, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            if (!r.Ok())
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            static_cast<Plugin*>(ctx.user)->gfxLayers.erase(name);
            return abi::Status::Ok;
        });

        Register(abi::Op::InputGrab, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const bool mouse = r.boolean(), keyboard = r.boolean();
            if (!r.Ok())
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            static_cast<Plugin*>(ctx.user)->grab = static_cast<uint8_t>((mouse ? 1 : 0) | (keyboard ? 2 : 0));
            RefreshGrabs();
            return abi::Status::Ok;
        });
    }
}
