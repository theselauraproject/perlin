#include "core/Log.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace perlin::server {
    namespace {
        std::string StripFormatting(const std::string& s) {
            std::string out;
            out.reserve(s.size());
            for (size_t i = 0; i < s.size(); i++) {
                if (static_cast<unsigned char>(s[i]) == 0xC2 && i + 1 < s.size() &&
                    static_cast<unsigned char>(s[i + 1]) == 0xA7) {
                    i += 2;
                    continue;
                }
                out.push_back(s[i]);
            }
            return out;
        }

        std::mutex g_mutex;
        LogSink g_sink;
        thread_local std::string* g_capture = nullptr;
    }

    LogCapture::LogCapture() : previous(g_capture) {
        g_capture = &text;
    }
    LogCapture::~LogCapture() {
        g_capture = previous;
    }

    void SetLogSink(LogSink sink) {
        std::lock_guard lock(g_mutex);
        g_sink = std::move(sink);
    }

    void Log(LogLevel level, const std::string& text) {
        if (g_capture && level == LogLevel::Info) {
            *g_capture += StripFormatting(text);
            *g_capture += '\n';
            return;
        }
        std::lock_guard lock(g_mutex);
        if (g_sink) {
            g_sink(level, text);
            return;
        }
        static const char* names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
        const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char stamp[16];
        std::strftime(stamp, sizeof stamp, "%H:%M:%S", std::localtime(&t));
        std::fprintf(stdout, "[%s %s] %s\n", stamp, names[static_cast<int>(level)],
                     StripFormatting(text).c_str());
        std::fflush(stdout);
    }
}
