#include "client/update/Updater.hpp"

#include "util/Log.hpp"
#include "util/Paths.hpp"
#include "util/Platform.hpp"

#include <SDL3/SDL_misc.h>

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
#include "network/FastDownload.hpp"
#include "network/Http.hpp"
#include "util/ZipFile.hpp"
#endif

#if defined(_WIN32)
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <sys/stat.h>
#include <unistd.h>
#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <jni.h>
#elif !defined(PERLIN_IOS)
// desktop only: phones never start a second copy of the game
#include <spawn.h>
extern char** environ;
#endif
#endif

#ifndef PERLIN_BUILD_COMMIT
#define PERLIN_BUILD_COMMIT ""
#endif
#ifndef PERLIN_UPDATE_REPO
#define PERLIN_UPDATE_REPO ""
#endif
#ifndef PERLIN_UPDATE_ASSET
#define PERLIN_UPDATE_ASSET ""
#endif

namespace fs = std::filesystem;

#if defined(__EMSCRIPTEN__)
// the site itself is the nightly: version.json beside index.html names the commit it was built from
// clang-format off
EM_JS(void, perlin_js_check_version, (), {
    fetch("version.json", {cache: "no-store"})
        .then(function (r) { return r.ok ? r.json() : null; })
        .then(function (v) {
            if (!v || !v.commit) return;
            var size = lengthBytesUTF8(v.commit) + 1;
            var ptr = _malloc(size);
            stringToUTF8(v.commit, ptr, size);
            _perlin_web_version(ptr);
            _free(ptr);
        })
        .catch(function () {});
});
EM_JS(void, perlin_js_reload_fresh, (), {
    var reload = function () { location.reload(); };
    if (!self.caches) { reload(); return; }
    caches.keys()
        .then(function (keys) { return Promise.all(keys.map(function (k) { return caches.delete(k); })); })
        .then(reload, reload);
});
// clang-format on
#endif

namespace perlin::update {
    namespace {
        constexpr std::string_view kCommit = PERLIN_BUILD_COMMIT;
        constexpr std::string_view kRepo = PERLIN_UPDATE_REPO;
        constexpr std::string_view kAsset = PERLIN_UPDATE_ASSET;
        constexpr std::string_view kStageDir = ".perlin-update";
        constexpr std::string_view kOldSuffix = ".perlin-old";

        enum class Kind { Desktop, Android, Ios, Web };
        constexpr Kind kKind =
#if defined(__EMSCRIPTEN__)
            Kind::Web;
#elif defined(__ANDROID__)
            Kind::Android;
#elif defined(PERLIN_IOS)
            Kind::Ios;
#else
            Kind::Desktop;
#endif

        struct Shared {
            std::mutex mutex;
            Status status;
            std::string latestSha;
            fs::path payload; // the staged folder (desktop) or the apk (android)
            std::atomic<bool> restart{false};
            std::atomic<bool> started{false};
        };
        Shared& State_() {
            static Shared s;
            return s;
        }

        void Set(State state, std::string message, float progress = 0) {
            Shared& s = State_();
            std::lock_guard lock(s.mutex);
            s.status.state = state;
            s.status.message = std::move(message);
            s.status.progress = progress;
        }

        std::string Short(std::string_view sha) {
            return std::string(sha.substr(0, 7));
        }

        std::string ReleasePage() {
            return "https://github.com/" + std::string(kRepo) + "/releases/tag/nightly";
        }

        // a Linux AppImage replaces its own single file (APPIMAGE is set by its runtime); the unpacked tree it runs
        // from is read-only
        std::string AppImagePath() {
#if defined(__linux__) && !defined(__ANDROID__)
            if (const char* p = std::getenv("APPIMAGE"); p && *p)
                return p;
#endif
            return {};
        }

        std::string AppImageAsset() {
            std::string a(kAsset);
            if (a.ends_with(".zip"))
                a.replace(a.size() - 4, 4, ".AppImage");
            return a;
        }

