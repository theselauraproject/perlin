#include "core/Json.hpp"

#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace perlin::server::json {
    namespace {
        const Value kNull;

        struct Parser {
            std::string_view s;
            size_t i = 0;
            std::string error;

            void Skip() {
                while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
                    i++;
            }
            bool Fail(const char* what) {
                if (error.empty())
                    error = std::string(what) + " at offset " + std::to_string(i);
                return false;
            }
            bool Literal(const char* word, size_t n) {
                if (s.compare(i, n, word) != 0)
                    return false;
                i += n;
                return true;
            }
            static void Utf8(std::string& out, uint32_t cp) {
                if (cp < 0x80) {
                    out.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else if (cp < 0x10000) {
                    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
            }
            bool Hex4(uint32_t& out) {
                if (i + 4 > s.size())
                    return false;
                out = 0;
                for (int k = 0; k < 4; k++) {
                    const char c = s[i++];
                    out <<= 4;
                    if (c >= '0' && c <= '9')
                        out |= c - '0';
                    else if (c >= 'a' && c <= 'f')
                        out |= c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F')
                        out |= c - 'A' + 10;
                    else
                        return false;
                }
                return true;
            }
            bool Str(std::string& out) {
                if (i >= s.size() || s[i] != '"')
                    return Fail("expected string");
                i++;
                while (i < s.size()) {
                    char c = s[i++];
                    if (c == '"')
                        return true;
                    if (c != '\\') {
                        out.push_back(c);
                        continue;
                    }
                    if (i >= s.size())
                        break;
                    c = s[i++];
                    switch (c) {
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 'b':
                        out.push_back('\b');
                        break;
                    case 'f':
                        out.push_back('\f');
                        break;
                    case 'u': {
                        uint32_t cp;
                        if (!Hex4(cp))
                            return Fail("bad \\u escape");
                        // surrogate pair: combine, a lone one becomes U+FFFD
                        if (cp >= 0xD800 && cp < 0xDC00) {
                            uint32_t lo = 0;
                            if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                                i += 2;
                                if (!Hex4(lo))
                                    return Fail("bad \\u escape");
                            }
                            cp = lo >= 0xDC00 && lo < 0xE000 ? 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00)
                                                             : 0xFFFD;
                        }
                        Utf8(out, cp);
                        break;
                    }
                    default:
                        out.push_back(c);
                        break;
                    }
                }
                return Fail("unterminated string");
            }
            bool Val(Value& v, int depth) {
                if (depth > 64)
                    return Fail("nesting too deep");
                Skip();
                if (i >= s.size())
                    return Fail("unexpected end");
                const char c = s[i];
                if (c == '{') {
                    i++;
                    v.kind = Value::Kind::Object;
                    Skip();
                    if (i < s.size() && s[i] == '}') {
                        i++;
                        return true;
                    }
                    for (;;) {
                        Skip();
                        std::string key;
                        if (!Str(key))
                            return false;
                        Skip();
                        if (i >= s.size() || s[i++] != ':')
                            return Fail("expected ':'");
                        if (!Val(v.members[key], depth + 1))
                            return false;
                        Skip();
                        if (i < s.size() && s[i] == ',') {
                            i++;
                            continue;
                        }
                        if (i < s.size() && s[i] == '}') {
                            i++;
                            return true;
                        }
                        return Fail("expected ',' or '}'");
                    }
                }
                if (c == '[') {
                    i++;
                    v.kind = Value::Kind::Array;
                    Skip();
                    if (i < s.size() && s[i] == ']') {
                        i++;
                        return true;
                    }
                    for (;;) {
                        v.items.emplace_back();
                        if (!Val(v.items.back(), depth + 1))
                            return false;
                        Skip();
                        if (i < s.size() && s[i] == ',') {
                            i++;
                            continue;
                        }
                        if (i < s.size() && s[i] == ']') {
                            i++;
                            return true;
                        }
                        return Fail("expected ',' or ']'");
                    }
                }
                if (c == '"') {
                    v.kind = Value::Kind::String;
                    return Str(v.text);
                }
                if (Literal("true", 4)) {
                    v.kind = Value::Kind::Bool;
                    v.boolean = true;
                    return true;
                }
                if (Literal("false", 5)) {
                    v.kind = Value::Kind::Bool;
                    return true;
                }
                if (Literal("null", 4))
                    return true;
                const std::string num(s.substr(i, 32));
                char* end = nullptr;
                const double d = std::strtod(num.c_str(), &end);
                if (end == num.c_str())
                    return Fail("unexpected character");
                i += static_cast<size_t>(end - num.c_str());
                v.kind = Value::Kind::Number;
                v.number = d;
                return true;
            }
        };
    }

    const Value& Value::operator[](std::string_view key) const {
        if (kind != Kind::Object)
            return kNull;
        const auto it = members.find(key);
        return it == members.end() ? kNull : it->second;
    }

    const Value& Value::operator[](size_t index) const {
        return kind == Kind::Array && index < items.size() ? items[index] : kNull;
    }

    bool Parse(std::string_view text, Value& out, std::string* error) {
        Parser p;
        p.s = text;
        out = Value{};
        if (!p.Val(out, 0)) {
            if (error)
                *error = p.error;
            return false;
        }
        p.Skip();
        if (p.i != text.size()) {
            if (error)
                *error = "trailing characters at offset " + std::to_string(p.i);
            return false;
        }
        return true;
    }

    std::string Escape(std::string_view text) {
        std::string out;
        out.reserve(text.size() + 2);
        for (const unsigned char c : text) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
            }
        }
        return out;
    }

    std::string Base64UrlDecode(std::string_view text) {
        std::string out;
        uint32_t acc = 0;
        int bits = 0;
        for (const char c : text) {
            int v;
            if (c >= 'A' && c <= 'Z')
                v = c - 'A';
            else if (c >= 'a' && c <= 'z')
                v = c - 'a' + 26;
            else if (c >= '0' && c <= '9')
                v = c - '0' + 52;
            else if (c == '-' || c == '+')
                v = 62;
            else if (c == '_' || c == '/')
                v = 63;
            else
                continue;
            acc = (acc << 6) | static_cast<uint32_t>(v);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out.push_back(static_cast<char>((acc >> bits) & 0xFF));
            }
        }
        return out;
    }

    bool DecodeJwtPayload(std::string_view jwt, Value& out) {
        const size_t a = jwt.find('.');
        if (a == std::string_view::npos)
            return false;
        const size_t b = jwt.find('.', a + 1);
        const std::string_view mid =
            jwt.substr(a + 1, b == std::string_view::npos ? std::string_view::npos : b - a - 1);
        return Parse(Base64UrlDecode(mid), out);
    }
}
