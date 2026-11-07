#include "client/platform/DiscordRpc.hpp"

#include <cctype>

#include "util/Json.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define PERLIN_DISCORD_IPC 1
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE
#define PERLIN_DISCORD_UNIX 1
#endif
#elif (defined(__linux__) && !defined(__ANDROID__)) || defined(__FreeBSD__)
#define PERLIN_DISCORD_UNIX 1
#endif

#ifdef PERLIN_DISCORD_UNIX
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#define PERLIN_DISCORD_IPC 1
#endif

namespace perlin::discord {
    namespace {
        enum Opcode : uint32_t { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

        std::string Escape(const std::string& s) {
            std::string out;
            out.reserve(s.size() + 8);
            for (unsigned char c : s) {
                switch (c) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", c);
                        out += buf;
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
                }
            }
            return out;
        }

        std::string Field(std::string s, size_t max = 128) {
            if (s.size() > max) {
                size_t cut = max;
                while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
                    cut--;
                s.resize(cut);
            }
            if (!s.empty() && s.size() < 2)
                s += ' ';
            return s;
        }

        int ProcessId() {
#if defined(_WIN32)
            return static_cast<int>(GetCurrentProcessId());
#elif defined(PERLIN_DISCORD_IPC)
            return static_cast<int>(getpid());
#else
            return 0;
#endif
        }
    }

#if defined(_WIN32)
    struct Client::Pipe {
        HANDLE handle = INVALID_HANDLE_VALUE;
        bool Open() {
            for (int i = 0; i < 10; i++) {
                const std::string name = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
                handle = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                if (handle != INVALID_HANDLE_VALUE)
                    return true;
            }
            return false;
        }
        void Shut() {
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
        bool Write(const void* data, size_t len) {
            DWORD wrote = 0;
            return WriteFile(handle, data, static_cast<DWORD>(len), &wrote, nullptr) && wrote == len;
        }
        int Available(int timeoutMs) {
            for (int waited = 0;; waited += 20) {
                DWORD avail = 0;
                if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &avail, nullptr))
                    return -1;
                if (avail > 0)
                    return 1;
                if (waited >= timeoutMs)
                    return 0;
                Sleep(20);
            }
        }
        bool Read(void* data, size_t len) {
            auto* p = static_cast<uint8_t*>(data);
            while (len > 0) {
                DWORD got = 0;
                if (!ReadFile(handle, p, static_cast<DWORD>(len), &got, nullptr) || got == 0)
                    return false;
                p += got;
                len -= got;
            }
            return true;
        }
        ~Pipe() { Shut(); }
    };
#elif defined(PERLIN_DISCORD_IPC)
    struct Client::Pipe {
        int fd = -1;
        bool Open() {
            std::vector<std::string> roots;
            for (const char* env : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"})
                if (const char* v = std::getenv(env); v && *v)
                    roots.emplace_back(v);
            roots.emplace_back("/tmp");
            static const char* kSubdirs[] = {"", "app/com.discordapp.Discord/", "app/com.discordapp.DiscordCanary/",
                                             "snap.discord/", "snap.discord-canary/",
                                             ".flatpak/dev.vencord.Vesktop/xdg-run/"};
            for (const std::string& root : roots)
                for (const char* sub : kSubdirs)
                    for (int i = 0; i < 10; i++) {
                        std::string path = root;
                        if (!path.empty() && path.back() != '/')
                            path += '/';
                        path += std::string(sub) + "discord-ipc-" + std::to_string(i);
                        sockaddr_un addr{};
                        if (path.size() >= sizeof addr.sun_path)
                            continue;
                        addr.sun_family = AF_UNIX;
                        std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
                        const int s = socket(AF_UNIX, SOCK_STREAM, 0);
                        if (s < 0)
                            return false;
#ifdef SO_NOSIGPIPE
                        const int one = 1;
                        setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
                        if (connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) == 0) {
                            fd = s;
                            return true;
                        }
                        close(s);
                    }
            return false;
        }
        void Shut() {
            if (fd >= 0)
                close(fd);
            fd = -1;
        }
        bool Write(const void* data, size_t len) {
            const auto* p = static_cast<const uint8_t*>(data);
            while (len > 0) {
#ifdef MSG_NOSIGNAL
                const ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
#else
                const ssize_t n = send(fd, p, len, 0);
#endif
                if (n <= 0)
                    return false;
                p += n;
                len -= static_cast<size_t>(n);
            }
            return true;
        }
        int Available(int timeoutMs) {
            pollfd pfd{fd, POLLIN, 0};
            const int r = poll(&pfd, 1, timeoutMs);
            if (r < 0 || (r > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) && !(pfd.revents & POLLIN)))
                return -1;
            return r > 0 ? 1 : 0;
        }
        bool Read(void* data, size_t len) {
            auto* p = static_cast<uint8_t*>(data);
            while (len > 0) {
                const ssize_t n = recv(fd, p, len, 0);
                if (n <= 0)
                    return false;
                p += n;
                len -= static_cast<size_t>(n);
            }
            return true;
        }
        ~Pipe() { Shut(); }
    };
