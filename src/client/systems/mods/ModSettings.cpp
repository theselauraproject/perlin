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
#include "util/JsonText.hpp"

namespace perlin::mods {
    namespace fs = std::filesystem;
    using support::kPermissionNames;
    using support::PermissionName;
    using support::SafeRelative;

    namespace {
    }

    std::string ModManager::KeysFile(const Plugin& mod) const {
        return (fs::path(directory) / (mod.name + ".keys.json")).string();
    }

    void ModManager::LoadKeys(Plugin& mod) {
        mod.savedKeys.clear();
        std::ifstream in(KeysFile(mod), std::ios::binary);
        if (!in)
            return;
        std::string text;
        file::ReadRest(in, text);
        json::Value json;
        std::string error;
        if (!json::Value::Parse(text, json, error) || !json.IsObject())
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
            text += "  \"" + json::Escape(entry.first) + "\": \"" + json::Escape(entry.second) + "\"";
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
            for (auto& key : mod->reg.keys)
                if (key.id == id)
                    key.binding = binding;
            mod->savedKeys[id] = binding;
            SaveKeys(*mod);
        }
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
                if (std::ranges::find(def.choices, in) == def.choices.end())
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

    void ParseManifestSettings(const json::Value& list, std::vector<abi::SettingDef>& out) {
        int anonymous = 0;
        for (const json::Value& item : list.array) {
            if (!item.IsObject() || out.size() >= 160)
                continue;
            const auto text = [&](const char* key) {
                const json::Value* v = item.Find(key);
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
            if (const json::Value* icon = item.Find("icon")) {
                if (icon->IsString()) {
                    def.icon = icon->string;
                } else if (icon->IsObject()) {
                    for (const char* kind : {"item", "texture", "file"})
                        if (const json::Value* v = icon->Find(kind); v && v->IsString())
                            def.icon = std::string(kind) + ":" + v->string;
                }
            }
            def.min = item.Find("min") ? item.Find("min")->NumberOr(0) : 0;
            def.max = item.Find("max") ? item.Find("max")->NumberOr(1) : 1;
            def.step = item.Find("step") ? item.Find("step")->NumberOr(0) : 0;
            if (const json::Value* r = item.Find("restart"))
                def.restart = r->BoolOr(false);
            if (const json::Value* choices = item.Find("choices"); choices && choices->IsArray())
                for (const json::Value& c : choices->array) {
                    if (def.choices.size() >= 64)
                        break;
                    if (c.IsString()) {
                        def.choices.push_back(c.string);
                        def.choiceLabels.push_back("");
                    } else if (c.IsObject()) {
                        const json::Value* value = c.Find("value");
                        const json::Value* label = c.Find("label");
                        if (value && value->IsString()) {
                            def.choices.push_back(value->string);
                            def.choiceLabels.push_back(label ? label->StringOr("") : std::string());
                        }
                    }
                }
            if (def.kind == abi::SettingKind::Choice && def.choices.empty())
                continue;
            std::string fallback;
            if (const json::Value* d = item.Find("default")) {
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
        std::string text;
        file::ReadRest(in, text);
        json::Value json;
        std::string error;
        if (!json::Value::Parse(text, json, error) || !json.IsObject())
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
            text += "  \"" + json::Escape(entry.first) + "\": \"" + json::Escape(entry.second) + "\"";
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
        if (mod->loaded && mod->subs.Has(abi::Event::SettingChanged)) {
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
                for (const std::string& p : info.permissions) {
                    if (p == "storage")
                        e.permissions |= abi::Permission::Storage;
                    for (const PermissionName& known : kPermissionNames)
                        if (p == known.name)
                            e.permissions |= known.bit;
                }
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
}
