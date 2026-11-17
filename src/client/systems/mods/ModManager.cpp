#include "client/systems/mods/ModInternal.hpp"

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
#include "util/FileIo.hpp"

namespace perlin::mods {
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;
    using support::kPermissionNames;
    using support::PermissionName;
    using support::EmbeddedManifest;
    using support::ReadFile;
    using support::SafeRelative;

    ModManager& ModManager::Instance() {
        static ModManager* instance = new ModManager();
        return *instance;
    }

    ModManager::ModManager() = default;

    ModManager::~ModManager() {
        Stop();
    }

    void ModManager::SetImageLoader(ImageLoader loader, ScopeId scope) {
        std::lock_guard lock(mutex);
        imageLoader = std::move(loader);
        imageLoaderScope = scope;
    }

    void ModManager::SetEnvironment(const std::string& version, const std::string& platformName) {
        std::lock_guard lock(mutex);
        gameVersion = version;
        platform = platformName;
    }

    void ModManager::Register(abi::Op op, OpHandler handler, ScopeId scope) {
        std::unique_lock lock(opsMutex);
        ops[static_cast<uint32_t>(op)] = {std::move(handler), abi::PermissionOf(op), scope};
    }

    ScopeId ModManager::OpenScope() {
        std::lock_guard lock(mutex);
        const ScopeId id = nextScope++;
        liveScopes.insert(id);
        return id;
    }

