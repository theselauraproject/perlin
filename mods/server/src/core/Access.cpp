#include "core/Access.hpp"

#include "core/Json.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace perlin::server {
    namespace {}

    void AccessList::Load(const std::string& file) {
        path = file;
        entries.clear();
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return;
        std::stringstream buf;
        buf << in.rdbuf();
        json::Value root;
        if (!json::Parse(buf.str(), root) || !root.IsArray())
            return;
        for (const json::Value& e : root.items) {
            AccessEntry entry;
            if (e.IsString())
                entry.name = e.text;
            else {
                entry.name = e["name"].String();
                entry.xuid = e["xuid"].String();
                entry.ignoresPlayerLimit = e["ignoresPlayerLimit"].Bool(false);
                entry.permission = str::Lower(e["permission"].String());
            }
            if (kind == Kind::Permissions && entry.permission.empty())
                entry.permission = "member";
            if (!entry.name.empty() || !entry.xuid.empty())
                entries.push_back(std::move(entry));
        }
    }

    bool AccessList::Save() const {
        if (path.empty())
            return true;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << "[";
        for (size_t i = 0; i < entries.size(); i++) {
            const AccessEntry& e = entries[i];
            out << (i ? "," : "") << "\n  {";
            if (kind == Kind::Permissions)
                out << "\"permission\": \"" << json::Escape(e.permission.empty() ? "member" : e.permission)
                    << "\"";
            else
                out << "\"ignoresPlayerLimit\": " << (e.ignoresPlayerLimit ? "true" : "false");
            if (!e.name.empty())
                out << ", \"name\": \"" << json::Escape(e.name) << "\"";
            if (!e.xuid.empty())
                out << ", \"xuid\": \"" << json::Escape(e.xuid) << "\"";
            out << "}";
        }
        out << (entries.empty() ? "]\n" : "\n]\n");
        return out.good();
    }

    AccessEntry* AccessList::FindMutable(const std::string& name, const std::string& xuid) {
        const std::string lowered = str::Lower(name);
        for (AccessEntry& e : entries) {
            if (!xuid.empty() && !e.xuid.empty() && e.xuid == xuid)
                return &e;
            if (!name.empty() && !e.name.empty() && str::Lower(e.name) == lowered)
                return &e;
            // an entry that is only an xuid (a name field holding digits works too)
            if (!xuid.empty() && e.name == xuid)
                return &e;
        }
        return nullptr;
    }

    const AccessEntry* AccessList::Find(const std::string& name, const std::string& xuid) const {
        return const_cast<AccessList*>(this)->FindMutable(name, xuid);
    }

    bool AccessList::Contains(const std::string& name, const std::string& xuid) const {
        return Find(name, xuid) != nullptr;
    }

    bool AccessList::SetPermission(const std::string& name, const std::string& xuid,
                                   const std::string& permission) {
        if (AccessEntry* e = FindMutable(name, xuid)) {
            bool changed = e->permission != permission;
            e->permission = permission;
            if (e->xuid.empty() && !xuid.empty()) {
                e->xuid = xuid;
                changed = true;
            }
            if (e->name.empty() && !name.empty()) {
                e->name = name;
                changed = true;
            }
            if (changed)
                Save();
            return changed;
        }
        AccessEntry e;
        e.name = name;
        e.xuid = xuid;
        e.permission = permission;
        entries.push_back(std::move(e));
        Save();
        return true;
    }

    bool AccessList::Add(const std::string& name, const std::string& xuid) {
        const std::string lowered = str::Lower(name);
        for (AccessEntry& e : entries) {
            const bool same =
                (!xuid.empty() && e.xuid == xuid) || (!name.empty() && str::Lower(e.name) == lowered);
            if (!same)
                continue;
            bool changed = false;
            if (e.xuid.empty() && !xuid.empty()) {
                e.xuid = xuid;
                changed = true;
            }
            if (e.name.empty() && !name.empty()) {
                e.name = name;
                changed = true;
            }
            if (changed)
                Save();
            return changed;
        }
        AccessEntry added;
        added.name = name;
        added.xuid = xuid;
        if (kind == Kind::Permissions)
            added.permission = "member";
        entries.push_back(std::move(added));
        Save();
        return true;
    }

    bool AccessList::Remove(const std::string& key) {
        const std::string lowered = str::Lower(key);
        const size_t before = entries.size();
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const AccessEntry& e) {
                                         return str::Lower(e.name) == lowered ||
                                                (!e.xuid.empty() && e.xuid == key);
                                     }),
                      entries.end());
        const bool removed = entries.size() != before;
        if (removed)
            Save();
        return removed;
    }
}
