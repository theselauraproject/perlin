#pragma once
#include <string>
#include <vector>

namespace perlin::server {

    struct AccessEntry {
        std::string name;
        std::string xuid;

        bool ignoresPlayerLimit = false;

        std::string permission;
    };

    class AccessList {
    public:
        enum class Kind { Allowlist, Permissions };

        explicit AccessList(Kind kind = Kind::Allowlist) : kind(kind) {}

        void Load(const std::string& path);
        bool Save() const;

        bool Contains(const std::string& name, const std::string& xuid) const;
        const AccessEntry* Find(const std::string& name, const std::string& xuid) const;

        bool Add(const std::string& name, const std::string& xuid);

        bool SetPermission(const std::string& name, const std::string& xuid, const std::string& permission);

        bool Remove(const std::string& nameOrXuid);
        const std::vector<AccessEntry>& Entries() const { return entries; }
        const std::string& Path() const { return path; }

    private:
        AccessEntry* FindMutable(const std::string& name, const std::string& xuid);
        Kind kind;
        std::string path;
        std::vector<AccessEntry> entries;
    };
}
