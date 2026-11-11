#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace perlin::json {
    class Value {
    public:
        enum class Kind : uint8_t { Null, Bool, Number, String, Array, Object };

        Kind kind() const { return type; }
        bool is_null() const { return type == Kind::Null; }
        bool is_bool() const { return type == Kind::Bool; }
        bool is_number() const { return type == Kind::Number; }
        bool is_string() const { return type == Kind::String; }
        bool is_array() const { return type == Kind::Array; }
        bool is_object() const { return type == Kind::Object; }

        bool boolean(bool fallback = false) const {
            if (type == Kind::Bool)
                return flag;
            if (type == Kind::Number)
                return number_value != 0.0;
            return fallback;
        }
        double number(double fallback = 0.0) const {
            return type == Kind::Number ? number_value : type == Kind::Bool ? (flag ? 1.0 : 0.0) : fallback;
        }
        int64_t integer(int64_t fallback = 0) const {
            return type == Kind::Number ? static_cast<int64_t>(number_value)
                                        : static_cast<int64_t>(number(static_cast<double>(fallback)));
        }
        std::string_view string(std::string_view fallback = {}) const {
            return type == Kind::String ? std::string_view(text) : fallback;
        }

        const std::vector<Value>& array() const { return items; }
        size_t size() const { return items.size(); }
        const std::vector<std::string>& keys() const { return names; }

        const Value& operator[](std::string_view key) const {
            if (type == Kind::Object)
                for (size_t i = 0; i < names.size(); i++)
                    if (names[i] == key)
                        return items[i];
            return null_value();
        }
        const Value& operator[](size_t index) const {
            return type == Kind::Array && index < items.size() ? items[index] : null_value();
        }
        bool contains(std::string_view key) const {
            if (type == Kind::Object)
                for (const std::string& n : names)
                    if (n == key)
                        return true;
            return false;
        }

    private:
        friend class Parser;
        static const Value& null_value() {
            static const Value value;
            return value;
        }
        Kind type = Kind::Null;
        bool flag = false;
        double number_value = 0.0;
        std::string text;
        std::vector<Value> items;
        std::vector<std::string> names;
    };

    class Parser {
    public:
        explicit Parser(std::string_view source) : src(source) {}

        std::optional<Value> run() {
            Value v;
            if (!value(v, 0))
                return std::nullopt;
            skip();
            return pos == src.size() ? std::optional<Value>(std::move(v)) : std::nullopt;
        }

    private:
        std::string_view src;
        size_t pos = 0;

        void skip() {
            while (pos < src.size() &&
                   (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\n' || src[pos] == '\r'))
                pos++;
        }
        bool eat(char c) {
            skip();
            if (pos < src.size() && src[pos] == c) {
                pos++;
                return true;
            }
            return false;
        }
        bool literal(std::string_view word) {
            if (src.substr(pos, word.size()) != word)
                return false;
            pos += word.size();
            return true;
        }

        bool value(Value& out, int depth) {
            if (depth > 64)
                return false;
            skip();
            if (pos >= src.size())
                return false;
            const char c = src[pos];
            if (c == '{')
                return object(out, depth);
            if (c == '[')
                return array(out, depth);
            if (c == '"') {
                out.type = Value::Kind::String;
                return string(out.text);
            }
            if (literal("true")) {
                out.type = Value::Kind::Bool;
                out.flag = true;
                return true;
            }
            if (literal("false")) {
                out.type = Value::Kind::Bool;
                return true;
            }
            if (literal("null"))
                return true;
            return number(out);
        }

        bool number(Value& out) {
            const size_t start = pos;
            while (pos < src.size() &&
                   (src[pos] == '-' || src[pos] == '+' || src[pos] == '.' || src[pos] == 'e' ||
                    src[pos] == 'E' || (src[pos] >= '0' && src[pos] <= '9')))
                pos++;
            if (pos == start || pos - start > 40)
                return false;
            const std::string copy(src.substr(start, pos - start));
            char* end = nullptr;
            out.number_value = std::strtod(copy.c_str(), &end);
            out.type = Value::Kind::Number;
            return end == copy.c_str() + copy.size();
        }

        static void utf8(std::string& out, uint32_t cp) {
            if (cp < 0x80) {
                out += static_cast<char>(cp);
            } else if (cp < 0x800) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        bool hex4(uint32_t& out) {
            if (pos + 4 > src.size())
                return false;
            out = 0;
            for (int i = 0; i < 4; i++) {
                const char c = src[pos++];
                out <<= 4;
                if (c >= '0' && c <= '9')
                    out |= static_cast<uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f')
                    out |= static_cast<uint32_t>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F')
                    out |= static_cast<uint32_t>(c - 'A' + 10);
                else
                    return false;
            }
            return true;
        }

        bool string(std::string& out) {
            if (!eat('"'))
                return false;
            while (pos < src.size()) {
                const char c = src[pos++];
                if (c == '"')
                    return true;
                if (c != '\\') {
                    out += c;
                    continue;
                }
                if (pos >= src.size())
                    return false;
                switch (const char e = src[pos++]) {
                case 'n':
                    out += '\n';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!hex4(cp))
                        return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && src.substr(pos, 2) == "\\u") {
                        pos += 2;
                        uint32_t low = 0;
                        if (!hex4(low))
                            return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    utf8(out, cp);
                    break;
                }
                default:
                    out += e;
                    break;
                }
            }
            return false;
        }

        bool array(Value& out, int depth) {
            eat('[');
            out.type = Value::Kind::Array;
            if (eat(']'))
                return true;
            do {
                Value element;
                if (!value(element, depth + 1))
                    return false;
                out.items.push_back(std::move(element));
                if (out.items.size() > 100000)
                    return false;
            } while (eat(','));
            return eat(']');
        }

        bool object(Value& out, int depth) {
            eat('{');
            out.type = Value::Kind::Object;
            if (eat('}'))
                return true;
            do {
                skip();
                std::string key;
                if (!string(key) || !eat(':'))
                    return false;
                Value member;
                if (!value(member, depth + 1))
                    return false;
                out.names.push_back(std::move(key));
                out.items.push_back(std::move(member));
                if (out.items.size() > 100000)
                    return false;
            } while (eat(','));
            return eat('}');
        }
    };

    inline std::optional<Value> parse(std::string_view text) {
        return Parser(text).run();
    }
}
