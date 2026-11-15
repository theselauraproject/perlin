#include "client/services/account/AccountStore.hpp"

#include "util/JsonValue.hpp"
#include "util/FileIo.hpp"

#include <filesystem>
#include <fstream>
#include <random>

namespace perlin::services {
    namespace fs = std::filesystem;

    void AccountStore::Load(const std::string& dir, bool onlineLogin, const std::string& playerName, Forget forget) {
        dataDir = dir;
        forgetAuth = std::move(forget);
        entries.clear();
        activeId.clear();
        std::string accountsText;
        if (file::Read(dataDir + "/accounts.json", accountsText)) {
            json::Value root;
            std::string error;
            if (json::Value::Parse(accountsText, root, error) && root.IsObject()) {
                if (const json::Value* list = root.Find("accounts"); list && list->IsArray())
                    for (const json::Value& a : list->array) {
                        if (!a.IsObject())
                            continue;
                        AccountEntry e;
                        if (const json::Value* v = a.Find("id"))
                            e.id = v->StringOr("");
                        if (const json::Value* v = a.Find("type"))
                            e.microsoft = v->StringOr("") == "microsoft";
                        if (const json::Value* v = a.Find("name"))
                            e.name = v->StringOr("");
                        if (const json::Value* v = a.Find("auth"))
                            e.authFile = v->StringOr("");
                        if (const json::Value* v = a.Find("skin"); v && v->IsString()) {
                            e.skinKnown = true;
                            e.skin = v->StringOr("");
                        }
                        if (const json::Value* v = a.Find("cape"))
                            e.cape = v->StringOr("");
                        if (e.id.empty() || (e.microsoft && e.authFile.empty()) ||
                            (!e.microsoft && e.name.empty()))
                            continue;
                        entries.push_back(std::move(e));
                    }
                if (const json::Value* v = root.Find("active"))
                    activeId = v->StringOr("");
            }
            if (!Active() && !entries.empty())
                activeId = entries.front().id;
            return;
        }
        std::error_code ec;
        if (fs::exists(dataDir + "/auth.json", ec)) {
            entries.push_back({"ms-1", true, std::string(), "auth.json"});
            if (onlineLogin)
                activeId = "ms-1";
        }
        if (!playerName.empty()) {
            entries.push_back({"off-1", false, playerName, std::string()});
            if (activeId.empty())
                activeId = "off-1";
        }
        if (activeId.empty() && !entries.empty())
            activeId = entries.front().id;
        Save();
    }

    void AccountStore::Save() const {
        if (dataDir.empty())
            return;
        json::Value list = json::Value::MakeArray();
        for (const AccountEntry& e : entries) {
            json::Value a = json::Value::MakeObject();
            a.Set("id", json::Value(e.id));
            a.Set("type", json::Value(e.microsoft ? "microsoft" : "offline"));
            a.Set("name", json::Value(e.name));
            if (e.microsoft)
                a.Set("auth", json::Value(e.authFile));
            if (e.skinKnown) {
                a.Set("skin", json::Value(e.skin));
                a.Set("cape", json::Value(e.cape));
            }
            list.Push(std::move(a));
        }
        json::Value root = json::Value::MakeObject();
        root.Set("accounts", std::move(list));
        root.Set("active", json::Value(activeId));
        std::error_code ec;
        fs::create_directories(dataDir, ec);
        const std::string path = dataDir + "/accounts.json";
        {
            std::ofstream out(path + ".tmp", std::ios::binary | std::ios::trunc);
            out << root.Dump(2);
        }
        fs::rename(path + ".tmp", path, ec);
    }

    const AccountEntry* AccountStore::Active() const {
        for (const AccountEntry& e : entries)
            if (e.id == activeId)
                return &e;
        return nullptr;
    }

    bool AccountStore::SetActive(const std::string& id) {
        for (const AccountEntry& e : entries)
            if (e.id == id) {
                activeId = id;
                Save();
                return true;
            }
        return false;
    }

    std::string AccountStore::NewId(const char* prefix) const {
        std::random_device rd;
        for (;;) {
            const std::string id = std::string(prefix) + std::to_string(rd() % 1000000);
            bool used = false;
            for (const AccountEntry& e : entries)
                used = used || e.id == id;
            if (!used)
                return id;
        }
    }

    std::string AccountStore::NewAuthFile() const {
        std::error_code ec;
        fs::create_directories(dataDir + "/accounts", ec);
        std::random_device rd;
        for (;;) {
            const std::string rel = "accounts/auth-" + std::to_string(rd() % 100000000) + ".json";
            if (!fs::exists(dataDir + "/" + rel, ec))
                return rel;
        }
    }

    const AccountEntry& AccountStore::AddOffline(const std::string& name) {
        for (AccountEntry& e : entries)
            if (!e.microsoft && e.name == name) {
                activeId = e.id;
                Save();
                return e;
            }
        entries.push_back({NewId("off-"), false, name, std::string()});
        activeId = entries.back().id;
        Save();
        return entries.back();
    }

    const AccountEntry& AccountStore::AddMicrosoft(const std::string& gamertag, const std::string& authFile) {
        for (AccountEntry& e : entries)
            if (e.microsoft && !gamertag.empty() && e.name == gamertag) {
                if (e.authFile != authFile)
                    ForgetAuth(e.authFile);
                e.authFile = authFile;
                activeId = e.id;
                Save();
                return e;
            }
        entries.push_back({NewId("ms-"), true, gamertag, authFile});
        activeId = entries.back().id;
        Save();
        return entries.back();
    }

    void AccountStore::ForgetAuth(const std::string& authFile) const {
        if (authFile.empty())
            return;
        if (forgetAuth) {
            forgetAuth(dataDir + "/" + authFile);
            return;
        }
        std::error_code ec;
        fs::remove(dataDir + "/" + authFile, ec);
    }

    void AccountStore::Rename(const std::string& id, const std::string& name) {
        for (AccountEntry& e : entries)
            if (e.id == id && e.name != name) {
                e.name = name;
                Save();
            }
    }

    void AccountStore::SetSkin(const std::string& id, const std::string& skin, const std::string& cape) {
        for (AccountEntry& e : entries)
            if (e.id == id && (!e.skinKnown || e.skin != skin || e.cape != cape)) {
                e.skinKnown = true;
                e.skin = skin;
                e.cape = cape;
                Save();
            }
    }

    void AccountStore::Remove(const std::string& id) {
        for (size_t i = 0; i < entries.size(); i++) {
            if (entries[i].id != id)
                continue;
            if (entries[i].microsoft)
                ForgetAuth(entries[i].authFile);
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
        if (!Active())
            activeId = entries.empty() ? std::string() : entries.front().id;
        Save();
    }
}
