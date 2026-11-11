#include "client/javashaders/Cond.hpp"
#include "util/Strings.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace perlin::jshader {
    namespace {
        struct Parser {
            const std::string& s;
            const MacroMap& macros;
            int depth;
            size_t i = 0;

            Parser(const std::string& s, const MacroMap& macros, int depth)
                : s(s), macros(macros), depth(depth) {}

            void Skip() {
                while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
                    i++;
            }
            bool Eat(const char* op) {
                Skip();
                size_t n = std::char_traits<char>::length(op);
                if (s.compare(i, n, op) == 0) {
                    i += n;
                    return true;
                }
                return false;
            }
            bool Peek(const char* op) {
                Skip();
                return s.compare(i, std::char_traits<char>::length(op), op) == 0;
            }

            long long Ternary() {
                long long c = LogicOr();
                if (Eat("?")) {
                    long long a = Ternary();
                    Eat(":");
                    long long b = Ternary();
                    return c ? a : b;
                }
                return c;
            }
            long long LogicOr() {
                long long v = LogicAnd();
                while (Peek("||")) {
                    Eat("||");
                    long long r = LogicAnd();
                    v = (v || r) ? 1 : 0;
                }
                return v;
            }
            long long LogicAnd() {
                long long v = BitOr();
                while (Peek("&&")) {
                    Eat("&&");
                    long long r = BitOr();
                    v = (v && r) ? 1 : 0;
                }
                return v;
            }
            long long BitOr() {
                long long v = BitXor();
                while (Peek("|") && !Peek("||")) {
                    Eat("|");
                    v |= BitXor();
                }
                return v;
            }
            long long BitXor() {
                long long v = BitAnd();
                while (Peek("^")) {
                    Eat("^");
                    v ^= BitAnd();
                }
                return v;
            }
            long long BitAnd() {
                long long v = Equality();
                while (Peek("&") && !Peek("&&")) {
                    Eat("&");
                    v &= Equality();
                }
                return v;
            }
            long long Equality() {
                long long v = Relational();
                for (;;) {
                    if (Eat("=="))
                        v = (v == Relational());
                    else if (Eat("!="))
                        v = (v != Relational());
                    else
                        break;
                }
                return v;
            }
            long long Relational() {
                long long v = Shift();
                for (;;) {
                    if (Eat("<="))
                        v = (v <= Shift());
                    else if (Eat(">="))
                        v = (v >= Shift());
                    else if (Peek("<") && !Peek("<<")) {
                        Eat("<");
                        v = (v < Shift());
                    } else if (Peek(">") && !Peek(">>")) {
                        Eat(">");
                        v = (v > Shift());
                    } else
                        break;
                }
                return v;
            }
            long long Shift() {
                long long v = Additive();
                for (;;) {
                    if (Eat("<<"))
                        v <<= Additive();
                    else if (Eat(">>"))
                        v >>= Additive();
                    else
                        break;
                }
                return v;
            }
            long long Additive() {
                long long v = Multiplicative();
                for (;;) {
                    if (Eat("+"))
                        v += Multiplicative();
                    else if (Eat("-"))
                        v -= Multiplicative();
                    else
                        break;
                }
                return v;
            }
            long long Multiplicative() {
                long long v = Unary();
                for (;;) {
                    if (Eat("*"))
                        v *= Unary();
                    else if (Eat("/")) {
                        long long d = Unary();
                        v = d ? v / d : 0;
                    } else if (Eat("%")) {
                        long long d = Unary();
                        v = d ? v % d : 0;
                    } else
                        break;
                }
                return v;
            }
            long long Unary() {
                if (Eat("!"))
                    return !Unary();
                if (Eat("~"))
                    return ~Unary();
                if (Eat("-"))
                    return -Unary();
                if (Eat("+"))
                    return Unary();
                return Primary();
            }
            long long Primary() {
                Skip();
                if (Eat("(")) {
                    long long v = Ternary();
                    Eat(")");
                    return v;
                }
                if (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                    size_t j = i;
                    bool isFloat = false;
                    if (s[j] == '0' && j + 1 < s.size() && (s[j + 1] == 'x' || s[j + 1] == 'X')) {
                        j += 2;
                        while (j < s.size() && std::isxdigit(static_cast<unsigned char>(s[j])))
                            j++;
                    } else {
                        while (j < s.size() &&
                               (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.')) {
                            if (s[j] == '.')
                                isFloat = true;
                            j++;
                        }
                    }
                    const std::string num = s.substr(i, j - i);
                    i = j;
                    while (i < s.size() && (s[i] == 'u' || s[i] == 'U' || s[i] == 'l' || s[i] == 'L' ||
                                            s[i] == 'f' || s[i] == 'F'))
                        i++;
                    return isFloat ? static_cast<long long>(std::atof(num.c_str()))
                                   : std::strtoll(num.c_str(), nullptr, 0);
                }
                if (i < s.size() && (std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
                    size_t j = i;
                    while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_'))
                        j++;
                    const std::string name = s.substr(i, j - i);
                    i = j;
                    if (name == "defined") {
                        const bool paren = Eat("(");
                        Skip();
                        size_t k = i;
                        while (k < s.size() &&
                               (std::isalnum(static_cast<unsigned char>(s[k])) || s[k] == '_'))
                            k++;
                        const std::string target = s.substr(i, k - i);
                        i = k;
                        if (paren)
                            Eat(")");
                        return macros.contains(target) ? 1 : 0;
                    }
                    if (name == "true")
                        return 1;
                    if (name == "false")
                        return 0;
                    const auto it = macros.find(name);
                    if (it == macros.end())
                        return 0;
                    if (it->second.empty())
                        return 1;
                    if (depth > 24)
                        return 0;
                    Parser inner(it->second, macros, depth + 1);
                    return inner.Ternary();
                }
                i++;
                return 0;
            }
        };

        std::string StripComments(const std::string& line) {
            std::string out;
            for (size_t i = 0; i < line.size(); i++) {
                if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/')
                    break;
                if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '*') {
                    const size_t end = line.find("*/", i + 2);
                    if (end == std::string::npos)
                        break;
                    i = end + 1;
                    out += ' ';
                    continue;
                }
                out += line[i];
            }
            return out;
        }

    }

    long long EvalCondition(const std::string& expression, const MacroMap& macros) {
        Parser parser(expression, macros, 0);
        return parser.Ternary();
    }

    std::string ResolveConditionals(const std::string& text, MacroMap& macros) {
        struct Frame {
            bool parentActive;
            bool taken;
            bool active;
        };
        std::vector<Frame> stack;
        std::string out;
        out.reserve(text.size());
        bool active = true;
        std::istringstream in(text);
        std::string raw;
        std::string pending;
        bool continuing = false;
        while (std::getline(in, raw)) {
            if (!raw.empty() && raw.back() == '\r')
                raw.pop_back();
            std::string line = raw;
            if (continuing) {
                line = pending + raw;
                continuing = false;
                pending.clear();
            }
            const std::string trimmed = str::Trim(line);
            if (!trimmed.empty() && trimmed[0] == '#') {
                if (!trimmed.empty() && trimmed.back() == '\\' && !trimmed.contains("define")) {}
                std::string body = str::Trim(StripComments(trimmed.substr(1)));
                size_t k = 0;
                while (k < body.size() && std::isalpha(static_cast<unsigned char>(body[k])))
                    k++;
                const std::string dir = body.substr(0, k);
                const std::string rest = str::Trim(body.substr(k));
                if (dir == "if" || dir == "ifdef" || dir == "ifndef") {
                    bool cond = false;
                    if (active) {
                        if (dir == "if")
                            cond = EvalCondition(rest, macros) != 0;
                        else if (dir == "ifdef")
                            cond = macros.contains(rest.substr(0, rest.find_first_of(" \t")));
                        else
                            cond = !macros.contains(rest.substr(0, rest.find_first_of(" \t")));
                    }
                    stack.push_back({active, cond, active && cond});
                    active = stack.back().active;
                    out += '\n';
                    continue;
                }
                if (dir == "elif" && !stack.empty()) {
                    Frame& f = stack.back();
                    bool cond = false;
                    if (f.parentActive && !f.taken)
                        cond = EvalCondition(rest, macros) != 0;
                    f.active = f.parentActive && !f.taken && cond;
                    if (cond)
                        f.taken = true;
                    active = f.active;
                    out += '\n';
                    continue;
                }
                if (dir == "else" && !stack.empty()) {
                    Frame& f = stack.back();
                    f.active = f.parentActive && !f.taken;
                    f.taken = true;
                    active = f.active;
                    out += '\n';
                    continue;
                }
                if (dir == "endif" && !stack.empty()) {
                    active = stack.back().parentActive;
                    stack.pop_back();
                    out += '\n';
                    continue;
                }
                if (dir == "define" && active) {
                    size_t n = 0;
                    while (n < rest.size() &&
                           (std::isalnum(static_cast<unsigned char>(rest[n])) || rest[n] == '_'))
                        n++;
                    const std::string name = rest.substr(0, n);
                    if (!name.empty() && !(n < rest.size() && rest[n] == '('))
                        macros[name] = str::Trim(rest.substr(n));
                    out += '\n';
                    continue;
                }
                if (dir == "undef" && active) {
                    macros.erase(rest);
                    out += '\n';
                    continue;
                }
            }
            if (active) {
                out += line;
            }
            out += '\n';
        }
        return out;
    }
}
