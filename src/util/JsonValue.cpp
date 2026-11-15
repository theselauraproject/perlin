#include "util/JsonValue.hpp"
#include "util/Utf8.hpp"
#include "util/JsonText.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace perlin::json {
    const Value* Value::Find(std::string_view key) const {
        if (type != Type::Object)
            return nullptr;
        for (size_t i = object.size(); i-- > 0;)
            if (object[i].first == key)
                return &object[i].second;
        return nullptr;
    }

    Value* Value::Find(std::string_view key) {
        return const_cast<Value*>(static_cast<const Value*>(this)->Find(key));
    }

    Value& Value::Set(std::string_view key, Value value) {
        if (type != Type::Object) {
            type = Type::Object;
            object.clear();
        }
        for (auto& member : object)
            if (member.first == key) {
                member.second = std::move(value);
                return member.second;
            }
        object.emplace_back(std::string(key), std::move(value));
        return object.back().second;
    }

    bool Value::Erase(std::string_view key) {
        if (type != Type::Object)
            return false;
        for (size_t i = 0; i < object.size(); i++)
            if (object[i].first == key) {
                object.erase(object.begin() + static_cast<long>(i));
                return true;
            }
        return false;
    }

    namespace {
        struct Parser {
            std::string_view text;
            size_t pos = 0;
            std::string error;
            int depth = 0;

            bool Fail(const char* message) {
                if (error.empty()) {
                    size_t line = 1, column = 1;
                    for (size_t i = 0; i < pos && i < text.size(); i++) {
                        if (text[i] == '\n') {
                            line++;
                            column = 1;
                        } else
                            column++;
                    }
                    error = std::string(message) + " at line " + std::to_string(line) + " column " +
                            std::to_string(column);
                }
                return false;
            }

            void SkipSpace() {
                while (pos < text.size()) {
                    const char c = text[pos];
                    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
                        pos++;
                    } else if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '/') {
                        while (pos < text.size() && text[pos] != '\n')
                            pos++;
                    } else if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '*') {
                        const size_t end = text.find("*/", pos + 2);
                        pos = end == std::string_view::npos ? text.size() : end + 2;
                    } else {
                        break;
                    }
                }
            }
            bool ParseString(std::string& out) {
                if (pos >= text.size() || text[pos] != '"')
                    return Fail("expected a string");
                pos++;
                out.clear();
                while (pos < text.size()) {
                    const char c = text[pos++];
                    if (c == '"')
                        return true;
                    if (c != '\\') {
                        out += c;
                        continue;
                    }
                    if (pos >= text.size())
                        break;
                    const char e = text[pos++];
                    switch (e) {
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
                        auto hex4 = [&](uint32_t& value) {
                            if (pos + 4 > text.size())
                                return false;
                            value = 0;
                            for (int i = 0; i < 4; i++) {
                                const char h = text[pos + i];
                                value <<= 4;
                                if (h >= '0' && h <= '9')
                                    value |= h - '0';
                                else if (h >= 'a' && h <= 'f')
                                    value |= h - 'a' + 10;
                                else if (h >= 'A' && h <= 'F')
                                    value |= h - 'A' + 10;
                                else
                                    return false;
                            }
                            pos += 4;
                            return true;
                        };
                        uint32_t cp = 0;
                        if (!hex4(cp))
                            return Fail("bad \\u escape");
                        if (cp >= 0xD800 && cp < 0xDC00 && pos + 1 < text.size() && text[pos] == '\\' &&
                            text[pos + 1] == 'u') {
                            pos += 2;
                            uint32_t low = 0;
                            if (!hex4(low))
                                return Fail("bad \\u escape");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default:
                        out += e;
                        break;
                    }
                }
                return Fail("unterminated string");
            }

            bool ParseValue(Value& out) {
                SkipSpace();
                if (pos >= text.size())
                    return Fail("unexpected end of input");
                if (++depth > 256)
                    return Fail("nesting too deep");
                const bool ok = ParseValueInner(out);
                depth--;
                return ok;
            }

            bool ParseValueInner(Value& out) {
                const char c = text[pos];
                if (c == '{') {
                    pos++;
                    out = Value::MakeObject();
                    for (;;) {
                        SkipSpace();
                        if (pos >= text.size())
                            return Fail("unterminated object");
                        if (text[pos] == '}') {
                            pos++;
                            return true;
                        }
                        if (text[pos] == ',') {
                            pos++;
                            continue;
                        }
                        std::string key;
                        if (!ParseString(key))
                            return false;
                        SkipSpace();
                        if (pos >= text.size() || text[pos] != ':')
                            return Fail("expected ':'");
                        pos++;
                        Value value;
                        if (!ParseValue(value))
                            return false;
                        out.object.emplace_back(std::move(key), std::move(value));
                    }
                }
                if (c == '[') {
                    pos++;
                    out = Value::MakeArray();
                    for (;;) {
                        SkipSpace();
                        if (pos >= text.size())
                            return Fail("unterminated array");
                        if (text[pos] == ']') {
                            pos++;
                            return true;
                        }
                        if (text[pos] == ',') {
                            pos++;
                            continue;
                        }
                        Value value;
                        if (!ParseValue(value))
                            return false;
                        out.array.push_back(std::move(value));
                    }
                }
                if (c == '"') {
                    out.type = Value::Type::String;
                    return ParseString(out.string);
                }
                if (text.compare(pos, 4, "true") == 0) {
                    pos += 4;
                    out = Value(true);
                    return true;
                }
                if (text.compare(pos, 5, "false") == 0) {
                    pos += 5;
                    out = Value(false);
                    return true;
                }
                if (text.compare(pos, 4, "null") == 0) {
                    pos += 4;
                    out = Value();
                    return true;
                }
                const size_t start = pos;
                if (text[pos] == '-' || text[pos] == '+')
                    pos++;
                while (pos < text.size() &&
                       (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.' ||
                        text[pos] == 'e' || text[pos] == 'E' || text[pos] == '-' || text[pos] == '+'))
                    pos++;
                if (pos == start)
                    return Fail("unexpected character");
                const std::string number(text.substr(start, pos - start));
                char* end = nullptr;
                const double v = std::strtod(number.c_str(), &end);
                if (end == number.c_str())
                    return Fail("bad number");
                out = Value(v);
                return true;
            }
        };

        void DumpString(std::string& out, const std::string& s) {
            out += '"';
            AppendEscaped(out, s);
            out += '"';
        }

        void DumpValue(std::string& out, const Value& j, int indent, int level) {
            const auto newline = [&](int l) {
                if (indent >= 0) {
                    out += '\n';
                    out.append(static_cast<size_t>(l * indent), ' ');
                }
            };
            switch (j.type) {
            case Value::Type::Null:
                out += "null";
                break;
            case Value::Type::Bool:
                out += j.boolean ? "true" : "false";
                break;
            case Value::Type::Number: {
                char buffer[64];
                if (j.number == std::floor(j.number) && std::fabs(j.number) < 1e15)
                    std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(j.number));
                else
                    std::snprintf(buffer, sizeof(buffer), "%.9g", j.number);
                out += buffer;
                break;
            }
            case Value::Type::String:
                DumpString(out, j.string);
                break;
            case Value::Type::Array:
                out += '[';
                for (size_t i = 0; i < j.array.size(); i++) {
                    if (i)
                        out += ',';
                    newline(level + 1);
                    DumpValue(out, j.array[i], indent, level + 1);
                }
                if (!j.array.empty())
                    newline(level);
                out += ']';
                break;
            case Value::Type::Object:
                out += '{';
                for (size_t i = 0; i < j.object.size(); i++) {
                    if (i)
                        out += ',';
                    newline(level + 1);
                    DumpString(out, j.object[i].first);
                    out += indent >= 0 ? ": " : ":";
                    DumpValue(out, j.object[i].second, indent, level + 1);
                }
                if (!j.object.empty())
                    newline(level);
                out += '}';
                break;
            }
        }
    }

    namespace {
        std::string Relax(std::string_view in) {
            std::string out;
            out.reserve(in.size());
            bool inString = false;
            for (size_t i = 0; i < in.size(); i++) {
                const char c = in[i];
                if (inString) {
                    out += c;
                    if (c == '\\' && i + 1 < in.size())
                        out += in[++i];
                    else if (c == '"')
                        inString = false;
                    continue;
                }
                if (c == '"') {
                    inString = true;
                    out += c;
                } else if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
                    while (i < in.size() && in[i] != '\n')
                        i++;
                    out += '\n';
                } else if (c == '/' && i + 1 < in.size() && in[i + 1] == '*') {
                    i += 2;
                    while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/'))
                        i++;
                    i++;
                } else if (c == ',') {
                    size_t j = i + 1;
                    while (j < in.size() && (in[j] == ' ' || in[j] == '\t' || in[j] == '\r' || in[j] == '\n'))
                        j++;
                    if (j < in.size() && (in[j] == '}' || in[j] == ']'))
                        continue;
                    out += c;
                } else {
                    out += c;
                }
            }
            return out;
        }
    }

    bool Value::Parse(std::string_view text, Value& out, std::string& error) {
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
            text.remove_prefix(3);
        Parser parser;
        parser.text = text;
        Value value;
        if (!parser.ParseValue(value)) {
            const std::string relaxed = Relax(text);
            Parser retry;
            retry.text = relaxed;
            Value again;
            if (!retry.ParseValue(again)) {
                error = parser.error;
                return false;
            }
            out = std::move(again);
            return true;
        }
        out = std::move(value);
        return true;
    }

    std::string Value::Dump(int indent) const {
        std::string out;
        DumpValue(out, *this, indent, 0);
        return out;
    }
}
