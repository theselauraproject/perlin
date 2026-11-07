#pragma once
#include <string>
#include <vector>

namespace perlin {
    struct AccountEntry {
        std::string id;
        bool microsoft = false;
        std::string name;
        std::string authFile;
        bool skinKnown = false;
        std::string skin, cape;
    };

    class AccountList {
    public:
        void Load(const std::string& dataDir, bool onlineLogin, const std::string& playerName);
        void Save() const;

        const std::vector<AccountEntry>& Entries() const { return entries; }
        const AccountEntry* Active() const;
        const std::string& ActiveId() const { return activeId; }
        bool SetActive(const std::string& id);

        const AccountEntry& AddOffline(const std::string& name);
        const AccountEntry& AddMicrosoft(const std::string& gamertag, const std::string& authFile);
        void Rename(const std::string& id, const std::string& name);
        void SetSkin(const std::string& id, const std::string& skin, const std::string& cape);
        void Remove(const std::string& id);

        std::string NewAuthFile() const;
        std::string AuthPath(const AccountEntry& entry) const { return dataDir + "/" + entry.authFile; }

    private:
        std::string NewId(const char* prefix) const;
        std::string dataDir;
        std::vector<AccountEntry> entries;
        std::string activeId;
    };
}
