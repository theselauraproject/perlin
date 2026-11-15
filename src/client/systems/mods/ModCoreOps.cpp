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
    using support::SafeRelative;
    using support::UnixMs;

    void ModManager::RegisterCore() {
        Register(abi::Op::Log, [](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const int32_t level = r.get<int32_t>();
            const std::string text = r.str();
            if (!r.Ok())
                return abi::Status::BadArguments;
            static const char* const names[] = {"debug", "info", "warn", "error"};
            Log("mod:" + ctx.plugin + ":" + names[std::clamp(level, 0, 3)], "{}", std::string_view(text).substr(0, 2000));
            return abi::Status::Ok;
        });

        Register(abi::Op::Clock, [](CallContext&, abi::Reader&, abi::Writer& w) {
            w.put<int64_t>(UnixMs());
            return abi::Status::Ok;
        });

        Register(abi::Op::ApiInfo, [](CallContext& ctx, abi::Reader&, abi::Writer& w) {
            w.put<uint32_t>(abi::kApiVersion).put<uint32_t>(abi::kApiRevision).put<uint32_t>(abi::kApiOldestRevision);
            w.put<uint32_t>(ctx.apiRevision);
            return abi::Status::Ok;
        });

        Register(abi::Op::Capabilities, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            abi::CapabilityList list;
            for (const auto& entry : ops)
                if (!declaredOnly.contains(entry.first))
                    list.ops.push_back(entry.first);
            std::ranges::sort(list.ops);
            w.write(list);
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
            file::ReadRest(in, bytes);
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
            std::ranges::sort(names);
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
            for (auto& c : mod->reg.commands)
                if (c.name == cmd.name) {
                    c = cmd;
                    commandVersion++;
                    return abi::Status::Ok;
                }
            if (mod->reg.commands.size() >= 128)
                return abi::Status::Failed;
            mod->reg.commands.push_back(std::move(cmd));
            commandVersion++;
            return abi::Status::Ok;
        });

        Register(abi::Op::CommandUnregister, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            std::erase_if(mod->reg.commands, [&](const PluginCommand& c) { return c.name == name; });
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
            for (auto& k : mod->reg.keys)
                if (k.id == key.id) {
                    k.label = key.label;
                    return abi::Status::Ok;
                }
            if (mod->reg.keys.size() >= 64)
                return abi::Status::Failed;
            mod->reg.keys.push_back(std::move(key));
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
            for (auto& k : mod->reg.keys)
                if (k.id == key.id) {
                    k.label = key.label;
                    k.ctrl = key.ctrl;
                    k.shift = key.shift;
                    k.alt = key.alt;
                    k.context = key.context;
                    return abi::Status::Ok;
                }
            if (mod->reg.keys.size() >= 64)
                return abi::Status::Failed;
            mod->reg.keys.push_back(std::move(key));
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
                            mod->subs.Has(abi::Event::ModMessage))
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
                if (mod->reg.layers.size() >= 16 && !mod->reg.layers.contains(name))
                    return abi::Status::Failed;
                UiLayer& layer = mod->reg.layers[name];
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
                static_cast<Plugin*>(ctx.user)->reg.layers.erase(name);
            }
            layerVersion++;
            return abi::Status::Ok;
        });

        RegisterGfxOps();
        RegisterSettingOps();
    }
}
