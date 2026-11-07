#pragma once
#include "client/javashaders/PackSource.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace perlin::jshader {
    struct ShaderOption {
        enum class Kind { Toggle, Value, Const };
        std::string name;
        Kind kind = Kind::Toggle;
        bool defaultOn = false;
        std::string defaultValue;
        std::vector<std::string> values;
        std::string comment;
        std::string file;
        std::string type;
    };

    using OptionValues = std::map<std::string, std::string>;

    struct LineOrigin {
        std::string file;
        int line = 0;
    };

    struct ExpandedSource {
        std::string text;
        std::vector<LineOrigin> origins;
        std::set<std::string> files;
        bool ok = true;
        std::string error;
    };

    struct IncludeContext {
        const PackSource* pack = nullptr;
        const OptionValues* overrides = nullptr;
        std::vector<ShaderOption>* options = nullptr;
        std::set<std::string> seenOptionKeys;
    };

    ExpandedSource ExpandIncludes(IncludeContext& ctx, const std::string& path);
    void ScanOptionsInText(IncludeContext& ctx, const std::string& path, const std::string& text);
    ExpandedSource ExpandIncludesText(IncludeContext& ctx, const std::string& path, const std::string& text);
}