    void ModManager::CloseScope(ScopeId scope) {
        if (scope == 0)
            return;
        {
            std::unique_lock lock(opsMutex);
            std::erase_if(ops, [scope](const auto& entry) { return entry.second.scope == scope; });
        }
        std::vector<MainTask> dropped;
        ImageLoader droppedLoader;
        {
            std::lock_guard lock(mutex);
            liveScopes.erase(scope);
            std::erase_if(mainQueue, [&](MainTask& t) {
                if (t.scope != scope)
                    return false;
                dropped.push_back(std::move(t));
                return true;
            });
            if (imageLoaderScope == scope) {
                droppedLoader = std::move(imageLoader);
                imageLoader = nullptr;
                imageLoaderScope = 0;
            }
        }
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
        uint32_t mask = 0;
        for (const PermissionName& p : kPermissionNames)
            mask |= static_cast<uint32_t>(p.bit);
        return mask;
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
            info.apiRevision = mod->apiRevision;
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
                const Subscriptions subs = mod->subs.Load();
                for (uint32_t id = 1; id < abi::kEventCount; id++)
                    if (Subscribed(subs, id) && id != static_cast<uint32_t>(abi::Event::HttpResponse))
                        if (const auto n = abi::SnakeName(static_cast<abi::Event>(id)); !n.empty())
                            info.hooks.emplace_back(n);
            }
            out.push_back(std::move(info));
        }
        std::ranges::sort(out, [](const ModInfo& a, const ModInfo& b) { return a.name < b.name; });
        return out;
    }

    std::vector<PluginCommand> ModManager::Commands() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<PluginCommand> out;
        for (const auto& mod : mods)
            if (mod->loaded)
                out.insert(out.end(), mod->reg.commands.begin(), mod->reg.commands.end());
        return out;
    }

    std::vector<PluginKey> ModManager::Keys() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<PluginKey> out;
        for (const auto& mod : mods)
            if (mod->loaded)
                out.insert(out.end(), mod->reg.keys.begin(), mod->reg.keys.end());
        return out;
    }

    std::vector<UiLayer> ModManager::Layers() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<UiLayer> out;
        for (const auto& mod : mods)
            if (mod->loaded)
                for (const auto& entry : mod->reg.layers)
                    out.push_back(entry.second);
        std::ranges::stable_sort(out, [](const UiLayer& a, const UiLayer& b) { return a.z < b.z; });
        return out;
    }

    std::vector<GfxLayer> ModManager::GfxLayers() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<GfxLayer> out;
        for (const auto& mod : mods)
            if (mod->loaded && !mod->revoked)
                for (const auto& entry : mod->reg.gfxLayers)
                    out.push_back(entry.second);
        std::ranges::stable_sort(out, [](const GfxLayer& a, const GfxLayer& b) { return a.z < b.z; });
        return out;
    }

    std::vector<GfxTexture> ModManager::GfxTextures() const {
        std::lock_guard lock(const_cast<std::mutex&>(mutex));
        std::vector<GfxTexture> out;
        for (const auto& mod : mods)
            if (mod->loaded && !mod->revoked)
                for (auto& entry : mod->reg.textures) {
                    Plugin::Registrations::Tex& t = entry.second;
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
            if (mod->loaded && !mod->revoked && !mod->stuck && mod->reg.grab.load())
                out.push_back(mod->name);
        return out;
    }

    void ModManager::RefreshGrabs() {
        bool mouse = false, keyboard = false;
        for (const auto& mod : mods)
            if (mod->loaded && !mod->revoked && !mod->stuck) {
                const uint8_t g = mod->reg.grab.load();
                mouse = mouse || (g & 1);
                keyboard = keyboard || (g & 2);
            }
        grabMouse = mouse;
        grabKeyboard = keyboard;
    }

    void ModManager::ReleaseGrabs() {
        std::lock_guard lock(mutex);
        for (const auto& mod : mods)
            mod->reg.grab = 0;
        RefreshGrabs();
    }

    void ModManager::ClearRegistrations(Plugin& mod) {
        mod.reg.commands.clear();
        mod.reg.keys.clear();
        mod.reg.layers.clear();
        mod.reg.gfxLayers.clear();
        mod.reg.textures.clear();
        mod.reg.textureBytes = 0;
        mod.reg.grab = 0;
        RefreshGrabs();
        mod.settings.clear();
        mod.runtimeIcon.clear();
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
                std::unique_lock lock(mod.job.mutex);
                if (mod.job.finished.wait_for(lock, std::chrono::milliseconds(500), [&] { return mod.job.done; })) {
                    mod.job.kind = Plugin::Job::Kind::Quit;
                    lock.unlock();
                    mod.job.wake.notify_all();
                    mod.thread.join();
                } else {
                    lock.unlock();
                    mod.thread.detach();
                }
            } else {
                {
                    std::lock_guard lock(mod.job.mutex);
                    mod.job.kind = Plugin::Job::Kind::Quit;
                }
                mod.job.wake.notify_all();
                mod.thread.join();
            }
        }
        {
            std::lock_guard lock(mutex);
            ClearRegistrations(mod);
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
                file::ReadRest(in, manifestText);
            else
                manifestText = EmbeddedManifest(mod.path);
        }
        if (!manifestText.empty()) {
            const std::string& text = manifestText;
            json::Value json;
            std::string error;
            if (json::Value::Parse(text, json, error) && json.IsObject()) {
                const auto get = [&](const char* key) {
                    const json::Value* v = json.Find(key);
                    return v ? v->StringOr("") : std::string();
                };
                if (!get("name").empty())
                    meta.displayName = get("name");
                meta.description = get("description");
                meta.version = get("version");
                meta.author = get("author");
                meta.website = get("website");
                if (const json::Value* v = json.Find("priority"))
                    meta.priority = static_cast<int>(std::clamp(v->NumberOr(0), -1000.0, 1000.0));
                if (const json::Value* v = json.Find("frame_rate"))
                    meta.frameRate = std::clamp(v->NumberOr(0), 0.0, 1000.0);
                manifestIcon = get("icon");
                if (const json::Value* list = json.Find("settings"); list && list->IsArray())
                    ParseManifestSettings(*list, manifestSettings);
                if (const json::Value* list = json.Find("permissions"); list && list->IsArray())
                    for (const json::Value& item : list->array) {
                        const std::string p = item.StringOr("");
                        for (const PermissionName& known : kPermissionNames)
                            if (p == known.name)
                                permissions |= static_cast<uint32_t>(known.bit);
                    }
            }
        }
        for (const PermissionName& known : kPermissionNames)
            if (permissions & static_cast<uint32_t>(known.bit))
                meta.permissions.push_back(known.name);
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
        mod.subs.Store(AllEvents());
        Spawn(mod);
        {
            std::lock_guard lock(mod.job.mutex);
            mod.job.done = false;
            mod.job.kind = Plugin::Job::Kind::Load;
        }
        mod.job.wake.notify_all();
        std::unique_lock lock(mod.job.mutex);
        const bool finished =
            mod.job.finished.wait_for(lock, std::chrono::milliseconds(4000), [&] { return mod.job.done; });
        if (!finished) {
            lock.unlock();
            Revoke(mod, "unresponsive: the plugin did not finish loading");
            return;
        }
        if (!mod.job.error.empty()) {
            const std::string message = mod.job.error;
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
        bool all = false;
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
                created->owner = this;
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
            mod->enabled = std::ranges::find(off, name) == off.end() && !mod->needsApproval;
            const bool first = mod->stamp == fs::file_time_type::min();
            const bool force = all || std::ranges::find(forced, name) != forced.end();
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
                            fresh->owner = this;
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
                if (std::ranges::find(present, (*it)->name) == present.end()) {
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
        Subscriptions mask{};
        std::lock_guard lock(mutex);
        for (const auto& mod : mods)
            if (mod->loaded && !mod->stuck) {
                const Subscriptions s = mod->subs.Load();
                for (size_t i = 0; i < mask.size(); i++)
                    mask[i] |= s[i];
            }
        wanted.Store(mask);
    }

    void ModManager::Queue(abi::Event event, abi::Writer payload) {
        if (!Wants(event))
            return;
        PostMain([this, event, payload = std::move(payload)] { Dispatch(event, payload); });
    }

    void ModManager::PostMain(std::function<void()> task, ScopeId scope) {
        std::lock_guard lock(mutex);
        if (scope != 0 && !liveScopes.contains(scope))
            return;
        if (mainQueue.size() < 512)
            mainQueue.push_back({scope, std::move(task)});
    }

    void ModManager::Pump() {
        if (tlsPlugin)
            return;
        std::vector<MainTask> batch;
        {
            std::lock_guard lock(mutex);
            batch.swap(mainQueue);
        }
        for (auto& task : batch) {
            if (task.scope != 0) {
                std::lock_guard lock(mutex);
                if (!liveScopes.contains(task.scope))
                    continue;
            }
            task.run();
        }
        const auto now = Clock::now();
        if (now - lastFlush > std::chrono::milliseconds(500)) {
            lastFlush = now;
            std::lock_guard lock(mutex);
            for (auto& mod : mods)
                SaveSettings(*mod);
        }
    }
}
