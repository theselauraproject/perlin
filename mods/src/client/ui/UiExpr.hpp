#pragma once
#include "client/ui/Json.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace perlin::ui {
    struct ExprEnv {
        std::function<bool(const std::string& name, Json& out)> binding;
        std::function<bool(const std::string& name, Json& out)> variable;
    };

    bool IsExpression(std::string_view text);
    bool EvalExpression(std::string_view text, const ExprEnv& env, Json& out);
    bool Truthy(const Json& value);
    bool UsesBindings(std::string_view text);
}
