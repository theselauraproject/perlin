#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace perlin::server::json {

    // small tree reader for config files, plugin manifests and login tokens; missing keys read as null so chains never throw
    class Value {
    public:
        enum class Kind : uint8_t { Null, Bool, Number, String, Array, Object };

        Kind kind = Kind::Null;
        bool boolean = false;
        double number = 0;
        std::string text;
        std::vector<Value> items;
        std::map<std::string, Value, std::less<>> members;

        bool IsNull() const { return kind == Kind::Null; }
        bool IsObject() const { return kind == Kind::Object; }
        bool IsArray() const { return kind == Kind::Array; }
        bool IsString() const { return kind == Kind::String; }

        const Value& operator[](std::string_view key) const;
        const Value& operator[](size_t index) const;

        std::string String(std::string_view fallback = {}) const {
            return kind == Kind::String ? text : std::string(fallback);
        }
        double Number(double fallback = 0) const { return kind == Kind::Number ? number : fallback; }
        int64_t Int(int64_t fallback = 0) const {
            return kind == Kind::Number ? static_cast<int64_t>(number) : fallback;
        }
        bool Bool(bool fallback = false) const { return kind == Kind::Bool ? boolean : fallback; }
        size_t Size() const {
            return kind == Kind::Array ? items.size() : kind == Kind::Object ? members.size() : 0;
        }
    };

    bool Parse(std::string_view text, Value& out, std::string* error = nullptr);
    std::string Escape(std::string_view text);

    // jwt payload (middle segment) decoded from base64url; unsigned, callers decide what to trust
    bool DecodeJwtPayload(std::string_view jwt, Value& out);
    std::string Base64UrlDecode(std::string_view text);
}
