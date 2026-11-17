#pragma once
#include "util/JsonValue.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace perlin::ui {
    struct ExprEnv {
        std::function<bool(const std::string& name, json::Value& out)> binding;
        std::function<bool(const std::string& name, json::Value& out)> variable;
    };

    bool IsExpression(std::string_view text);
    bool EvalExpression(std::string_view text, const ExprEnv& env, json::Value& out);
    bool Truthy(const json::Value& value);
    bool UsesBindings(std::string_view text);
}
