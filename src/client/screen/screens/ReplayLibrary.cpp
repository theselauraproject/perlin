#include "client/screen/screens/ReplayLibrary.hpp"
#include "client/systems/bindings/Keys.hpp"
#include "client/ui/TextEdit.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace perlin::screen {
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    namespace {
        std::string Duration(double ms) {
            return ui::FormatReplayTime(ms);
        }

        std::string SizeText(uintmax_t bytes) {
            char buf[48];
            if (bytes >= (1u << 30))
                std::snprintf(buf, sizeof buf, "%.1f GB", static_cast<double>(bytes) / (1u << 30));
            else if (bytes >= (1u << 20))
                std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1u << 20));
            else
                std::snprintf(buf, sizeof buf, "%.0f KB", static_cast<double>(bytes) / 1024.0);
            return buf;
        }

        std::string DateText(long long unixTime) {
            if (unixTime <= 0)
                return "unknown date";
            const std::time_t t = static_cast<std::time_t>(unixTime);
            std::tm tmv{};
#ifdef _WIN32
            localtime_s(&tmv, &t);
#else
            localtime_r(&t, &tmv);
#endif
            char buf[64];
            std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tmv);
            return buf;
        }
    }

    void ReplayLibrary::Refresh() {
        std::error_code ec;
        fs::create_directories(replay::ReplayDirectory(), ec);
        if (!recoveryChecked) {
            recoveryChecked = true;
            std::string recovered;
            if (replay::ReplayController::RecoverInterrupted(recovered))
                deps.title.replayMessage = "Recovered an interrupted recording";
        }
        std::vector<ui::ReplayEntry> list;
        for (const auto& e : fs::directory_iterator(replay::ReplayDirectory(), ec)) {
            if (!e.is_regular_file(ec) || e.path().extension() != replay::kExtension)
                continue;
            const std::string key = e.path().string();
            CachedMeta& cm = metaCache[key];
            const auto mtime = e.last_write_time(ec);
            const uintmax_t size = e.file_size(ec);
            if (cm.mtime != mtime || cm.size != size || (!cm.ok && cm.error.empty())) {
                cm = CachedMeta{};
                cm.mtime = mtime;
                cm.size = size;
                cm.ok = replay::Reader::ReadMetadata(key, cm.meta, nullptr, cm.error);
            }
            ui::ReplayEntry r;
            r.file = key;
            r.sizeBytes = static_cast<long long>(size);
            if (cm.ok) {
                r.name = cm.meta.name.empty() ? e.path().stem().string() : cm.meta.name;
                r.created = cm.meta.createdUnix;
                r.durationMs = cm.meta.durationMs;
                r.line1 = Duration(cm.meta.durationMs) + "  -  " + DateText(cm.meta.createdUnix) + "  -  " +
                          SizeText(size);
                r.line2 = (cm.meta.server.empty() ? std::string("unknown server") : cm.meta.server) +
                          "  -  " + cm.meta.playerName +
                          (cm.meta.gameVersion.empty() ? std::string() : "  -  " + cm.meta.gameVersion);
            } else {
                r.name = e.path().stem().string();
                r.broken = true;
                r.line1 = "Can't open: " + cm.error;
                r.line2 = e.path().filename().string();
            }
            list.push_back(std::move(r));
        }
        const int sort = deps.title.replaySort;
        const bool desc = deps.title.replaySortDesc;
        const auto key = [&](const ui::ReplayEntry& a, const ui::ReplayEntry& b) {
            const auto cmp = [](auto x, auto y) { return x < y ? -1 : (y < x ? 1 : 0); };
            switch (sort) {
            case 1:
                return cmp(a.name, b.name);
            case 2:
                return cmp(a.line2, b.line2);
            case 3:
                return cmp(a.durationMs, b.durationMs);
            case 4:
                return cmp(a.sizeBytes, b.sizeBytes);
            default:
                return cmp(a.created, b.created);
            }
        };
        std::ranges::stable_sort(list, [&](const ui::ReplayEntry& a, const ui::ReplayEntry& b) {
            const int c = key(a, b);
            return desc ? c > 0 : c < 0;
        });
        std::string selected;
        if (deps.title.replaySelected >= 0 && deps.title.replaySelected < static_cast<int>(deps.title.replays.size()))
            selected = deps.title.replays[static_cast<size_t>(deps.title.replaySelected)].file;
        deps.title.replays = std::move(list);
        deps.title.replaySelected = -1;
        for (size_t i = 0; i < deps.title.replays.size(); i++)
            if (deps.title.replays[i].file == selected)
                deps.title.replaySelected = static_cast<int>(i);
        deps.invalidate();
    }

    bool ReplayLibrary::Typed(const InputManager::Typed& t) {
        const int key = t.key;
        const bool character = t.character;
        if (deps.title.page == ui::TitlePage::ReplayRename) {
            if (!character && key == Key::Enter)
                Click(ui::kReplayRenameOk);
            else
                ui::TextEdit::Focused().Apply(t, deps.title.replayName, ui::TextLimits{.maxBytes = 60});
            return true;
        }
        if (deps.title.page == ui::TitlePage::Replays) {
            const int count = static_cast<int>(deps.title.replays.size());
            if (!character && key == Key::Enter) {
                Click(ui::kReplayPlay);
            } else if (!character && key == Key::Down && count) {
                deps.title.replaySelected = std::min(count - 1, deps.title.replaySelected + 1);
            } else if (!character && key == Key::Up && count) {
                deps.title.replaySelected = std::max(0, deps.title.replaySelected - 1);
            } else if (!character && key == Key::Delete && deps.title.replaySelected >= 0) {
                deps.title.page = ui::TitlePage::ReplayDelete;
            }
            return true;
        }
        return deps.title.page == ui::TitlePage::ReplayDelete;
    }

    bool ReplayLibrary::Click(const std::string& hover) {
        const auto now = Clock::now();
        const auto selectedEntry = [&]() -> const ui::ReplayEntry* {
            if (deps.title.replaySelected < 0 || deps.title.replaySelected >= static_cast<int>(deps.title.replays.size()))
                return nullptr;
            return &deps.title.replays[static_cast<size_t>(deps.title.replaySelected)];
        };
        if (hover == ui::kTitleReplays) {
            deps.title.page = ui::TitlePage::Replays;
            deps.title.replayMessage.clear();
            deps.title.replaySelected = -1;
            deps.title.replayScroll = 0;
            Refresh();
            return true;
        }
        if (deps.title.page != ui::TitlePage::Replays && deps.title.page != ui::TitlePage::ReplayRename &&
            deps.title.page != ui::TitlePage::ReplayDelete)
            return false;
        const std::string rowPrefix = ui::kReplayRowPrefix;
        if (hover.starts_with(rowPrefix)) {
            const int row = std::atoi(hover.c_str() + rowPrefix.size());
            deps.title.replaySelected = row;
            if (row == clickRow && now - clickAt < std::chrono::milliseconds(400))
                Click(ui::kReplayPlay);
            clickRow = row;
            clickAt = now;
            return true;
        }
        if (hover == ui::kReplayPlay) {
            if (const ui::ReplayEntry* e = selectedEntry(); e && !e->broken)
                deps.play(e->file);
            return true;
        }
        if (hover == ui::kReplayDone) {
            deps.title.page = ui::TitlePage::Main;
            return true;
        }
        if (hover == ui::kReplayFolder) {
            std::error_code ec;
            fs::create_directories(replay::ReplayDirectory(), ec);
            std::string p = replay::ReplayDirectory();
            std::replace(p.begin(), p.end(), '\\', '/');
            deps.openUrl("file:///" + (!p.empty() && p[0] == '/' ? p.substr(1) : p));
            return true;
        }
        if (hover == ui::kReplaySort) {
            deps.title.replaySort = (deps.title.replaySort + 1) % 5;
            Refresh();
            return true;
        }
        if (hover == ui::kReplayDirection) {
            deps.title.replaySortDesc = !deps.title.replaySortDesc;
            Refresh();
            return true;
        }
        if (hover == ui::kReplayRename) {
            if (const ui::ReplayEntry* e = selectedEntry()) {
                deps.title.replayName = e->name;
                deps.title.page = ui::TitlePage::ReplayRename;
            }
            return true;
        }
        if (hover == ui::kReplayDelete) {
            if (selectedEntry())
                deps.title.page = ui::TitlePage::ReplayDelete;
            return true;
        }
        if (hover == ui::kReplayDialogCancel) {
            deps.title.page = ui::TitlePage::Replays;
            return true;
        }
        if (hover == ui::kReplayRenameOk) {
            if (const ui::ReplayEntry* e = selectedEntry()) {
                replay::Metadata meta;
                std::string error;
                const std::string file = e->file;
                if (replay::Reader::ReadMetadata(file, meta, nullptr, error)) {
                    meta.name = deps.title.replayName.empty() ? meta.name : deps.title.replayName;
                    if (replay::UpdateArchiveFile(file, "metadata.json", meta.ToJson().Dump(2), error))
                        deps.title.replayMessage = "Saved";
                    else
                        deps.title.replayMessage = "Could not save: " + error;
                } else {
                    deps.title.replayMessage = "Could not read the replay: " + error;
                }
                metaCache.erase(file);
            }
            deps.title.page = ui::TitlePage::Replays;
            Refresh();
            return true;
        }
        if (hover == ui::kReplayDeleteOk) {
            if (const ui::ReplayEntry* e = selectedEntry()) {
                std::error_code ec;
                fs::remove(e->file, ec);
                metaCache.erase(e->file);
                deps.title.replayMessage = ec ? "Could not delete: " + ec.message() : "Deleted";
            }
            deps.title.replaySelected = -1;
            deps.title.page = ui::TitlePage::Replays;
            Refresh();
            return true;
        }
        return false;
    }

    bool ReplayLibrary::Import(const std::string& source) {
        std::error_code ec;
        replay::Metadata meta;
        std::string error;
        if (!replay::Reader::ReadMetadata(source, meta, nullptr, error)) {
            deps.title.replayMessage = "Not a valid replay: " + error;
            deps.announce("\xC2\xA7"
                          "cNot a valid replay: " +
                          error);
            return false;
        }
        fs::create_directories(replay::ReplayDirectory(), ec);
        fs::path dest =
            fs::path(replay::ReplayDirectory()) / replay::SafeFileName(fs::path(source).stem().string());
        dest += replay::kExtension;
        for (int n = 2; fs::exists(dest, ec); n++) {
            dest = fs::path(replay::ReplayDirectory()) /
                   (replay::SafeFileName(fs::path(source).stem().string()) + "_" + std::to_string(n) +
                    replay::kExtension);
        }
        fs::copy_file(source, dest, fs::copy_options::none, ec);
        if (ec) {
            deps.title.replayMessage = "Could not import: " + ec.message();
            return false;
        }
        deps.title.replayMessage = "Imported " + dest.filename().string();
        deps.announce("\xC2\xA7"
                      "aImported replay " +
                      dest.filename().string());
        if (deps.title.page == ui::TitlePage::Replays)
            Refresh();
        return true;
    }
}
