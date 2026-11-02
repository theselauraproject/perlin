#pragma once
#include "client/javashaders/ProgramSet.hpp"

#include <map>
#include <string>
#include <vector>

// the option screens an optifine / iris pack describes in shaders.properties: screen / screen.<NAME> layouts,
// sliders, profiles and the lang file labels (option.<NAME>, value.<NAME>.<value>, prefix. / suffix.)
namespace perlin::jshader {
    struct ScreenEntry {
        enum class Kind { Option, Screen, Profile, Empty };
        Kind kind = Kind::Empty;
        std::string name; // option or sub screen name
    };

    struct ScreenLayout {
        std::string title;
        std::vector<ScreenEntry> entries;
        int columns = 2;
    };

    // root screen when name is empty; a pack without screens lists every option on the root
    ScreenLayout BuildScreen(const ShaderPackInstance& pack, const std::string& name);

    bool IsSlider(const ShaderPackInstance& pack, const std::string& option);

    // profile name -> the option values it sets, with profile.X includes resolved (in declaration order)
    std::vector<std::pair<std::string, OptionValues>> Profiles(const ShaderPackInstance& pack);
    // the profile whose values all match the current ones, empty for custom
    std::string MatchingProfile(const ShaderPackInstance& pack, const OptionValues& current);

    // value of an option now: override, else default ("true" / "false" for toggles)
    std::string OptionValue(const ShaderPackInstance& pack, const ShaderOption& option,
                            const OptionValues& current);
    const ShaderOption* FindOption(const ShaderPackInstance& pack, const std::string& name);

    std::string OptionLabel(const ShaderPackInstance& pack, const std::string& option);
    std::string OptionComment(const ShaderPackInstance& pack, const ShaderOption& option);
    std::string ValueLabel(const ShaderPackInstance& pack, const ShaderOption& option,
                           const std::string& value);
    std::string ScreenTitle(const ShaderPackInstance& pack, const std::string& screen);
    std::string ProfileLabel(const ShaderPackInstance& pack, const std::string& profile);

    // drops minecraft formatting codes (section sign + one character) from lang text
    std::string StripFormatting(const std::string& text);
}
