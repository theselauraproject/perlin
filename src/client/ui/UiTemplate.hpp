#pragma once
#include <chrono>
#include <cstdint>
#include <string>

namespace perlin::ui {
    std::string LoadUiTemplate(const std::string& dataDir, const std::string& fileName,
                               const std::string& defaultText, uint64_t& version);
    std::string ReadMarkup(const std::string& resourceName);
    std::string ExpandSceneTokens(std::string text);
    void ReplaceAll(std::string& text, const std::string& key, const std::string& value);
    const std::string& BuiltinTemplate(const std::string& resourceName);
    std::string ScreenTemplate(const std::string& fileName);

    struct ScreenTemplateDefault {
        explicit ScreenTemplateDefault(const char* fileName);
    };
    void ExportScreenTemplates();

    struct TemplateSlot {
        std::string text;
        uint64_t version = 0;
        std::chrono::steady_clock::time_point checkAt;

        bool Refresh(const char* fileName, const std::string& defaultText, std::chrono::milliseconds every,
                     std::chrono::steady_clock::time_point now, const char* logLabel = nullptr);
    };
}
