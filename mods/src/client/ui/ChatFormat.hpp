#pragma once
#include <string>
#include <vector>

namespace perlin::ui {
    std::string FormatTextPacket(int type, bool needsTranslation, const std::string& source,
                                 const std::string& message, const std::vector<std::string>& parameters);
    std::string FormatCommandOutput(const std::string& messageId, bool success,
                                    const std::vector<std::string>& parameters);
    std::string Translate(const std::string& key, const std::vector<std::string>& parameters);
    std::string FormatRawText(const std::string& json);
}