        // Flatpak (and distribution packages) update through their own package manager
        bool ManagedElsewhere() {
            return std::getenv("FLATPAK_ID") != nullptr || std::getenv("SNAP") != nullptr;
        }

#if !defined(__EMSCRIPTEN__)
        // the folder the package was unpacked into: beside the executable, or around the .app on macOS
        fs::path InstallRoot() {
            fs::path exe = paths::ExeDir();
#if defined(__APPLE__)
            for (fs::path p = exe; p.has_parent_path() && p != p.parent_path(); p = p.parent_path())
                if (p.extension() == ".app")
                    return p.parent_path();
#endif
            return exe;
        }

        fs::path StageRoot() {
            return kKind == Kind::Desktop ? InstallRoot() / std::string(kStageDir)
                                          : fs::path(paths::DataDir()) / "update";
        }

        // the nightly tag's commit, from the GitHub api
        std::string LatestCommit() {
            const http::Response r = http::Request(
                "api.github.com", 443, true, "GET", "/repos/" + std::string(kRepo) + "/git/ref/tags/nightly",
                {}, {}, {{"User-Agent", "Perlin-updater"}, {"Accept", "application/vnd.github+json"}}, 15);
            if (r.status != 200)
                return {};
            const size_t object = r.body.find("\"object\"");
            const size_t key = r.body.find("\"sha\"", object == std::string::npos ? 0 : object);
            if (key == std::string::npos)
                return {};
            const size_t open = r.body.find('"', r.body.find(':', key) + 1);
            const size_t close = r.body.find('"', open + 1);
            return open == std::string::npos || close == std::string::npos
                       ? std::string()
                       : r.body.substr(open + 1, close - open - 1);
        }