#else
    struct Client::Pipe {
        bool Open() { return false; }
        void Shut() {}
        bool Write(const void*, size_t) { return false; }
        int Available(int) { return -1; }
        bool Read(void*, size_t) { return false; }
    };
#endif

    namespace {
        template <class P> bool Send(P& pipe, uint32_t op, const std::string& json) {
            std::vector<uint8_t> frame(8 + json.size());
            const uint32_t len = static_cast<uint32_t>(json.size());
            std::memcpy(frame.data(), &op, 4);
            std::memcpy(frame.data() + 4, &len, 4);
            std::memcpy(frame.data() + 8, json.data(), json.size());
            return pipe.Write(frame.data(), frame.size());
        }

        template <class P> bool Receive(P& pipe, uint32_t& op, std::string& json) {
            uint32_t header[2];
            if (!pipe.Read(header, sizeof header))
                return false;
            if (header[1] > (1u << 20))
                return false;
            op = header[0];
            json.resize(header[1]);
            return header[1] == 0 || pipe.Read(json.data(), header[1]);
        }
    }

    bool Supported() {
#if defined(PERLIN_DISCORD_IPC)
        return true;
#else
        return false;
#endif
    }

    Client::Client(std::string applicationId) : appId(std::move(applicationId)) {
        if (Supported())
            worker = std::thread([this] { Run(); });
    }

    Client::~Client() {
        stop = true;
        wake.notify_all();
        if (worker.joinable())
            worker.join();
    }

    void Client::Set(std::optional<Presence> presence) {
        std::lock_guard lock(mutex);
        if (presence == wanted)
            return;
        wanted = std::move(presence);
        wantedGeneration++;
        wake.notify_all();
    }

    std::optional<std::string> Client::TakeJoin() {
        std::lock_guard lock(mutex);
        if (joins.empty())
            return std::nullopt;
        std::string s = std::move(joins.front());
        joins.pop_front();
        return s;
    }

    std::optional<JoinRequest> Client::TakeAccepted() {
        std::lock_guard lock(mutex);
        if (accepted.empty())
            return std::nullopt;
        JoinRequest r = std::move(accepted.front());
        accepted.pop_front();
        return r;
    }

    std::string Client::User() const {
        std::lock_guard lock(mutex);
        return user;
    }

    std::string Client::Nonce() {
        return std::to_string(++nonce);
    }

    std::string Client::ActivityJson(const std::optional<Presence>& p) const {
        if (!p)
            return "null";
        std::string a = "{";
        bool first = true;
        const auto add = [&](const std::string& key, const std::string& value) {
            a += (first ? "\"" : ",\"") + key + "\":" + value;
            first = false;
        };
        const auto str = [](const std::string& s) { return "\"" + Escape(s) + "\""; };
        if (!p->details.empty())
            add("details", str(Field(p->details)));
        if (!p->state.empty())
            add("state", str(Field(p->state)));
        if (p->startUnix > 0)
            add("timestamps", "{\"start\":" + std::to_string(p->startUnix) + "}");
        std::string assets;
        const auto asset = [&](const char* key, const std::string& v) {
            if (!v.empty())
                assets += (assets.empty() ? "\"" : ",\"") + std::string(key) + "\":" + str(Field(v, 256));
        };
        asset("large_image", p->largeImage);
        asset("large_text", p->largeText);
        asset("small_image", p->smallImage);
        asset("small_text", p->smallText);
        if (!assets.empty())
            add("assets", "{" + assets + "}");
        if (!p->partyId.empty()) {
            std::string party = "{\"id\":" + str(Field(p->partyId));
            if (p->partyMax > 0)
                party += ",\"size\":[" + std::to_string(std::max(1, p->partySize)) + "," +
                         std::to_string(std::max({1, p->partySize, p->partyMax})) + "]";
            add("party", party + "}");
        }
        if (!p->joinSecret.empty()) {
            add("secrets", "{\"join\":" + str(Field(p->joinSecret)) + "}");
        } else if (!p->buttons.empty()) {
            std::string b = "[";
            for (size_t i = 0; i < p->buttons.size() && i < 2; i++)
                b += (i ? ",{\"label\":" : "{\"label\":") + str(Field(p->buttons[i].first, 32)) +
                     ",\"url\":" + str(Field(p->buttons[i].second, 512)) + "}";
            add("buttons", b + "]");
        }
        add("instance", "false");
        return a + "}";
    }

    void Client::Handle(Pipe& pipe, const std::string& text) {
        const auto doc = json::Document::Parse(text);
        if (!doc)
            return;
        const json::View root = doc->root();
        const std::string evt = root["evt"].str();
        const std::string cmd = root["cmd"].str();
        if (evt == "READY") {
            std::lock_guard lock(mutex);
            user = root["data"]["user"]["username"].str();
            return;
        }
        if (evt == "ERROR") {
            Log("discord", "{} failed: {}", cmd, root["data"]["message"].str());
            return;
        }
        if (cmd != "DISPATCH")
            return;
        if (evt == "ACTIVITY_JOIN") {
            std::lock_guard lock(mutex);
            if (joins.size() < 4)
                joins.push_back(root["data"]["secret"].str());
        } else if (evt == "ACTIVITY_JOIN_REQUEST") {
            JoinRequest r{root["data"]["user"]["id"].str(), root["data"]["user"]["username"].str()};
            if (r.userId.empty() || r.userId.size() > 32 ||
                !std::all_of(r.userId.begin(), r.userId.end(), [](char c) { return c >= '0' && c <= '9'; }))
                return;
            const bool yes = autoAccept.load();
            Send(pipe, Frame,
                 std::string("{\"cmd\":\"") + (yes ? "SEND_ACTIVITY_JOIN_INVITE" : "CLOSE_ACTIVITY_REQUEST") +
                     "\",\"args\":{\"user_id\":\"" + r.userId + "\"},\"nonce\":\"" + Nonce() + "\"}");
            if (yes) {
                std::lock_guard lock(mutex);
                if (accepted.size() < 8)
                    accepted.push_back(std::move(r));
            }
        }
    }

    bool Client::Session(Pipe& pipe) {
        if (!Send(pipe, Handshake, "{\"v\":1,\"client_id\":\"" + Escape(appId) + "\"}"))
            return false;
        uint32_t op = 0;
        std::string text;
        if (pipe.Available(5000) <= 0 || !Receive(pipe, op, text) || op != Frame)
            return false;
        Handle(pipe, text);
        connected = true;
        const std::string name = User();
        Log("discord", "connected to Discord{}", name.empty() ? std::string() : " as " + name);
        for (const char* evt : {"ACTIVITY_JOIN", "ACTIVITY_JOIN_REQUEST"})
            if (!Send(pipe, Frame,
                      std::string("{\"cmd\":\"SUBSCRIBE\",\"evt\":\"") + evt + "\",\"nonce\":\"" + Nonce() + "\"}"))
                return false;
        uint64_t sentGeneration = 0;
        auto lastSent = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        while (!stop) {
            std::optional<Presence> presence;
            uint64_t generation = 0;
            {
                std::lock_guard lock(mutex);
                generation = wantedGeneration;
                presence = wanted;
            }
            const auto now = std::chrono::steady_clock::now();
            if (generation != sentGeneration && now - lastSent >= std::chrono::seconds(4)) {
                const std::string body = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(ProcessId()) +
                                         ",\"activity\":" + ActivityJson(presence) + "},\"nonce\":\"" + Nonce() + "\"}";
                if (!Send(pipe, Frame, body))
                    return false;
                sentGeneration = generation;
                lastSent = now;
            }
            const int ready = pipe.Available(250);
            if (ready < 0)
                return false;
            if (ready == 0)
                continue;
            if (!Receive(pipe, op, text))
                return false;
            if (op == Ping) {
                if (!Send(pipe, Pong, text))
                    return false;
            } else if (op == Close) {
                Log("discord", "Discord closed the connection: {}", text.substr(0, 200));
                return false;
            } else if (op == Frame) {
                Handle(pipe, text);
            }
        }
        return true;
    }

    void Client::Run() {
        int delay = 2;
        while (!stop) {
            Pipe pipe;
            if (pipe.Open()) {
                Session(pipe);
                pipe.Shut();
                if (connected.exchange(false))
                    Log("discord", "disconnected from Discord");
                delay = 5;
            } else {
                delay = std::min(delay * 2, 30);
            }
            std::unique_lock lock(mutex);
            wake.wait_for(lock, std::chrono::seconds(delay), [this] { return stop.load(); });
        }
    }

    std::string ServerFromUrl(const std::string& url) {
        constexpr const char* kScheme = "perlin://";
        if (url.size() <= 9 || url.compare(0, 9, kScheme) != 0)
            return {};
        std::string rest = url.substr(9);
        if (const size_t q = rest.find_first_of("?#"); q != std::string::npos) {
            // perlin://join?server=host:port
            const std::string query = rest.substr(q + 1);
            rest.erase(q);
            if (rest.empty() || rest == "join" || rest == "connect") {
                const size_t k = query.find("server=");
                if (k == std::string::npos)
                    return {};
                rest = query.substr(k + 7);
                rest = rest.substr(0, rest.find('&'));
            }
        }
        for (const char* verb : {"join/", "connect/", "server/"})
            if (rest.starts_with(verb))
                rest.erase(0, std::char_traits<char>::length(verb));
        while (!rest.empty() && rest.back() == '/')
            rest.pop_back();
        std::string out;
        for (size_t i = 0; i < rest.size(); i++) {
            if (rest[i] == '%' && i + 2 < rest.size() + 0 && std::isxdigit(static_cast<unsigned char>(rest[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(rest[i + 2]))) {
                out += static_cast<char>(std::stoi(rest.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else
                out += rest[i];
        }
        // a hostname or address with an optional port, nothing else
        for (char c : out)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':' || c == '_' ||
                  c == '[' || c == ']'))
                return {};
        return out;
    }

    bool RegisterUrlScheme(const std::string& scheme, const std::string& executable) {
        if (scheme.empty() || executable.empty())
            return false;
        std::error_code ec;
#if defined(_WIN32)
        const std::string key = "Software\\Classes\\" + scheme;
        HKEY root = nullptr;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &root, nullptr) !=
            ERROR_SUCCESS)
            return false;
        const auto set = [](HKEY k, const char* name, const std::string& value) {
            RegSetValueExA(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                           static_cast<DWORD>(value.size() + 1));
        };
        std::string exe = executable;
        std::replace(exe.begin(), exe.end(), '/', '\\');
        set(root, nullptr, "URL:Perlin");
        set(root, "URL Protocol", "");
        HKEY icon = nullptr;
        if (RegCreateKeyExA(root, "DefaultIcon", 0, nullptr, 0, KEY_WRITE, nullptr, &icon, nullptr) == ERROR_SUCCESS) {
            set(icon, nullptr, exe);
            RegCloseKey(icon);
        }
        HKEY command = nullptr;
        if (RegCreateKeyExA(root, "shell\\open\\command", 0, nullptr, 0, KEY_WRITE, nullptr, &command, nullptr) ==
            ERROR_SUCCESS) {
            set(command, nullptr, "\"" + exe + "\" \"%1\"");
            RegCloseKey(command);
        }
        RegCloseKey(root);
        return true;
#elif defined(__APPLE__)
        (void)ec;
        return false; // declared by the bundle (CFBundleURLTypes)
#elif defined(PERLIN_DISCORD_UNIX)
        std::string exe = executable;
        if (const char* appimage = std::getenv("APPIMAGE"); appimage && *appimage)
            exe = appimage;
        std::filesystem::path dir;
        if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data)
            dir = std::filesystem::path(data) / "applications";
        else if (const char* home = std::getenv("HOME"); home && *home)
            dir = std::filesystem::path(home) / ".local/share/applications";
        else
            return false;
        std::filesystem::create_directories(dir, ec);
        const std::string file = "perlin-url-" + scheme + ".desktop";
        std::string quoted = "\"";
        for (char c : exe) {
            if (c == '"' || c == '`' || c == '$' || c == '\\')
                quoted += '\\';
            quoted += c;
        }
        quoted += '"';
        const std::string desktop = "[Desktop Entry]\nName=Perlin\nExec=" + quoted +
                                    " %u\nType=Application\nNoDisplay=true\nCategories=Game;\nMimeType=x-scheme-handler/" +
                                    scheme + ";\n";
        const std::filesystem::path path = dir / file;
        std::string existing;
        if (std::ifstream in(path); in)
            existing.assign(std::istreambuf_iterator<char>(in), {});
        if (existing == desktop)
            return true;
        std::ofstream(path, std::ios::trunc) << desktop;
        const std::string mime = "x-scheme-handler/" + scheme;
        const pid_t child = fork();
        if (child == 0) {
            const int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, 1);
                dup2(devnull, 2);
            }
            execlp("xdg-mime", "xdg-mime", "default", file.c_str(), mime.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        if (child > 0)
            waitpid(child, nullptr, 0);
        return child > 0;
#else
        (void)ec;
        return false;
#endif
    }

    bool RegisterLaunch(const std::string& applicationId, const std::string& executable) {
        if (applicationId.empty() || executable.empty())
            return false;
        std::error_code ec;
#if defined(_WIN32)
        const std::string key = "Software\\Classes\\discord-" + applicationId;
        HKEY root = nullptr;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &root, nullptr) !=
            ERROR_SUCCESS)
            return false;
        const auto set = [](HKEY k, const char* name, const std::string& value) {
            RegSetValueExA(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                           static_cast<DWORD>(value.size() + 1));
        };
        std::string exe = executable;
        std::replace(exe.begin(), exe.end(), '/', '\\');
        set(root, nullptr, "URL:Run game " + applicationId + " protocol");
        set(root, "URL Protocol", "");
        HKEY icon = nullptr;
        if (RegCreateKeyExA(root, "DefaultIcon", 0, nullptr, 0, KEY_WRITE, nullptr, &icon, nullptr) == ERROR_SUCCESS) {
            set(icon, nullptr, exe);
            RegCloseKey(icon);
        }
        HKEY command = nullptr;
        if (RegCreateKeyExA(root, "shell\\open\\command", 0, nullptr, 0, KEY_WRITE, nullptr, &command, nullptr) ==
            ERROR_SUCCESS) {
            set(command, nullptr, "\"" + exe + "\" \"%1\"");
            RegCloseKey(command);
        }
        RegCloseKey(root);
        return true;
