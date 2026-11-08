#pragma once
#include "client/javashaders/ProgramSet.hpp"

#include <map>
#include <string>
#include <vector>

namespace perlin::jshader {
    struct ScreenEntry {
        enum class Kind { Option, Screen, Profile, Empty };
        Kind kind = Kind::Empty;
        std::string name;
    };

    struct ScreenLayout {
        std::string title;
        std::vector<ScreenEntry> entries;
        int columns = 2;
    };

    ScreenLayout BuildScreen(const ShaderPackInstance& pack, const std::string& name);

    bool IsSlider(const ShaderPackInstance& pack, const std::string& option);

    std::vector<std::pair<std::string, OptionValues>> Profiles(const ShaderPackInstance& pack);

    std::string MatchingProfile(const ShaderPackInstance& pack, const OptionValues& current);

    std::string OptionValue(const ShaderPackInstance& pack, const ShaderOption& option,
                            const OptionValues& current);
    const ShaderOption* FindOption(const ShaderPackInstance& pack, const std::string& name);

    std::string OptionLabel(const ShaderPackInstance& pack, const std::string& option);
    std::string OptionComment(const ShaderPackInstance& pack, const ShaderOption& option);
    std::string ValueLabel(const ShaderPackInstance& pack, const ShaderOption& option,
                           const std::string& value);
    std::string ScreenTitle(const ShaderPackInstance& pack, const std::string& screen);
    std::string ProfileLabel(const ShaderPackInstance& pack, const std::string& profile);

    std::string StripFormatting(const std::string& text);
}