        // unzips keeping unix permissions and symbolic links (an .app is useless without its executable bit)
        bool Extract(const fs::path& zipPath, const fs::path& into) {
            mz_zip_archive zip{};
            FILE* file = nullptr;
            if (!OpenZipReader(zip, zipPath, file))
                return false;
            bool ok = true;
            const mz_uint count = mz_zip_reader_get_num_files(&zip);
            for (mz_uint i = 0; i < count && ok; i++) {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&zip, i, &st))
                    continue;
                const std::string name = st.m_filename;
                if (name.starts_with("__MACOSX/") || name.find("..") != std::string::npos)
                    continue;
                const fs::path out = into / fs::path(name).relative_path();
                std::error_code ec;
                if (st.m_is_directory) {
                    fs::create_directories(out, ec);
                    continue;
                }
                fs::create_directories(out.parent_path(), ec);
                const uint32_t mode =
                    (st.m_version_made_by >> 8) == 3 ? (st.m_external_attr >> 16) & 0xFFFF : 0;
#if !defined(_WIN32)
                if ((mode & 0170000) == 0120000) {
                    size_t size = 0;
                    void* target = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
                    if (target) {
                        fs::create_symlink(std::string(static_cast<char*>(target), size), out, ec);
                        mz_free(target);
                    }
                    continue;
                }
#endif
                ok = mz_zip_reader_extract_to_file(&zip, i, out.string().c_str(), 0);
#if !defined(_WIN32)
                // an archive without unix modes: executables are the bundle's MacOS/ files and extensionless top-level files
                const bool executable =
                    name.find("/Contents/MacOS/") != std::string::npos ||
                    (name.find('/') == std::string::npos && fs::path(name).extension().empty());
                if (ok && ((mode & 0777) || executable))
                    ::chmod(out.c_str(), (mode & 0777) ? (mode & 0777) : 0755);
#endif
            }
            mz_zip_reader_end(&zip);
            if (file)
                std::fclose(file);
            return ok;
        }

        // the staged copy of `sha`, if a previous run already downloaded and unpacked it
        fs::path StagedFor(const std::string& sha) {
            const fs::path dir = StageRoot() / sha;
            std::error_code ec;
            if (kKind == Kind::Android)
                return fs::exists(dir / "Perlin.apk", ec) ? dir / "Perlin.apk" : fs::path();
            return fs::exists(dir / ".complete", ec) ? dir / "files" : fs::path();
        }

        void Work() {
            Set(State::Checking, "Checking for updates...");
            const std::string sha = LatestCommit();
            if (sha.empty()) {
                Set(State::Failed, "Could not check for updates");
                return;
            }
            {
                Shared& s = State_();
                std::lock_guard lock(s.mutex);
                s.latestSha = sha;
                s.status.latest = Short(sha);
            }
            if (sha == kCommit) {
                Set(State::UpToDate, "Up to date");
                return;
            }
            Log("update", "nightly {} is newer than this build ({})", Short(sha), Short(kCommit));
            if (kKind == Kind::Ios) {
                Set(State::Available, "Update " + Short(sha) + " available - tap to get it");
                return;
            }
#if defined(__linux__) && !defined(__ANDROID__)
            if (const std::string image = AppImagePath(); !image.empty()) {
                const fs::path next = image + ".new";
                std::error_code ec;
                std::string have;
                if (std::ifstream mark(image + ".new.sha"); mark)
                    mark >> have;
                if (have != sha || !fs::exists(next, ec)) {
                    download::Progress progress;
                    std::atomic<bool> done{false};
                    std::thread watcher([&] {
                        while (!done) {
                            const uint64_t total = progress.total.load();
                            Set(State::Downloading, "Downloading update...",
                                total ? static_cast<float>(progress.done.load()) / static_cast<float>(total)
                                      : 0.0f);
                            std::this_thread::sleep_for(std::chrono::milliseconds(250));
                        }
                    });
                    const bool fetched = download::Fetch("https://github.com/" + std::string(kRepo) +
                                                             "/releases/download/nightly/" + AppImageAsset(),
                                                         next, &progress);
                    done = true;
                    watcher.join();
                    if (!fetched) {
                        Set(State::Available,
                            "Update " + Short(sha) + " available (could not write beside the AppImage)");
                        return;
                    }
                    ::chmod(next.c_str(), 0755);
                    std::ofstream(image + ".new.sha") << sha;
                }
                State_().payload = next;
                Set(State::Ready, "Restart to update");
                return;
            }
#endif
            if (fs::path staged = StagedFor(sha); !staged.empty()) {
                State_().payload = staged;
                Set(State::Ready,
                    kKind == Kind::Android ? "Update downloaded - tap to install" : "Restart to update");
                return;
            }

            const fs::path dir = StageRoot() / sha;
            std::error_code ec;
            fs::remove_all(StageRoot(), ec);
            fs::create_directories(dir, ec);
            if (ec) {
                Set(State::Available, "Update " + Short(sha) + " available (this folder is read-only)");
                return;
            }
            const std::string url = "https://github.com/" + std::string(kRepo) +
                                    "/releases/download/nightly/" + std::string(kAsset);
            const fs::path archive = dir / std::string(kAsset);
            download::Progress progress;
            std::atomic<bool> done{false};
            std::thread watcher([&] {
                while (!done) {
                    const uint64_t total = progress.total.load();
                    Set(State::Downloading, "Downloading update...",
                        total ? static_cast<float>(progress.done.load()) / static_cast<float>(total) : 0.0f);
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
            });
            const bool fetched = download::Fetch(url, archive, &progress);
            done = true;
            watcher.join();
            if (!fetched) {
                Set(State::Failed, "Update download failed");
                return;
            }
            if (kKind == Kind::Android) {
                fs::rename(archive, dir / "Perlin.apk", ec);
                State_().payload = dir / "Perlin.apk";
                Set(State::Ready, "Update downloaded - tap to install");
                return;
            }
            if (!Extract(archive, dir / "files")) {
                Set(State::Failed, "Update package is damaged");
                return;
            }
            fs::remove(archive, ec);
            std::FILE* mark = std::fopen((dir / ".complete").string().c_str(), "wb");
            if (mark)
                std::fclose(mark);
            State_().payload = dir / "files";
            // also installed by itself when the game closes, without the restart
            Set(State::Ready, "Restart to update");
        }

        // moves every top-level item of the staged package over the installed one; the replaced files are renamed
        // aside (a running executable can be renamed, not overwritten) and deleted on the next start
        bool Install(const fs::path& staged) {
            std::error_code ec;
            if (const std::string image = AppImagePath(); !image.empty()) {
                // the running AppImage is open, not locked: the new file simply takes its name
                fs::rename(staged, image, ec);
                fs::remove(image + ".new.sha", ec);
                if (ec)
                    Log("update", "could not replace the AppImage");
                else
                    Log("update", "installed the update");
                return !ec;
            }
            const fs::path root = InstallRoot();
            std::vector<std::pair<fs::path, fs::path>> moved;
            bool ok = true;
            std::vector<fs::path> items;
            for (const fs::directory_entry& e : fs::directory_iterator(staged, ec))
                items.push_back(e.path());
            for (const fs::path& item : items) {
                const fs::path target = root / item.filename();
                const fs::path aside = target.string() + std::string(kOldSuffix);
                fs::remove_all(aside, ec);
                if (fs::exists(target, ec)) {
                    fs::rename(target, aside, ec);
                    if (ec) {
                        ok = false;
                        break;
                    }
                }
                fs::rename(item, target, ec);
                if (ec) {
                    fs::rename(aside, target, ec);
                    ok = false;
                    break;
                }
                moved.emplace_back(target, aside);
            }
            if (!ok)
                for (const auto& [target, aside] : moved) {
                    fs::remove_all(target, ec);
                    fs::rename(aside, target, ec);
                }
            fs::remove_all(root / std::string(kStageDir), ec);
            if (ok)
                Log("update", "installed the update");
            else
                Log("update", "could not install the update; the old build stays");
            return ok;
        }

        void Relaunch() {
            const fs::path root = InstallRoot();
#if defined(__linux__) && !defined(__ANDROID__)
            if (const std::string image = AppImagePath(); !image.empty()) {
                char* argv[] = {const_cast<char*>(image.c_str()), nullptr};
                pid_t pid = 0;
                posix_spawn(&pid, image.c_str(), nullptr, nullptr, argv, environ);
                return;
            }
#endif
#if defined(__ANDROID__) || defined(PERLIN_IOS)
            (void)root;
#elif defined(_WIN32)
            wchar_t path[MAX_PATH * 4] = {};
            if (!GetModuleFileNameW(nullptr, path, MAX_PATH * 4))
                return;
            const fs::path exe = root / fs::path(path).filename();
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            std::wstring line = L"\"" + exe.wstring() + L"\"";
            if (CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, root.c_str(),
                               &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
#elif defined(__APPLE__)
            fs::path app;
            for (fs::path p = paths::ExeDir(); p.has_parent_path() && p != p.parent_path();
                 p = p.parent_path())
                if (p.extension() == ".app")
                    app = root / p.filename();
            if (app.empty())
                return;
            const std::string target = app.string();
            char* argv[] = {const_cast<char*>("open"), const_cast<char*>("-n"),
                            const_cast<char*>(target.c_str()), nullptr};
            pid_t pid = 0;
            posix_spawn(&pid, "/usr/bin/open", nullptr, nullptr, argv, environ);
#else
            // the running file was renamed aside: start the one that now has its old name
            std::error_code ec;
            std::string self = fs::read_symlink("/proc/self/exe", ec).filename().string();
            if (self.ends_with(kOldSuffix))
                self.resize(self.size() - kOldSuffix.size());
            if (self.empty())
                self = "Perlin";
            const std::string target = (root / self).string();
            char* argv[] = {const_cast<char*>(target.c_str()), nullptr};
            pid_t pid = 0;
            posix_spawn(&pid, target.c_str(), nullptr, nullptr, argv, environ);
#endif
        }

#if defined(__ANDROID__)
        // PerlinActivity.installApk: a PackageInstaller session; Android shows its own confirmation
        bool InstallApk(const fs::path& apk) {
            auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (!env || !activity)
                return false;
            jclass cls = env->GetObjectClass(activity);
            jmethodID method = env->GetMethodID(cls, "installApk", "(Ljava/lang/String;)Z");
            bool ok = false;
            if (method) {
                jstring path = env->NewStringUTF(apk.string().c_str());
                ok = env->CallBooleanMethod(activity, method, path);
                env->DeleteLocalRef(path);
            }
            if (env->ExceptionCheck())
                env->ExceptionClear();
            env->DeleteLocalRef(cls);
            env->DeleteLocalRef(activity);
            return ok;
        }
#endif
#endif // !__EMSCRIPTEN__

    }

    bool Supported() {
        if constexpr (kKind == Kind::Web)
            return !kCommit.empty();
        if (ManagedElsewhere())
            return false;
        return !kCommit.empty() && !kRepo.empty() && (kKind == Kind::Ios || !kAsset.empty());
    }

    std::string BuildLabel() {
        return kCommit.empty() ? "dev build" : "nightly " + Short(kCommit);
    }

    void OnStartup() {
#if !defined(__EMSCRIPTEN__)
        if (kKind != Kind::Desktop)
            return;
        std::error_code ec;
        for (const fs::directory_entry& e : fs::directory_iterator(InstallRoot(), ec))
            if (e.path().filename().string().ends_with(std::string(kOldSuffix)))
                fs::remove_all(e.path(), ec);
#endif
    }

    void Start(bool enabled) {
        if (!Supported() || !enabled) {
            Set(State::Disabled, Supported() ? "Auto update is off" : "");
            return;
        }
        if (State_().started.exchange(true))
            return;
#if defined(__EMSCRIPTEN__)
        Set(State::Checking, "Checking for updates...");
        perlin_js_check_version();
#else
        std::thread([] {
            try {
                Work();
            } catch (const std::exception& e) {
                Set(State::Failed, std::string("Update failed: ") + e.what());
            }
        }).detach();
#endif
    }

    Status Current() {
        Shared& s = State_();
        std::lock_guard lock(s.mutex);
        return s.status;
    }

    bool Act() {
        const Status st = Current();
        switch (kKind) {
        case Kind::Web:
#if defined(__EMSCRIPTEN__)
            if (st.state == State::Available)
                perlin_js_reload_fresh();
#endif
            return false;
        case Kind::Ios:
            if (st.state == State::Available)
                SDL_OpenURL(ReleasePage().c_str());
            return false;
        case Kind::Android:
#if defined(__ANDROID__)
            if (st.state == State::Ready && !InstallApk(State_().payload))
                SDL_OpenURL(ReleasePage().c_str());
#endif
            return false;
        case Kind::Desktop:
            if (st.state == State::Ready) {
                State_().restart = true;
                return true;
            }
            if (st.state == State::Available)
                SDL_OpenURL(ReleasePage().c_str());
            return false;
        }
        return false;
    }

    void OnExit() {
#if !defined(__EMSCRIPTEN__)
        if (kKind != Kind::Desktop || Current().state != State::Ready)
            return;
        if (Install(State_().payload) && State_().restart)
            Relaunch();
#endif
    }
}

#if defined(__EMSCRIPTEN__)
extern "C" EMSCRIPTEN_KEEPALIVE void perlin_web_version(const char* commit) {
    using namespace perlin::update;
    const std::string latest = commit ? commit : "";
    if (latest.empty() || latest == kCommit)
        Set(State::UpToDate, "Up to date");
    else
        Set(State::Available, "A newer version is live - click to reload");
}
#endif
