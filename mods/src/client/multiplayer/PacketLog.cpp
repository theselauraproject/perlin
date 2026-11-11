#include "client/multiplayer/PacketLog.hpp"

#include "protocol/Packets.hpp"
#include "util/Paths.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include "util/Log.hpp"

namespace perlin::packetlog {
    namespace fs = std::filesystem;

    namespace {
        constexpr size_t kMaxLine = 64 * 1024;

        std::mutex mutex;
        std::chrono::steady_clock::time_point checkedAt{};
        bool active = false;
        std::FILE* file = nullptr;
        std::set<std::string> only, skip;
        fs::file_time_type controlStamp{};

        std::string Clock() {
            const auto now = std::chrono::system_clock::now();
            const std::time_t t = std::chrono::system_clock::to_time_t(now);
            const int ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
            return buf;
        }

        void ReadFilter(const fs::path& control) {
            only.clear();
            skip.clear();
            std::ifstream in(control);
            std::string word;
            while (in >> word) {
                for (char& c : word)
                    if (c == ',')
                        c = ' ';
                std::istringstream parts(word);
                std::string name;
                while (parts >> name)
                    (name[0] == '-' ? skip : only).insert(name[0] == '-' ? name.substr(1) : name);
            }
        }

        void Poll() {
            const auto now = std::chrono::steady_clock::now();
            if (now - checkedAt < std::chrono::milliseconds(250))
                return;
            checkedAt = now;
            const fs::path dir = paths::DataDir();
            const fs::path control = dir / "packetlog";
            std::error_code ec;
            const bool wanted = fs::exists(control, ec);
            if (wanted) {
                const auto stamp = fs::last_write_time(control, ec);
                if (!active || stamp != controlStamp) {
                    controlStamp = stamp;
                    ReadFilter(control);
                }
            }
            if (wanted == active)
                return;
            active = wanted;
            if (active) {
                file = std::fopen((dir / "packets.log").string().c_str(), "ab");
                if (!file) {
                    active = false;
                    return;
                }
                std::fprintf(file, "=== %s capture started\n", Clock().c_str());
                std::fflush(file);
                Log("packetlog", "capture started -> {}", (dir / "packets.log").string());
            } else {
                std::fprintf(file, "=== %s capture stopped\n", Clock().c_str());
                std::fclose(file);
                file = nullptr;
                Log("packetlog", "capture stopped");
            }
        }
    }

    void Record(bool outgoing, uint32_t id, const uint8_t* data, size_t size) {
        std::lock_guard lock(mutex);
        Poll();
        if (!active)
            return;
        const std::string name = proto::PacketName(id);
        if ((!only.empty() && !only.contains(name)) || skip.contains(name))
            return;
        std::string body;
        if (!proto::PacketJson(id, data, size, body)) {
            static const char* hex = "0123456789abcdef";
            body = "\"undecoded ";
            for (size_t i = 0; i < size && i < 256; i++) {
                body += hex[data[i] >> 4];
                body += hex[data[i] & 15];
            }
            body += size > 256 ? "...\"" : "\"";
        }
        if (body.size() > kMaxLine) {
            body.resize(kMaxLine);
            body += "...";
        }
        std::fprintf(file, "%s %s %s(%u) %zuB %s\n", Clock().c_str(), outgoing ? ">>" : "<<", name.c_str(),
                     id, size, body.c_str());
        std::fflush(file);
    }
}
