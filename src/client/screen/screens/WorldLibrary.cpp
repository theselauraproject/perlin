#include "client/screen/screens/WorldLibrary.hpp"

#include "client/systems/bindings/Keys.hpp"
#include "client/ui/TextEdit.hpp"
#include "util/NbtLE.hpp"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace perlin::screen {
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    namespace {
        constexpr size_t kNameBytes = 32;  // as long as the game lets a world name be
        constexpr size_t kSeedBytes = 32;
        constexpr const char* kIcon = "world_icon.jpeg"; // Bedrock's preview file, beside level.dat

        std::string Lower(std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        std::string ModeName(int gameType) {
            switch (gameType) {
            case 1:
                return "Creative Mode";
            case 2:
                return "Adventure Mode";
            case 3:
            case 6:
                return "Spectator Mode";
            default:
                return "Survival Mode";
            }
        }

        std::string FileUrl(std::string p) {
            std::replace(p.begin(), p.end(), '\\', '/');
            return "file:///" + (!p.empty() && p[0] == '/' ? p.substr(1) : p);
        }

        long long FileSeconds(const fs::path& p) {
            std::error_code ec;
            const auto t = fs::last_write_time(p, ec);
            if (ec)
                return 0;
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::clock_cast<std::chrono::system_clock>(t).time_since_epoch())
                .count();
        }

        bool ZipFolder(const fs::path& folder, const fs::path& zipPath, const std::string& top) {
            mz_zip_archive za{};
            if (!mz_zip_writer_init_file(&za, zipPath.string().c_str(), 0))
                return false;
            bool ok = true;
            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(folder, ec); ok && it != fs::recursive_directory_iterator();
                 it.increment(ec)) {
                if (ec || !it->is_regular_file(ec))
                    continue;
                const std::string inside = top + "/" + fs::relative(it->path(), folder, ec).generic_string();
                ok = mz_zip_writer_add_file(&za, inside.c_str(), it->path().string().c_str(), nullptr, 0,
                                            MZ_DEFAULT_COMPRESSION);
            }
            ok = mz_zip_writer_finalize_archive(&za) && ok;
            mz_zip_writer_end(&za);
            if (!ok)
                fs::remove(zipPath, ec);
            return ok;
        }
    }

    // As the game's short date and time: 11/24/26, 1:42 PM
    std::string WorldLibrary::DateText(long long unixSeconds) {
        if (unixSeconds <= 0)
            return "unknown";
        const std::time_t t = static_cast<std::time_t>(unixSeconds);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        const int hour = tmv.tm_hour % 12 == 0 ? 12 : tmv.tm_hour % 12;
        char buf[64];
        std::snprintf(buf, sizeof buf, "%d/%d/%02d, %d:%02d %s", tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_year % 100, hour,
                      tmv.tm_min, tmv.tm_hour < 12 ? "AM" : "PM");
        return buf;
    }

    // The world name with the characters a folder cannot hold replaced, then " (1)", " (2)"... until no world
    // has that folder yet, as the game names save folders.
    std::string WorldLibrary::FolderFor(const std::string& worldsDir, const std::string& name) {
        std::string base;
        for (const char c : name) {
            const unsigned char u = static_cast<unsigned char>(c);
            base += u < 32 || std::string_view("/\\:*?\"<>|").contains(c) ? '_' : c;
        }
        while (!base.empty() && (base.back() == ' ' || base.back() == '.'))
            base.pop_back();
        while (!base.empty() && base.front() == ' ')
            base.erase(base.begin());
        if (base.empty())
            base = "World";
        static const char* const kReserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4",
                                                "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
                                                "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
        for (const char* r : kReserved)
            if (Lower(base) == r)
                base = "_" + base + "_";
        std::error_code ec;
        std::string folder = base;
        for (int n = 1; fs::exists(fs::path(worldsDir) / folder, ec); n++)
            folder = base + " (" + std::to_string(n) + ")";
        return folder;
    }

    void WorldLibrary::Open() {
        ui::TitleModel& t = deps.title;
        t.worldMessage.clear();
        t.worldSearch.clear();
        t.worldSearchFocus = true;
        t.worldSelected = -1;
        t.worldScroll = 0;
        Refresh();
        if (all.empty()) {
            OpenCreate(nullptr);
            return;
        }
        t.page = ui::TitlePage::Worlds;
    }

    void WorldLibrary::Refresh() {
        lastScan = Clock::now();
        std::vector<ui::WorldEntry> list;
        std::error_code ec;
        const fs::path root = deps.worldsDir();
        fs::create_directories(root, ec);
        for (const auto& dir : fs::directory_iterator(root, ec)) {
            if (!dir.is_directory(ec))
                continue;
            const fs::path levelDat = dir.path() / "level.dat";
            if (!fs::exists(levelDat, ec))
                continue;
            ui::WorldEntry w;
            w.folder = dir.path().filename().string();
            nbtle::Tag tag;
            uint32_t storage = 0;
            std::string version;
            if (nbtle::ReadLevelDat(levelDat.string(), tag, storage)) {
                w.name = tag.Str("LevelName");
                w.lastPlayed = tag.Int("LastPlayed", 0);
                w.gameType = static_cast<int>(tag.Int("GameType", 0));
                w.difficulty = static_cast<int>(tag.Int("Difficulty", 2));
                w.generator = static_cast<int>(tag.Int("Generator", 1));
                w.seed = tag.Int("RandomSeed", 0);
                w.cheats = tag.Int("commandsEnabled", 0) != 0;
                w.structures = tag.Int("GenerateStructures", 1) != 0;
                if (const nbtle::Tag* v = tag.Find("lastOpenedWithVersion"); v && v->type == nbtle::List) {
                    // trailing zeros of the five-part version are not shown: 1.21.60
                    size_t parts = v->items.size();
                    while (parts > 3 && v->items[parts - 1].integer == 0)
                        parts--;
                    for (size_t i = 0; i < parts; i++)
                        version += (i ? "." : "") + std::to_string(v->items[i].integer);
                }
            }
            if (w.name.empty()) {
                std::ifstream in(dir.path() / "levelname.txt");
                std::getline(in, w.name);
            }
            if (w.name.empty())
                w.name = w.folder;
            if (w.lastPlayed <= 0)
                w.lastPlayed = FileSeconds(levelDat);
            w.line2 = w.folder + " (" + DateText(w.lastPlayed) + ")";
            w.line3 = ModeName(w.gameType) + (w.cheats ? ", Cheats" : "") +
                      ", Version: " + (version.empty() ? std::string("unknown") : version);
            const fs::path icon = dir.path() / kIcon;
            if (fs::exists(icon, ec))
                w.icon = "mcfile:" + icon.string() + "#square" + std::to_string(FileSeconds(icon)) + "_" +
                         std::to_string(fs::file_size(icon, ec));
            list.push_back(std::move(w));
        }
        std::ranges::stable_sort(list, [](const ui::WorldEntry& a, const ui::WorldEntry& b) {
            return a.lastPlayed != b.lastPlayed ? a.lastPlayed > b.lastPlayed : a.folder < b.folder;
        });
        all = std::move(list);
        ApplySearch();
    }

    void WorldLibrary::Poll() {
        if (deps.title.page == ui::TitlePage::Worlds && Clock::now() - lastScan > std::chrono::seconds(2))
            Refresh();
    }

    void WorldLibrary::ApplySearch() {
        ui::TitleModel& t = deps.title;
        std::string selected;
        if (t.worldSelected >= 0 && t.worldSelected < static_cast<int>(t.worlds.size()))
            selected = t.worlds[static_cast<size_t>(t.worldSelected)].folder;
        const std::string query = Lower(t.worldSearch);
        t.worlds.clear();
        for (const ui::WorldEntry& w : all)
            if (query.empty() || Lower(w.name).contains(query) || Lower(w.folder).contains(query))
                t.worlds.push_back(w);
        t.worldTotal = static_cast<int>(all.size());
        t.worldSelected = -1;
        for (size_t i = 0; i < t.worlds.size(); i++)
            if (t.worlds[i].folder == selected)
                t.worldSelected = static_cast<int>(i);
        t.worldScroll = std::clamp(t.worldScroll, 0, std::max(0, static_cast<int>(t.worlds.size()) - 1));
        deps.invalidate();
    }

    const ui::WorldEntry* WorldLibrary::Selected() const {
        const ui::TitleModel& t = deps.title;
        if (t.worldSelected < 0 || t.worldSelected >= static_cast<int>(t.worlds.size()))
            return nullptr;
        return &t.worlds[static_cast<size_t>(t.worldSelected)];
    }

    void WorldLibrary::OpenCreate(const ui::WorldEntry* from) {
        ui::TitleModel& t = deps.title;
        t.page = ui::TitlePage::CreateWorld;
        t.createTab = 0;
        t.createFocus = 0;
        t.createName = from ? from->name : "New World";
        t.createSeed = from ? std::to_string(from->seed) : std::string();
        t.createMode = from && (from->gameType == 1 || from->gameType == 2) ? from->gameType : 0;
        t.createDifficulty = from ? std::clamp(from->difficulty, 0, 3) : 2;
        t.createCheats = from ? from->cheats : false;
        t.createType = from && from->generator == 2 ? 1 : 0;
        t.createStructures = from ? from->structures : true;
        t.createBonus = false;
        t.createFolder = FolderFor(deps.worldsDir(), t.createName);
        deps.invalidate();
    }

    void WorldLibrary::Create() {
        ui::TitleModel& t = deps.title;
        WorldLaunch w;
        w.name = t.createName.empty() ? std::string("New World") : t.createName;
        w.folder = FolderFor(deps.worldsDir(), w.name);
        w.fresh = true;
        w.seedText = t.createSeed;
        w.gameMode = t.createMode;
        w.difficulty = t.createDifficulty;
        w.cheats = t.createCheats;
        w.flat = t.createType == 1;
        w.structures = t.createStructures;
        w.bonusChest = t.createBonus;
        deps.play(w);
    }

    void WorldLibrary::Play(int row) {
        ui::TitleModel& t = deps.title;
        if (row < 0 || row >= static_cast<int>(t.worlds.size()))
            return;
        const ui::WorldEntry& e = t.worlds[static_cast<size_t>(row)];
        WorldLaunch w;
        w.folder = e.folder;
        w.name = e.name;
        deps.play(w);
    }

    void WorldLibrary::Backup(const ui::WorldEntry& e) {
        std::error_code ec;
        const fs::path dir = deps.backupsDir();
        fs::create_directories(dir, ec);
        const std::time_t now = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &now);
