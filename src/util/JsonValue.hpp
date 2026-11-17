#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace perlin::json {
    class Value {
    public:
        enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

        Value() = default;
        explicit Value(bool v) : type(Type::Bool), boolean(v) {}
        explicit Value(double v) : type(Type::Number), number(v) {}
        explicit Value(int v) : type(Type::Number), number(v) {}
        explicit Value(std::string v) : type(Type::String), string(std::move(v)) {}
        explicit Value(const char* v) : type(Type::String), string(v) {}
        static Value MakeArray() {
            Value j;
            j.type = Type::Array;
            return j;
        }
        static Value MakeObject() {
            Value j;
            j.type = Type::Object;
            return j;
        }

        Type type = Type::Null;
        bool boolean = false;
        double number = 0;
        std::string string;
        std::vector<Value> array;
        std::vector<std::pair<std::string, Value>> object;

        bool IsNull() const { return type == Type::Null; }
        bool IsBool() const { return type == Type::Bool; }
        bool IsNumber() const { return type == Type::Number; }
        bool IsString() const { return type == Type::String; }
        bool IsArray() const { return type == Type::Array; }
        bool IsObject() const { return type == Type::Object; }

        const Value* Find(std::string_view key) const;
        Value* Find(std::string_view key);
        bool Has(std::string_view key) const { return Find(key) != nullptr; }
        Value& Set(std::string_view key, Value value);
        bool Erase(std::string_view key);
        void Push(Value value) { array.push_back(std::move(value)); }

        double NumberOr(double fallback) const { return type == Type::Number ? number : fallback; }
        bool BoolOr(bool fallback) const { return type == Type::Bool ? boolean : fallback; }
        std::string StringOr(const std::string& fallback) const {
            return type == Type::String ? string : fallback;
        }

        static bool Parse(std::string_view text, Value& out, std::string& error);
        std::string Dump(int indent = -1) const;
    };
}
