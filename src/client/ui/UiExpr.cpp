#include <cstring>
#include <cstdio>
#include "client/ui/UiExpr.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace perlin::ui {
    namespace {
        struct Lexer {
            std::string_view s;
            size_t pos = 0;

            void Skip() {
                while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
                    pos++;
            }
        };

        class Evaluator {
        public:
            Evaluator(std::string_view text, const ExprEnv& env) : lex{text, 0}, env(env) {}

            bool Run(json::Value& out) {
                json::Value v = Or();
                lex.Skip();
                if (failed || lex.pos < lex.s.size()) {
                    out = json::Value();
                    return false;
                }
                out = std::move(v);
                return true;
            }

        private:
            Lexer lex;
            const ExprEnv& env;
            bool failed = false;

            bool Match(std::string_view token) {
                lex.Skip();
                if (lex.s.compare(lex.pos, token.size(), token) == 0) {
                    lex.pos += token.size();
                    return true;
                }
                return false;
            }

            bool MatchWord(std::string_view word) {
                lex.Skip();
                if (lex.s.compare(lex.pos, word.size(), word) != 0)
                    return false;
                const size_t end = lex.pos + word.size();
                if (end < lex.s.size() &&
                    (std::isalnum(static_cast<unsigned char>(lex.s[end])) || lex.s[end] == '_'))
                    return false;
                lex.pos = end;
                return true;
            }

            static std::string Format(const std::string& fmt, const json::Value& value) {
                std::string out;
                for (size_t i = 0; i < fmt.size(); i++) {
                    if (fmt[i] != '%' || i + 1 >= fmt.size()) {
                        out += fmt[i];
                        continue;
                    }
                    if (fmt[i + 1] == '%') {
                        out += '%';
                        i++;
                        continue;
                    }
                    size_t j = i + 1;
                    while (j < fmt.size() && std::strchr("-+ #0123456789.", fmt[j]))
                        j++;
                    if (j >= fmt.size())
                        break;
                    const std::string spec = fmt.substr(i, j - i + 1);
                    const char conv = fmt[j];
                    char buf[512];
                    if (conv == 's') {
                        const std::string text = value.IsString() ? value.string
                                                 : value.IsNull() ? std::string()
                                                                  : value.Dump();
                        std::snprintf(buf, sizeof(buf), spec.c_str(), text.c_str());
                    } else if (conv == 'd' || conv == 'i') {
                        std::snprintf(buf, sizeof(buf), (spec.substr(0, spec.size() - 1) + "lld").c_str(),
                                      static_cast<long long>(AsNumber(value)));
                    } else if (conv == 'f' || conv == 'g' || conv == 'e') {
                        std::snprintf(buf, sizeof(buf), spec.c_str(), AsNumber(value));
                    } else {
                        buf[0] = 0;
                    }
                    out += buf;
                    i = j;
                }
                return out;
            }

            static bool Equal(const json::Value& a, const json::Value& b) {
                if (a.IsNumber() && b.IsNumber())
                    return a.number == b.number;
                if (a.IsBool() && b.IsBool())
                    return a.boolean == b.boolean;
                if (a.IsString() && b.IsString())
                    return a.string == b.string;
                if (a.IsNull() && b.IsNull())
                    return true;
                if (a.IsNull() || b.IsNull())
                    return false;
                return a.Dump() == b.Dump();
            }

            static double AsNumber(const json::Value& v) {
                if (v.IsNumber())
                    return v.number;
                if (v.IsBool())
                    return v.boolean ? 1.0 : 0.0;
                if (v.IsString())
                    return std::atof(v.string.c_str());
                return 0.0;
            }

            json::Value Or() {
                json::Value left = And();
                while (MatchWord("or") || Match("||")) {
                    json::Value right = And();
                    left = json::Value(Truthy(left) || Truthy(right));
                }
                return left;
            }

            json::Value And() {
                json::Value left = Compare();
                while (MatchWord("and") || Match("&&")) {
                    json::Value right = Compare();
                    left = json::Value(Truthy(left) && Truthy(right));
                }
                return left;
            }

            json::Value Compare() {
                json::Value left = Sum();
                for (;;) {
                    if (Match("=="))
                        left = json::Value(Equal(left, Sum()));
                    else if (Match("!="))
                        left = json::Value(!Equal(left, Sum()));
                    else if (Match(">="))
                        left = json::Value(AsNumber(left) >= AsNumber(Sum()));
                    else if (Match("<="))
                        left = json::Value(AsNumber(left) <= AsNumber(Sum()));
                    else if (Match(">"))
                        left = json::Value(AsNumber(left) > AsNumber(Sum()));
                    else if (Match("<"))
                        left = json::Value(AsNumber(left) < AsNumber(Sum()));
                    else if (lex.Skip(), lex.pos < lex.s.size() && lex.s[lex.pos] == '=' &&
                                             !(lex.pos + 1 < lex.s.size() && lex.s[lex.pos + 1] == '=')) {
                        lex.pos++;
                        left = json::Value(Equal(left, Sum()));
                    } else {
                        return left;
                    }
                }
            }

            json::Value Sum() {
                json::Value left = Product();
                for (;;) {
                    lex.Skip();
                    if (lex.pos < lex.s.size() && lex.s[lex.pos] == '+') {
                        lex.pos++;
                        json::Value right = Product();
                        if (left.IsString() || right.IsString())
                            left = json::Value((left.IsString() ? left.string : left.Dump()) +
                                        (right.IsString() ? right.string : right.Dump()));
                        else
                            left = json::Value(AsNumber(left) + AsNumber(right));
                    } else if (lex.pos < lex.s.size() && lex.s[lex.pos] == '-') {
                        lex.pos++;
                        json::Value right = Product();
                        if (left.IsString()) {

                            const std::string cut = right.IsString() ? right.string : right.Dump();
                            std::string out = left.string;
                            if (!cut.empty())
                                for (size_t at; (at = out.find(cut)) != std::string::npos;)
                                    out.erase(at, cut.size());
                            left = json::Value(std::move(out));
                        } else {
                            left = json::Value(AsNumber(left) - AsNumber(right));
                        }
                    } else {
                        return left;
                    }
                }
            }

            json::Value Product() {
                json::Value left = Unary();
                for (;;) {
                    lex.Skip();
                    if (lex.pos < lex.s.size() && lex.s[lex.pos] == '*') {
                        lex.pos++;
                        json::Value right = Unary();
                        if (left.IsString() && left.string.contains('%'))
                            left = json::Value(Format(left.string, right));
                        else
                            left = json::Value(AsNumber(left) * AsNumber(right));
                    } else if (lex.pos < lex.s.size() && lex.s[lex.pos] == '/') {
                        lex.pos++;
                        const double d = AsNumber(Unary());
                        left = json::Value(d == 0 ? 0.0 : AsNumber(left) / d);
                    } else if (lex.pos < lex.s.size() && lex.s[lex.pos] == '%') {
                        lex.pos++;
                        const double d = AsNumber(Unary());
                        left = json::Value(d == 0 ? 0.0 : std::fmod(AsNumber(left), d));
                    } else {
                        return left;
                    }
                }
            }

            json::Value Unary() {
                if (MatchWord("not") ||
                    (lex.Skip(), lex.pos < lex.s.size() && lex.s[lex.pos] == '!' &&
                                     !(lex.pos + 1 < lex.s.size() && lex.s[lex.pos + 1] == '=') &&
                                     (lex.pos++, true)))
                    return json::Value(!Truthy(Unary()));
                lex.Skip();
                if (lex.pos < lex.s.size() && lex.s[lex.pos] == '-') {
                    lex.pos++;
                    return json::Value(-AsNumber(Unary()));
                }
                return Primary();
            }

            json::Value Primary() {
                lex.Skip();
                if (lex.pos >= lex.s.size()) {
                    failed = true;
                    return {};
                }
                const char c = lex.s[lex.pos];
                if (c == '(') {
                    lex.pos++;
                    json::Value v = Or();
                    lex.Skip();
                    if (lex.pos < lex.s.size() && lex.s[lex.pos] == ')')
                        lex.pos++;
                    else
                        failed = true;
                    return v;
                }
                if (c == '\'' || c == '"') {
                    const char quote = c;
                    lex.pos++;
                    std::string text;
                    while (lex.pos < lex.s.size() && lex.s[lex.pos] != quote)
                        text += lex.s[lex.pos++];
                    if (lex.pos < lex.s.size())
                        lex.pos++;
                    else
                        failed = true;
                    return json::Value(std::move(text));
                }
                if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
                    const size_t start = lex.pos;
                    while (
                        lex.pos < lex.s.size() &&
                        (std::isdigit(static_cast<unsigned char>(lex.s[lex.pos])) || lex.s[lex.pos] == '.'))
                        lex.pos++;
                    return json::Value(std::atof(std::string(lex.s.substr(start, lex.pos - start)).c_str()));
                }
                if (c == '#' || c == '$') {
                    const size_t start = lex.pos++;
                    while (lex.pos < lex.s.size() &&
                           (std::isalnum(static_cast<unsigned char>(lex.s[lex.pos])) ||
                            lex.s[lex.pos] == '_' || lex.s[lex.pos] == '.'))
                        lex.pos++;
                    const std::string name(lex.s.substr(start, lex.pos - start));
                    json::Value value;
                    if (c == '#' && env.binding && env.binding(name, value))
                        return value;
                    if (c == '$' && env.variable && env.variable(name, value))
                        return value;
                    return {};
                }
                if (MatchWord("true"))
                    return json::Value(true);
                if (MatchWord("false"))
                    return json::Value(false);
                if (MatchWord("null"))
                    return json::Value();
                if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                    const size_t start = lex.pos;
                    while (lex.pos < lex.s.size() &&
                           (std::isalnum(static_cast<unsigned char>(lex.s[lex.pos])) ||
                            lex.s[lex.pos] == '_' || lex.s[lex.pos] == '.'))
                        lex.pos++;
                    return json::Value(std::string(lex.s.substr(start, lex.pos - start)));
                }
                failed = true;
                return {};
            }
        };
    }

    bool IsExpression(std::string_view text) {
        size_t a = 0, b = text.size();
        while (a < b && std::isspace(static_cast<unsigned char>(text[a])))
            a++;
        while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1])))
            b--;
        return b - a >= 2 && text[a] == '(' && text[b - 1] == ')';
    }

    bool UsesBindings(std::string_view text) {
        return text.contains('#');
    }

    bool Truthy(const json::Value& v) {
        switch (v.type) {
        case json::Value::Type::Bool:
            return v.boolean;
        case json::Value::Type::Number:
            return v.number != 0.0;

        case json::Value::Type::String:
            return !v.string.empty() && v.string != "false" && v.string != "0" && v.string[0] != '$';
        case json::Value::Type::Array:
            return !v.array.empty();
        case json::Value::Type::Object:
            return !v.object.empty();
        default:
            return false;
        }
    }

    bool EvalExpression(std::string_view text, const ExprEnv& env, json::Value& out) {

        std::string balanced;
        balanced.reserve(text.size());
        int depth = 0;
        bool quoted = false;
        for (const char c : text) {
            if (c == '\'')
                quoted = !quoted;
            else if (!quoted && c == '(')
                depth++;
            else if (!quoted && c == ')' && depth-- == 0) {
                depth = 0;
                continue;
            }
            balanced += c;
        }
        Evaluator evaluator(balanced, env);
        return evaluator.Run(out);
    }
}
