#pragma once
#include <map>
#include <string>

namespace perlin::jshader {
    using MacroMap = std::map<std::string, std::string>;

    long long EvalCondition(const std::string& expression, const MacroMap& macros);

    std::string ResolveConditionals(const std::string& text, MacroMap& macros);
}