#else
        localtime_r(&now, &tmv);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &tmv);
        const fs::path zip = dir / (std::string(stamp) + "_" + e.folder + ".zip");
        if (ZipFolder(fs::path(deps.worldsDir()) / e.folder, zip, e.folder)) {
            const double mb = static_cast<double>(fs::file_size(zip, ec)) / (1024.0 * 1024.0);
            char size[64];
            std::snprintf(size, sizeof size, "Size: %.0f MB", std::max(1.0, mb));
            deps.title.editWorldMessage = "Backed up: " + zip.filename().string() + "  " + size;
        } else {
            deps.title.editWorldMessage = "Backup failed";
        }
    }

    void WorldLibrary::SaveEdit(const ui::WorldEntry& e) {
        std::string name = deps.title.editWorldName;
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        if (name.empty() || name == e.name)
            return;
        const fs::path dir = fs::path(deps.worldsDir()) / e.folder;
        nbtle::Tag tag;
        uint32_t storage = 0;
        if (nbtle::ReadLevelDat((dir / "level.dat").string(), tag, storage)) {
            tag.SetString("LevelName", name);
            nbtle::WriteLevelDat((dir / "level.dat").string(), tag, storage);
        }
        std::ofstream(dir / "levelname.txt", std::ios::trunc) << name;
    }

    bool WorldLibrary::Typed(const InputManager::Typed& t) {
        ui::TitleModel& m = deps.title;
        const bool special = !t.character;
        switch (m.page) {
        case ui::TitlePage::Worlds: {
            const int count = static_cast<int>(m.worlds.size());
            if (special && t.key == Key::Enter) {
                Play(m.worldSelected);
            } else if (special && t.key == Key::Down && count) {
                m.worldSelected = std::min(count - 1, m.worldSelected + 1);
            } else if (special && t.key == Key::Up && count) {
                m.worldSelected = std::max(0, m.worldSelected - 1);
            } else if (special && t.key == Key::Delete && !m.worldSearchFocus && Selected()) {
                m.page = ui::TitlePage::DeleteWorld;
            } else if (m.worldSearchFocus) {
                const std::string before = m.worldSearch;
                ui::TextEdit::Focused().Apply(t, m.worldSearch, ui::TextLimits{.maxBytes = 64});
                if (m.worldSearch != before)
                    ApplySearch();
            }
            return true;
        }
        case ui::TitlePage::CreateWorld:
            if (special && t.key == Key::Enter) {
                Create();
            } else if (special && t.key == Key::Tab) {
                m.createTab = (m.createTab + 1) % 3;
                m.createFocus = m.createTab == 1 ? 1 : 0;
            } else if (m.createTab == 0 && m.createFocus == 0) {
                ui::TextEdit::Focused().Apply(t, m.createName, ui::TextLimits{.maxBytes = kNameBytes});
                m.createFolder = FolderFor(deps.worldsDir(), m.createName.empty() ? "New World" : m.createName);
            } else if (m.createTab == 1 && m.createFocus == 1) {
                ui::TextEdit::Focused().Apply(t, m.createSeed, ui::TextLimits{.maxBytes = kSeedBytes});
            }
            return true;
        case ui::TitlePage::EditWorld:
            if (special && t.key == Key::Enter)
                Click(ui::kEditWorldSave);
            else
                ui::TextEdit::Focused().Apply(t, m.editWorldName, ui::TextLimits{.maxBytes = kNameBytes});
            return true;
        case ui::TitlePage::DeleteWorld:
            if (special && t.key == Key::Enter)
                Click(ui::kDeleteWorldOk);
            return true;
        default:
            return false;
        }
    }

    bool WorldLibrary::Click(const std::string& hover) {
        ui::TitleModel& m = deps.title;
        const auto now = Clock::now();
        if (hover == ui::kTitleSingle) {
            Open();
            return true;
        }
        if (m.page != ui::TitlePage::Worlds && m.page != ui::TitlePage::CreateWorld &&
            m.page != ui::TitlePage::EditWorld && m.page != ui::TitlePage::DeleteWorld)
            return false;
        const auto rowOf = [&](const char* prefix) {
            const std::string p = prefix;
            return hover.starts_with(p) ? std::atoi(hover.c_str() + p.size()) : -1;
        };

        if (m.page == ui::TitlePage::Worlds) {
            m.worldSearchFocus = hover == ui::kWorldSearch;
            if (const int play = rowOf(ui::kWorldPlayPrefix); play >= 0) {
                m.worldSelected = play;
                Play(play);
            } else if (const int row = rowOf(ui::kWorldRowPrefix); row >= 0) {
                if (row == clickRow && row == m.worldSelected && now - clickAt < std::chrono::milliseconds(400))
                    Play(row);
                m.worldSelected = row;
                clickRow = row;
                clickAt = now;
            } else if (hover == ui::kWorldPlay) {
                Play(m.worldSelected);
            } else if (hover == ui::kWorldCreate) {
                OpenCreate(nullptr);
            } else if (hover == ui::kWorldRecreate) {
                if (const ui::WorldEntry* e = Selected())
                    OpenCreate(e);
            } else if (hover == ui::kWorldEdit) {
                if (const ui::WorldEntry* e = Selected()) {
                    m.editWorldName = e->name;
                    m.editWorldMessage.clear();
                    m.page = ui::TitlePage::EditWorld;
                }
            } else if (hover == ui::kWorldDelete) {
                if (Selected())
                    m.page = ui::TitlePage::DeleteWorld;
            } else if (hover == ui::kWorldBack) {
                m.page = ui::TitlePage::Main;
            }
            deps.invalidate();
            return true;
        }

        if (m.page == ui::TitlePage::CreateWorld) {
            static const int kModes[] = {0, 1, 2};
            if (const int tab = rowOf(ui::kCreateTabPrefix); tab >= 0) {
                m.createTab = std::clamp(tab, 0, 2);
                m.createFocus = m.createTab == 1 ? 1 : 0;
            } else if (hover == ui::kCreateName) {
                m.createFocus = 0;
            } else if (hover == ui::kCreateSeed) {
                m.createFocus = 1;
            } else if (hover == ui::kCreateMode) {
                const auto at = std::ranges::find(kModes, m.createMode);
                m.createMode = kModes[(at == std::end(kModes) ? 0 : (at - std::begin(kModes) + 1)) % 3];
            } else if (hover == ui::kCreateDifficulty) {
                m.createDifficulty = (m.createDifficulty + 1) % 4;
            } else if (hover == ui::kCreateCheats) {
                m.createCheats = !m.createCheats;
            } else if (hover == ui::kCreateType) {
                m.createType = (m.createType + 1) % 2;
            } else if (hover == ui::kCreateStructures) {
                m.createStructures = !m.createStructures;
            } else if (hover == ui::kCreateBonus) {
                m.createBonus = !m.createBonus;
            } else if (hover == ui::kCreateDone) {
                Create();
            } else if (hover == ui::kCreateCancel) {
                Refresh();
                m.page = all.empty() ? ui::TitlePage::Main : ui::TitlePage::Worlds;
            }
            deps.invalidate();
            return true;
        }

        if (m.page == ui::TitlePage::EditWorld) {
            const ui::WorldEntry* e = Selected();
            if (!e) {
                m.page = ui::TitlePage::Worlds;
            } else if (hover == ui::kEditWorldResetIcon) {
                std::error_code ec;
                fs::remove(fs::path(deps.worldsDir()) / e->folder / kIcon, ec);
                Refresh();
            } else if (hover == ui::kEditWorldFolder) {
                deps.openUrl(FileUrl((fs::path(deps.worldsDir()) / e->folder).string()));
            } else if (hover == ui::kEditWorldBackup) {
                Backup(*e);
            } else if (hover == ui::kEditWorldBackupFolder) {
                std::error_code ec;
                fs::create_directories(deps.backupsDir(), ec);
                deps.openUrl(FileUrl(deps.backupsDir()));
            } else if (hover == ui::kEditWorldSave) {
                SaveEdit(*e);
                m.page = ui::TitlePage::Worlds;
                Refresh();
            } else if (hover == ui::kEditWorldCancel) {
                m.page = ui::TitlePage::Worlds;
            }
            deps.invalidate();
            return true;
        }

        if (m.page == ui::TitlePage::DeleteWorld) {
            if (hover == ui::kDeleteWorldOk) {
                if (const ui::WorldEntry* e = Selected()) {
                    std::error_code ec;
                    fs::remove_all(fs::path(deps.worldsDir()) / e->folder, ec);
                    m.worldMessage = ec ? "Failed to delete world" : std::string();
                }
                m.worldSelected = -1;
                Refresh();
                m.page = ui::TitlePage::Worlds;
            } else if (hover == ui::kDeleteWorldCancel) {
                m.page = ui::TitlePage::Worlds;
            }
            deps.invalidate();
            return true;
        }
        return false;
    }

    std::string WorldLibrary::Signature() const {
        const ui::TitleModel& m = deps.title;
        std::ostringstream s;
        s << "|w" << m.worldSelected << '|' << m.worldScroll << '|' << m.worldSearch << (m.worldSearchFocus ? 'f' : 'n')
          << '|' << m.worldMessage << '|' << m.worlds.size();
        for (const ui::WorldEntry& w : m.worlds)
            s << '|' << w.folder << w.name << w.line2 << w.line3 << w.icon;
        s << "|c" << m.createTab << m.createFocus << m.createName << '|' << m.createSeed << '|' << m.createFolder << '|'
          << m.createMode << m.createDifficulty << m.createCheats << m.createType << m.createStructures << m.createBonus;
        s << "|e" << m.editWorldName << '|' << m.editWorldMessage;
        return s.str();
    }
}
