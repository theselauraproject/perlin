#include "client/Accounts.hpp"

#include "client/ui/Json.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace perlin {
    namespace fs = std::filesystem;

    void AccountList::Load(const std::string& dir, bool onlineLogin, const std::string& playerName) {
        dataDir = dir;
        entries.clear();
        activeId.clear();
        std::ifstream in(dataDir + "/accounts.json", std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            ui::Json root;
            std::string error;
            if (ui::Json::Parse(ss.str(), root, error) && root.IsObject()) {
                if (const ui::Json* list = root.Find("accounts"); list && list->IsArray())
                    for (const ui::Json& a : list->array) {
                        if (!a.IsObject())
                            continue;
                        AccountEntry e;
                        if (const ui::Json* v = a.Find("id"))
                            e.id = v->StringOr("");
                        if (const ui::Json* v = a.Find("type"))
                            e.microsoft = v->StringOr("") == "microsoft";
                        if (const ui::Json* v = a.Find("name"))
                            e.name = v->StringOr("");
                        if (const ui::Json* v = a.Find("auth"))
                            e.authFile = v->StringOr("");
                        if (const ui::Json* v = a.Find("skin"); v && v->IsString()) {
                            e.skinKnown = true;
                            e.skin = v->StringOr("");
                        }
                        if (const ui::Json* v = a.Find("cape"))
                            e.cape = v->StringOr("");
                        if (e.id.empty() || (e.microsoft && e.authFile.empty()) ||
                            (!e.microsoft && e.name.empty()))
                            continue;
                        entries.push_back(std::move(e));
                    }
                if (const ui::Json* v = root.Find("active"))
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

    void AccountList::Save() const {
        if (dataDir.empty())
            return;
        ui::Json list = ui::Json::MakeArray();
        for (const AccountEntry& e : entries) {
            ui::Json a = ui::Json::MakeObject();
            a.Set("id", ui::Json(e.id));
            a.Set("type", ui::Json(e.microsoft ? "microsoft" : "offline"));
            a.Set("name", ui::Json(e.name));
            if (e.microsoft)
                a.Set("auth", ui::Json(e.authFile));
            if (e.skinKnown) {
                a.Set("skin", ui::Json(e.skin));
                a.Set("cape", ui::Json(e.cape));
            }
            list.Push(std::move(a));
        }
        ui::Json root = ui::Json::MakeObject();
        root.Set("accounts", std::move(list));
        root.Set("active", ui::Json(activeId));
        std::error_code ec;
        fs::create_directories(dataDir, ec);
        const std::string path = dataDir + "/accounts.json";
        {
            std::ofstream out(path + ".tmp", std::ios::binary | std::ios::trunc);
            out << root.Dump(2);
        }
        fs::rename(path + ".tmp", path, ec);
    }

    const AccountEntry* AccountList::Active() const {
        for (const AccountEntry& e : entries)
            if (e.id == activeId)
                return &e;
        return nullptr;
    }

    bool AccountList::SetActive(const std::string& id) {
        for (const AccountEntry& e : entries)
            if (e.id == id) {
                activeId = id;
                Save();
                return true;
            }
        return false;
    }

    std::string AccountList::NewId(const char* prefix) const {
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

    std::string AccountList::NewAuthFile() const {
        std::error_code ec;
        fs::create_directories(dataDir + "/accounts", ec);
        std::random_device rd;
        for (;;) {
            const std::string rel = "accounts/auth-" + std::to_string(rd() % 100000000) + ".json";
            if (!fs::exists(dataDir + "/" + rel, ec))
                return rel;
        }
    }

    const AccountEntry& AccountList::AddOffline(const std::string& name) {
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

    const AccountEntry& AccountList::AddMicrosoft(const std::string& gamertag, const std::string& authFile) {
        for (AccountEntry& e : entries)
            if (e.microsoft && !gamertag.empty() && e.name == gamertag) {
                std::error_code ec;
                if (e.authFile != authFile)
                    fs::remove(dataDir + "/" + e.authFile, ec);
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

    void AccountList::Rename(const std::string& id, const std::string& name) {
        for (AccountEntry& e : entries)
            if (e.id == id && e.name != name) {
                e.name = name;
                Save();
            }
    }

    void AccountList::SetSkin(const std::string& id, const std::string& skin, const std::string& cape) {
        for (AccountEntry& e : entries)
            if (e.id == id && (!e.skinKnown || e.skin != skin || e.cape != cape)) {
                e.skinKnown = true;
                e.skin = skin;
                e.cape = cape;
                Save();
            }
    }

    void AccountList::Remove(const std::string& id) {
        for (size_t i = 0; i < entries.size(); i++) {
            if (entries[i].id != id)
                continue;
            if (entries[i].microsoft) {
                std::error_code ec;
                fs::remove(dataDir + "/" + entries[i].authFile, ec);
            }
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
        if (!Active())
            activeId = entries.empty() ? std::string() : entries.front().id;
        Save();
    }
}