#elif defined(__APPLE__) && defined(PERLIN_DISCORD_UNIX)
        const char* home = std::getenv("HOME");
        if (!home)
            return false;
        const std::filesystem::path dir = std::filesystem::path(home) / "Library/Application Support/discord/games";
        std::filesystem::create_directories(dir, ec);
        std::ofstream(dir / (applicationId + ".json"), std::ios::trunc)
            << "{\"command\": \"" << Escape(executable) << "\"}";
        return !ec;
#elif defined(PERLIN_DISCORD_UNIX)
        std::string exe = executable;
        if (const char* appimage = std::getenv("APPIMAGE"); appimage && *appimage)
            exe = appimage;
        std::filesystem::path dir;
        if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data)
            dir = std::filesystem::path(data) / "applications";
        else if (const char* home = std::getenv("HOME"); home && *home)
            dir = std::filesystem::path(home) / ".local/share/applications";
        else
            return false;
        std::filesystem::create_directories(dir, ec);
        const std::string file = "discord-" + applicationId + ".desktop";
        std::string quoted = "\"";
        for (char c : exe) {
            if (c == '"' || c == '`' || c == '$' || c == '\\')
                quoted += '\\';
            quoted += c;
        }
        quoted += '"';
        const std::string desktop = "[Desktop Entry]\nName=Perlin\nExec=" + quoted +
                                    " %u\nType=Application\nNoDisplay=true\nCategories=Discord;Games;\nMimeType=x-scheme-handler/discord-" +
                                    applicationId + ";\n";
        const std::filesystem::path path = dir / file;
        std::string existing;
        if (std::ifstream in(path); in)
            existing.assign(std::istreambuf_iterator<char>(in), {});
        if (existing == desktop)
            return true;
        std::ofstream(path, std::ios::trunc) << desktop;
        const std::string scheme = "x-scheme-handler/discord-" + applicationId;
        const pid_t child = fork();
        if (child == 0) {
            const int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, 1);
                dup2(devnull, 2);
            }
            execlp("xdg-mime", "xdg-mime", "default", file.c_str(), scheme.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        if (child > 0)
            waitpid(child, nullptr, 0);
        return child > 0;
#else
        (void)ec;
        return false;
#endif
    }
}
