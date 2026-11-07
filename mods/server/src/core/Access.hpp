#pragma once
#include <string>
#include <vector>

namespace perlin::server {

    // one person on a list: matched by xuid when the entry has one and the player has one, otherwise by name (case-insensitive)
    struct AccessEntry {
        std::string name;
        std::string xuid;
        // allowlist.json: counts toward max-players or not
        bool ignoresPlayerLimit = false;
        // permissions.json: operator, member or visitor
        std::string permission;
    };

    // the two files bds reads next to its executable
    //   allowlist.json:   [{"ignoresPlayerLimit": false, "name": "Steve", "xuid": "2535..."}]
    //   permissions.json: [{"permission": "operator", "xuid": "2535..."}]
    // a bare string entry is a name. permissions.json entries may carry a "name" too, for offline servers where nobody has an xuid
    class AccessList {
    public:
        enum class Kind { Allowlist, Permissions };

        explicit AccessList(Kind kind = Kind::Allowlist) : kind(kind) {}
        // an empty path keeps the list in memory only
        void Load(const std::string& path);
        bool Save() const;

        bool Contains(const std::string& name, const std::string& xuid) const;
        const AccessEntry* Find(const std::string& name, const std::string& xuid) const;
        // adds or fills in the missing half of an existing entry; false when it was already there as is
        bool Add(const std::string& name, const std::string& xuid);
        // permissions.json: sets the level of an entry, adding it when missing; false when it already had that level
        bool SetPermission(const std::string& name, const std::string& xuid, const std::string& permission);
        // by name or xuid; true when something was removed
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
