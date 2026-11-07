#pragma once
#include <functional>
#include <string>

namespace perlin::server {

    enum class LogLevel : int { Debug, Info, Warn, Error };

    using LogSink = std::function<void(LogLevel, const std::string&)>;
    void SetLogSink(LogSink sink);
    void Log(LogLevel level, const std::string& text);

    class LogCapture {
    public:
        LogCapture();
        ~LogCapture();
        LogCapture(const LogCapture&) = delete;
        LogCapture& operator=(const LogCapture&) = delete;
        const std::string& Text() const { return text; }

    private:
        std::string text;
        std::string* previous = nullptr;
    };

    inline void LogInfo(const std::string& text) {
        Log(LogLevel::Info, text);
    }
    inline void LogWarn(const std::string& text) {
        Log(LogLevel::Warn, text);
    }
    inline void LogError(const std::string& text) {
        Log(LogLevel::Error, text);
    }
}
