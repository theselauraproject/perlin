#include "client/javashaders/UniformTable.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>

namespace perlin::jshader {
    namespace {
        void CopyMat(const float* in, float* out) {
            std::memcpy(out, in, 16 * sizeof(float));
        }

        void Invert(const float* m, float* out) {
            double a[16], inv[16];
            for (int i = 0; i < 16; i++)
                a[i] = m[i];
            inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
                     a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
            inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] -
                     a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
            inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
                     a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
            inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] -
                      a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
            inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] -
                     a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
            inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
                     a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
            inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
                     a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
            inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
                      a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
            inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
                     a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
            inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
                     a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
            inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
                      a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
            inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
                      a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
            inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
                     a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
            inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
                     a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
            inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
                      a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
            inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
                      a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
            double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
            if (std::fabs(det) < 1e-30) {
                static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                CopyMat(identity, out);
                return;
            }
            det = 1.0 / det;
            for (int i = 0; i < 16; i++)
                out[i] = static_cast<float>(inv[i] * det);
        }

        double Fract(double v) {
            return v - std::floor(v);
        }

        struct Value {
            double v[4] = {0, 0, 0, 0};
            int n = 1;
        };

        struct Node {
            enum class Kind { Number, Variable, Unary, Binary, Call } kind = Kind::Number;
            double number = 0;
            std::string text;
            std::vector<int> children;
        };

        struct Lexer {
            enum class T { End, Number, Ident, Op } type = T::End;
            std::string text;
            double number = 0;
        };

        class Parser {
        public:
            explicit Parser(const std::string& s) : src(s) { Next(); }
            std::vector<Node> pool;
            int Parse() {
                const int root = Expr();
                if (cur.type != Lexer::T::End)
                    error = "unexpected '" + cur.text + "'";
                return root;
            }
            std::string error;

        private:
            const std::string& src;
            size_t pos = 0;
            Lexer cur;

            int Make(Node::Kind k, std::string text = {}, std::vector<int> kids = {}, double number = 0) {
                Node n;
                n.kind = k;
                n.text = std::move(text);
                n.children = std::move(kids);
                n.number = number;
                pool.push_back(std::move(n));
                return static_cast<int>(pool.size()) - 1;
            }

            void Next() {
                while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])))
                    pos++;
                cur = {};
                if (pos >= src.size())
                    return;
                const char c = src[pos];
                if (std::isdigit(static_cast<unsigned char>(c)) ||
                    (c == '.' && pos + 1 < src.size() &&
                     std::isdigit(static_cast<unsigned char>(src[pos + 1])))) {
                    size_t e = pos;
                    while (e < src.size() &&
                           (std::isdigit(static_cast<unsigned char>(src[e])) || src[e] == '.'))
                        e++;
                    if (e < src.size() && (src[e] == 'e' || src[e] == 'E')) {
                        size_t f = e + 1;
                        if (f < src.size() && (src[f] == '+' || src[f] == '-'))
                            f++;
                        if (f < src.size() && std::isdigit(static_cast<unsigned char>(src[f]))) {
                            while (f < src.size() && std::isdigit(static_cast<unsigned char>(src[f])))
                                f++;
                            e = f;
                        }
                    }
                    cur.type = Lexer::T::Number;
                    cur.text = src.substr(pos, e - pos);
                    cur.number = std::atof(cur.text.c_str());
                    if (e < src.size() && (src[e] == 'f' || src[e] == 'F'))
                        e++;
                    pos = e;
                    return;
                }
                if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                    size_t e = pos;
                    while (e < src.size() && (std::isalnum(static_cast<unsigned char>(src[e])) ||
                                              src[e] == '_' || src[e] == '.'))
                        e++;
                    cur.type = Lexer::T::Ident;
                    cur.text = src.substr(pos, e - pos);
                    pos = e;
                    return;
                }
                cur.type = Lexer::T::Op;
                static const char* two[] = {"&&", "||", "==", "!=", "<=", ">="};
                for (const char* t : two)
                    if (src.compare(pos, 2, t) == 0) {
                        cur.text = t;
                        pos += 2;
                        return;
                    }
                cur.text = std::string(1, c);
                pos++;
            }

            bool IsOp(const char* o) const { return cur.type == Lexer::T::Op && cur.text == o; }

            int Expr() { return Or(); }
            int Or() {
                int l = And();
                while (IsOp("||")) {
                    Next();
                    int r = And();
                    l = Make(Node::Kind::Binary, "||", {l, r});
                }
                return l;
            }
            int And() {
                int l = Cmp();
                while (IsOp("&&")) {
                    Next();
                    int r = Cmp();
                    l = Make(Node::Kind::Binary, "&&", {l, r});
                }
                return l;
            }
            int Cmp() {
                int l = Add();
                while (IsOp("==") || IsOp("!=") || IsOp("<") || IsOp(">") || IsOp("<=") || IsOp(">=")) {
                    std::string op = cur.text;
                    Next();
                    int r = Add();
                    l = Make(Node::Kind::Binary, op, {l, r});
                }
                return l;
            }
            int Add() {
                int l = Mul();
                while (IsOp("+") || IsOp("-")) {
                    std::string op = cur.text;
                    Next();
                    int r = Mul();
                    l = Make(Node::Kind::Binary, op, {l, r});
                }
                return l;
            }
            int Mul() {
                int l = Unary();
                while (IsOp("*") || IsOp("/") || IsOp("%")) {
                    std::string op = cur.text;
                    Next();
                    int r = Unary();
                    l = Make(Node::Kind::Binary, op, {l, r});
                }
                return l;
            }
            int Unary() {
                if (IsOp("-")) {
                    Next();
                    int c = Unary();
                    return Make(Node::Kind::Unary, "-", {c});
                }
                if (IsOp("+")) {
                    Next();
                    return Unary();
                }
                if (IsOp("!")) {
                    Next();
                    int c = Unary();
                    return Make(Node::Kind::Unary, "!", {c});
                }
                return Primary();
            }
            int Primary() {
                if (cur.type == Lexer::T::Number) {
                    const double n = cur.number;
                    Next();
                    return Make(Node::Kind::Number, {}, {}, n);
                }
                if (cur.type == Lexer::T::Ident) {
                    std::string name = cur.text;
                    Next();
                    if (IsOp("(")) {
                        Next();
                        std::vector<int> args;
                        if (!IsOp(")")) {
                            for (;;) {
                                args.push_back(Expr());
                                if (IsOp(",")) {
                                    Next();
                                    continue;
                                }
                                break;
                            }
                        }
                        if (!IsOp(")")) {
                            error = "missing ')'";
                            return Make(Node::Kind::Number);
                        }
                        Next();
                        return Make(Node::Kind::Call, name, args);
                    }
                    return Make(Node::Kind::Variable, name);
                }
                if (IsOp("(")) {
                    Next();
                    int e = Expr();
                    if (!IsOp(")")) {
                        error = "missing ')'";
                        return e;
                    }
                    Next();
                    return e;
                }
                error = "unexpected '" + cur.text + "'";
                if (cur.type != Lexer::T::End)
                    Next();
                return Make(Node::Kind::Number);
            }
        };
    }

    struct CustomUniforms::Impl {
        struct Compiled {
            Def def;
            std::vector<Node> pool;
            int root = 0;
            Value value;
            int stamp = -1;
        };
        std::vector<Compiled> items;
        std::unordered_map<std::string, size_t> byName;
        struct Smooth {
            double current = 0;
            bool init = false;
        };
        std::map<int, Smooth> smooth;
        const FrameState* frame = nullptr;
        const UniformTable* table = nullptr;
        int stamp = 0;
        int depth = 0;

        Value Eval(Compiled& unit, int idx);
        Value Resolve(const std::string& name);
        void Run(Compiled& c);
    };

    CustomUniforms::CustomUniforms() : impl(std::make_unique<Impl>()) {}
    CustomUniforms::~CustomUniforms() = default;
    size_t CustomUniforms::Count() const {
        return impl->items.size();
    }
    bool CustomUniforms::Has(const std::string& name) const {
        return impl->byName.contains(name);
    }

    std::string CustomUniforms::TypeOf(const std::string& name) const {
        const auto it = impl->byName.find(name);
        return it == impl->byName.end() ? std::string() : impl->items[it->second].def.type;
    }

    std::vector<std::string>
    CustomUniforms::Load(const std::vector<std::pair<std::string, std::string>>& props) {
        std::vector<std::string> problems;
        impl->items.clear();
        impl->byName.clear();
        for (const auto& kv : props) {
            const std::string& key = kv.first;
            bool variable = false;
            std::string rest;
            if (key.compare(0, 8, "uniform.") == 0)
                rest = key.substr(8);
            else if (key.compare(0, 9, "variable.") == 0) {
                rest = key.substr(9);
                variable = true;
            } else
                continue;
            const size_t dot = rest.find('.');
            if (dot == std::string::npos) {
                problems.push_back("bad custom uniform key " + key);
                continue;
            }
            Impl::Compiled c;
            c.def.type = rest.substr(0, dot);
            c.def.name = rest.substr(dot + 1);
            c.def.source = kv.second;
            c.def.variable = variable;
            Parser parser(kv.second);
            c.root = parser.Parse();
            if (!parser.error.empty()) {
                problems.push_back(key + ": " + parser.error);
                continue;
            }
            c.pool = std::move(parser.pool);
            impl->byName[c.def.name] = impl->items.size();
            impl->items.push_back(std::move(c));
        }
        return problems;
    }

    Value CustomUniforms::Impl::Resolve(const std::string& name) {
        Value out;
        const auto it = byName.find(name);
        if (it != byName.end()) {
            Compiled& c = items[it->second];
            if (c.stamp != stamp) {
                if (depth > 32)
                    return out;
                c.stamp = stamp;
                depth++;
                Run(c);
                depth--;
            }
            return c.value;
        }
        if (name == "true") {
            out.v[0] = 1;
            return out;
        }
        if (name == "false") {
            out.v[0] = 0;
            return out;
        }
        if (name == "pi") {
            out.v[0] = 3.14159265358979323846;
            return out;
        }
        UniformTable::Context ctx;
        PackDrawState draw;
        ctx.frame = frame;
        ctx.draw = &draw;
        float tmp[16] = {};
        int comps = 0;
        std::string base = name, swizzle;
        const size_t dot = name.find('.');
        if (dot != std::string::npos) {
            base = name.substr(0, dot);
            swizzle = name.substr(dot + 1);
        }
        if (table->Evaluate(base, ctx, tmp, comps)) {
            if (swizzle.empty()) {
                out.v[0] = tmp[0];
                for (int i = 1; i < std::min(comps, 4); i++)
                    out.v[i] = tmp[i];
                out.n = std::min(comps, 4);
            } else {
                const char c = swizzle[0];
                const int idx = (c == 'x' || c == 'r' || c == 's')   ? 0
                                : (c == 'y' || c == 'g' || c == 't') ? 1
                                : (c == 'z' || c == 'b' || c == 'p') ? 2
                                                                     : 3;
                out.v[0] = idx < comps ? tmp[idx] : 0.0;
            }
        }
        return out;
    }

    void CustomUniforms::Impl::Run(Compiled& c) {
        c.value = Eval(c, c.root);
    }

    Value CustomUniforms::Impl::Eval(Compiled& unit, int idx) {
        const Node& n = unit.pool[static_cast<size_t>(idx)];
        Value r;
        auto num = [&](int child) { return Eval(unit, child).v[0]; };
        switch (n.kind) {
        case Node::Kind::Number:
            r.v[0] = n.number;
            return r;
        case Node::Kind::Variable:
            return Resolve(n.text);
        case Node::Kind::Unary: {
            const double a = num(n.children[0]);
            r.v[0] = n.text == "-" ? -a : (a == 0.0 ? 1.0 : 0.0);
            return r;
        }
        case Node::Kind::Binary: {
            const double a = num(n.children[0]);
            if (n.text == "&&") {
                r.v[0] = (a != 0.0 && num(n.children[1]) != 0.0) ? 1.0 : 0.0;
                return r;
            }
            if (n.text == "||") {
                r.v[0] = (a != 0.0 || num(n.children[1]) != 0.0) ? 1.0 : 0.0;
                return r;
            }
            const double b = num(n.children[1]);
            const std::string& o = n.text;
            if (o == "+")
                r.v[0] = a + b;
            else if (o == "-")
                r.v[0] = a - b;
            else if (o == "*")
                r.v[0] = a * b;
            else if (o == "/")
                r.v[0] = b == 0.0 ? 0.0 : a / b;
            else if (o == "%")
                r.v[0] = b == 0.0 ? 0.0 : std::fmod(a, b);
            else if (o == "==")
                r.v[0] = a == b;
            else if (o == "!=")
                r.v[0] = a != b;
            else if (o == "<")
                r.v[0] = a < b;
            else if (o == ">")
                r.v[0] = a > b;
            else if (o == "<=")
                r.v[0] = a <= b;
            else if (o == ">=")
                r.v[0] = a >= b;
            return r;
        }
        case Node::Kind::Call: {
            const std::string& f = n.text;
            const size_t argc = n.children.size();
            if (f == "if") {
                size_t i = 0;
                for (; i + 1 < argc; i += 2)
                    if (num(n.children[i]) != 0.0)
                        return Eval(unit, n.children[i + 1]);
                if (i < argc)
                    return Eval(unit, n.children[i]);
                return r;
            }
            if (f == "vec2" || f == "vec3" || f == "vec4") {
                const int want = f[3] - '0';
                r.n = want;
                for (int i = 0; i < want; i++)
                    r.v[i] = i < static_cast<int>(argc) ? num(n.children[static_cast<size_t>(i)]) : 0.0;
                return r;
            }
            std::vector<double> a;
            a.reserve(n.children.size());
            for (int c : n.children)
                a.push_back(num(c));
            auto arg = [&](size_t i) { return i < a.size() ? a[i] : 0.0; };
            if (f == "in") {
                r.v[0] = 0;
                for (size_t i = 1; i < a.size(); i++)
                    if (a[0] == a[i])
                        r.v[0] = 1;
                return r;
            }
            if (f == "smooth") {
                int id = 0;
                size_t base = 0;
                if (a.size() >= 4) {
                    id = static_cast<int>(a[0]);
                    base = 1;
                } else
                    id = idx * 131 + static_cast<int>(reinterpret_cast<std::uintptr_t>(&unit) & 0xffff);
                const double target = arg(base);
                const double up = arg(base + 1), down = a.size() > base + 2 ? arg(base + 2) : arg(base + 1);
                Smooth& s = smooth[id];
                if (!s.init) {
                    s.init = true;
                    s.current = target;
                }
                const double time = target > s.current ? up : down;
                const double dt = frame ? frame->frameTime : 0.016;
                const double k = time <= 0.0 ? 1.0 : 1.0 - std::exp(-dt / time);
                s.current += (target - s.current) * k;
                r.v[0] = s.current;
                return r;
            }
            if (f == "min") {
                double m = arg(0);
                for (size_t i = 1; i < a.size(); i++)
                    m = std::min(m, a[i]);
                r.v[0] = m;
                return r;
            }
            if (f == "max") {
                double m = arg(0);
                for (size_t i = 1; i < a.size(); i++)
                    m = std::max(m, a[i]);
                r.v[0] = m;
                return r;
            }
            if (f == "sin")
                r.v[0] = std::sin(arg(0));
            else if (f == "cos")
                r.v[0] = std::cos(arg(0));
            else if (f == "tan")
                r.v[0] = std::tan(arg(0));
            else if (f == "asin")
                r.v[0] = std::asin(arg(0));
            else if (f == "acos")
                r.v[0] = std::acos(arg(0));
            else if (f == "atan")
                r.v[0] = std::atan(arg(0));
            else if (f == "atan2")
                r.v[0] = std::atan2(arg(0), arg(1));
            else if (f == "sinh")
                r.v[0] = std::sinh(arg(0));
            else if (f == "cosh")
                r.v[0] = std::cosh(arg(0));
            else if (f == "tanh")
                r.v[0] = std::tanh(arg(0));
            else if (f == "sqrt")
                r.v[0] = std::sqrt(std::max(0.0, arg(0)));
            else if (f == "exp")
                r.v[0] = std::exp(arg(0));
            else if (f == "log")
                r.v[0] = std::log(arg(0));
            else if (f == "log10")
                r.v[0] = std::log10(arg(0));
            else if (f == "pow")
                r.v[0] = std::pow(arg(0), arg(1));
            else if (f == "abs")
                r.v[0] = std::fabs(arg(0));
            else if (f == "floor")
                r.v[0] = std::floor(arg(0));
            else if (f == "ceil")
                r.v[0] = std::ceil(arg(0));
            else if (f == "round")
                r.v[0] = std::floor(arg(0) + 0.5);
            else if (f == "trunc")
                r.v[0] = std::trunc(arg(0));
            else if (f == "frac")
                r.v[0] = Fract(arg(0));
            else if (f == "signum")
                r.v[0] = arg(0) > 0 ? 1.0 : (arg(0) < 0 ? -1.0 : 0.0);
            else if (f == "fmod")
                r.v[0] = arg(1) == 0.0 ? 0.0 : arg(0) - arg(1) * std::floor(arg(0) / arg(1));
            else if (f == "clamp")
                r.v[0] = std::min(std::max(arg(0), arg(1)), arg(2));
            else if (f == "lerp")
                r.v[0] = arg(0) + (arg(1) - arg(0)) * arg(2);
            else if (f == "toRad")
                r.v[0] = arg(0) * 3.14159265358979323846 / 180.0;
            else if (f == "toDeg")
                r.v[0] = arg(0) * 180.0 / 3.14159265358979323846;
            else if (f == "random")
                r.v[0] = static_cast<double>(std::rand()) / RAND_MAX;
            else if (f == "equals")
                r.v[0] = std::fabs(arg(0) - arg(1)) <= arg(2);
            else if (f == "between")
                r.v[0] = arg(0) >= arg(1) && arg(0) <= arg(2);
            else if (f == "float" || f == "int")
                r.v[0] = f == "int" ? std::trunc(arg(0)) : arg(0);
            return r;
        }
        }
        return r;
    }

    void CustomUniforms::Evaluate(const FrameState& state, const UniformTable& table) {
        impl->frame = &state;
        impl->table = &table;
        impl->stamp++;
        impl->depth = 0;
        for (Impl::Compiled& c : impl->items) {
            if (c.stamp == impl->stamp)
                continue;
            c.stamp = impl->stamp;
            impl->depth = 1;
            impl->Run(c);
        }
    }

    bool CustomUniforms::Get(const std::string& name, float* out, int& components) const {
        const auto it = impl->byName.find(name);
        if (it == impl->byName.end())
            return false;
        const Impl::Compiled& c = impl->items[it->second];
        const std::string& t = c.def.type;
        int n = 1;
        if (t == "vec2")
            n = 2;
        else if (t == "vec3")
            n = 3;
        else if (t == "vec4")
            n = 4;
        components = n;
        for (int i = 0; i < n; i++)
            out[i] = static_cast<float>(c.value.v[i]);
        if (t == "int")
            out[0] = std::trunc(out[0]);
        if (t == "bool")
            out[0] = out[0] != 0.0f ? 1.0f : 0.0f;
        return true;
    }

    UniformTable::UniformTable() {
        using C = const Context&;
        auto F = [](C c) -> const FrameState& { return *c.frame; };
        auto D = [](C c) -> const PackDrawState& { return *c.draw; };

        Add("gbufferModelView", 16, [=](C c, float* o) { CopyMat(F(c).modelView, o); });
        Add("gbufferModelViewInverse", 16, [=](C c, float* o) { Invert(F(c).modelView, o); });
        Add("gbufferProjection", 16, [=](C c, float* o) { CopyMat(F(c).projection, o); });
        Add("gbufferProjectionInverse", 16, [=](C c, float* o) { Invert(F(c).projection, o); });
        Add("gbufferPreviousModelView", 16, [=](C c, float* o) { CopyMat(F(c).prevModelView, o); });
        Add("gbufferPreviousProjection", 16, [=](C c, float* o) { CopyMat(F(c).prevProjection, o); });
        Add("shadowModelView", 16, [=](C c, float* o) { CopyMat(F(c).shadowModelView, o); });
        Add("shadowModelViewInverse", 16, [=](C c, float* o) { Invert(F(c).shadowModelView, o); });
        Add("shadowProjection", 16, [=](C c, float* o) { CopyMat(F(c).shadowProjection, o); });
        Add("shadowProjectionInverse", 16, [=](C c, float* o) { Invert(F(c).shadowProjection, o); });

        Add("iris_ModelViewMat", 16, [=](C c, float* o) { CopyMat(D(c).modelView, o); });
        Add("iris_ModelViewMatInverse", 16, [=](C c, float* o) { Invert(D(c).modelView, o); });
        Add("iris_ProjMat", 16, [=](C c, float* o) { CopyMat(D(c).projection, o); });
        Add("iris_ProjMatInverse", 16, [=](C c, float* o) { Invert(D(c).projection, o); });
        Add("iris_TextureMat", 32, [=](C c, float* o) {
            CopyMat(D(c).textureMatrix0, o);
            CopyMat(D(c).textureMatrix1, o + 16);
        });
        Add("iris_NormalMat", 9,
            [=](C c, float* o) { std::memcpy(o, D(c).normalMatrix, 9 * sizeof(float)); });
        Add("iris_FogColor", 4, [=](C c, float* o) { std::memcpy(o, D(c).fogColor, 4 * sizeof(float)); });
        Add("iris_FogDensity", 1, [=](C c, float* o) { o[0] = D(c).fogDensity; });
        Add("iris_FogStart", 1, [=](C c, float* o) { o[0] = D(c).fogStart; });
        Add("iris_FogEnd", 1, [=](C c, float* o) { o[0] = D(c).fogEnd; });
        Add("iris_FogScale", 1, [=](C c, float* o) {
            const float span = D(c).fogEnd - D(c).fogStart;
            o[0] = span > 0 ? 1.0f / span : 0.0f;
        });
        Add("iris_ViewHeight", 1, [=](C c, float* o) { o[0] = static_cast<float>(D(c).viewHeight); });

        Add("cameraPosition", 3, [=](C c, float* o) {
            for (int i = 0; i < 3; i++)
                o[i] = static_cast<float>(F(c).cameraPosition[i]);
        });
        Add("previousCameraPosition", 3, [=](C c, float* o) {
            for (int i = 0; i < 3; i++)
                o[i] = static_cast<float>(F(c).previousCameraPosition[i]);
        });
        Add("eyePosition", 3, [=](C c, float* o) {
            for (int i = 0; i < 3; i++)
                o[i] = static_cast<float>(F(c).cameraPosition[i]);
        });
        Add("sunPosition", 3, [=](C c, float* o) { std::memcpy(o, F(c).sunPosition, 12); });
        Add("moonPosition", 3, [=](C c, float* o) { std::memcpy(o, F(c).moonPosition, 12); });
        Add("shadowLightPosition", 3, [=](C c, float* o) { std::memcpy(o, F(c).shadowLightPosition, 12); });
        Add("upPosition", 3, [=](C c, float* o) { std::memcpy(o, F(c).upPosition, 12); });
        Add("skyColor", 3, [=](C c, float* o) { std::memcpy(o, F(c).skyColor, 12); });
        Add("fogColor", 3, [=](C c, float* o) { std::memcpy(o, F(c).fogColor, 12); });
        Add("entityColor", 4, [=](C c, float* o) {
            std::memcpy(o, D(c).overrideEntity ? D(c).entityColor : F(c).entityColor, 16);
        });
        Add("lightningBoltPosition", 4,
            [=](C c, float* o) { std::memcpy(o, F(c).lightningBoltPosition, 16); });
        Add("velocity", 3, [=](C c, float* o) { std::memcpy(o, F(c).velocity, 12); });
        Add("playerLookVector", 3, [=](C c, float* o) { std::memcpy(o, F(c).playerLookVector, 12); });

        Add("sunAngle", 1, [=](C c, float* o) { o[0] = F(c).sunAngle; });
        Add("shadowAngle", 1, [=](C c, float* o) { o[0] = F(c).shadowAngle; });
        Add("fogDensity", 1, [=](C c, float* o) { o[0] = F(c).fogDensity; });
        Add("fogStart", 1, [=](C c, float* o) { o[0] = F(c).fogStart; });
        Add("fogEnd", 1, [=](C c, float* o) { o[0] = F(c).fogEnd; });
        Add("fogMode", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).fogMode); });
        Add("fogShape", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).fogShape); });
        Add("worldTime", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).worldTime); });
        Add("worldDay", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).worldDay); });
        Add("moonPhase", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).moonPhase); });
        Add("frameCounter", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).frameCounter); });
        Add("frameTime", 1, [=](C c, float* o) { o[0] = F(c).frameTime; });
        Add("frameTimeCounter", 1, [=](C c, float* o) { o[0] = F(c).frameTimeCounter; });
        Add("rainStrength", 1, [=](C c, float* o) { o[0] = F(c).rainStrength; });
        Add("wetness", 1, [=](C c, float* o) { o[0] = F(c).wetness; });
        Add("thunderStrength", 1, [=](C c, float* o) { o[0] = F(c).thunderStrength; });
        Add("viewWidth", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).viewWidth); });
        Add("viewHeight", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).viewHeight); });
        Add("aspectRatio", 1, [=](C c, float* o) {
            o[0] = F(c).viewHeight > 0
                       ? static_cast<float>(F(c).viewWidth) / static_cast<float>(F(c).viewHeight)
                       : 1.0f;
        });
        Add("near", 1, [=](C c, float* o) { o[0] = F(c).nearPlane; });
        Add("far", 1, [=](C c, float* o) { o[0] = F(c).farPlane; });
        Add("isEyeInWater", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).isEyeInWater); });
        Add("eyeAltitude", 1, [=](C c, float* o) { o[0] = F(c).eyeAltitude; });
        Add("eyeBrightness", 2, [=](C c, float* o) {
            o[0] = static_cast<float>(F(c).eyeBrightness[0]);
            o[1] = static_cast<float>(F(c).eyeBrightness[1]);
        });
        Add("eyeBrightnessSmooth", 2, [=](C c, float* o) {
            o[0] = static_cast<float>(F(c).eyeBrightnessSmooth[0]);
            o[1] = static_cast<float>(F(c).eyeBrightnessSmooth[1]);
        });
        Add("blindness", 1, [=](C c, float* o) { o[0] = F(c).blindness; });
        Add("nightVision", 1, [=](C c, float* o) { o[0] = F(c).nightVision; });
        Add("screenBrightness", 1, [=](C c, float* o) { o[0] = F(c).screenBrightness; });
        Add("playerMood", 1, [=](C c, float* o) { o[0] = F(c).playerMood; });
        Add("heldItemId", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).heldItemId); });
        Add("heldItemId2", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).heldItemId2); });
        Add("heldBlockLightValue", 1,
            [=](C c, float* o) { o[0] = static_cast<float>(F(c).heldBlockLightValue); });
        Add("heldBlockLightValue2", 1,
            [=](C c, float* o) { o[0] = static_cast<float>(F(c).heldBlockLightValue2); });
        Add("hideGUI", 1, [=](C c, float* o) { o[0] = F(c).hideGUI ? 1.0f : 0.0f; });
        Add("centerDepthSmooth", 1, [=](C c, float* o) { o[0] = F(c).centerDepthSmooth; });
        Add("entityId", 1, [=](C c, float* o) {
            o[0] = static_cast<float>(D(c).overrideEntity ? D(c).entityId : F(c).entityId);
        });
        Add("blockEntityId", 1, [=](C c, float* o) {
            o[0] = static_cast<float>(D(c).overrideEntity ? D(c).blockEntityId : F(c).blockEntityId);
        });
        Add("currentRenderedItemId", 1, [=](C c, float* o) {
            o[0] = static_cast<float>(D(c).overrideEntity ? D(c).currentRenderedItemId
                                                          : F(c).currentRenderedItemId);
        });
        Add("renderStage", 1, [=](C c, float* o) { o[0] = static_cast<float>(D(c).renderStage); });
        Add("atlasSize", 2, [=](C c, float* o) {
            o[0] = static_cast<float>(F(c).atlasSize[0]);
            o[1] = static_cast<float>(F(c).atlasSize[1]);
        });
        // iris uniforms the engine has no live source for: constants that match what a pack sees in a plain overworld
        // iris: the alpha test reference of the program drawing (0 where it has none)
        Add("alphaTestRef", 1, [=](C c, float* o) { o[0] = D(c).alphaTestRef; });
        Add("bedrockLevel", 1, [=](C, float* o) { o[0] = -64.0f; });
        Add("heightLimit", 1, [=](C, float* o) { o[0] = 384.0f; });
        Add("logicalHeightLimit", 1, [=](C, float* o) { o[0] = 384.0f; });
        Add("hasSkylight", 1, [=](C, float* o) { o[0] = 1.0f; });
        Add("hasCeiling", 1, [=](C, float* o) { o[0] = 0.0f; });
        Add("constantMood", 1, [=](C, float* o) { o[0] = 0.0f; });
        Add("currentColorSpace", 1, [=](C, float* o) { o[0] = 0.0f; });
        Add("currentSelectedBlockId", 1, [=](C, float* o) { o[0] = -1.0f; });
        Add("currentSelectedBlockPos", 3, [=](C, float* o) { o[0] = o[1] = o[2] = 0.0f; });
        Add("endFlashIntensity", 1, [=](C, float* o) { o[0] = 0.0f; });
        Add("endFlashPosition", 3, [=](C, float* o) {
            o[0] = 0.0f;
            o[1] = 1.0f;
            o[2] = 0.0f;
        });
        Add("dhFarPlane", 1, [=](C, float* o) { o[0] = 0.0f; });
        Add("dhNearPlane", 1, [=](C, float* o) { o[0] = 0.0f; });
        Add("dhRenderDistance", 1, [=](C, float* o) { o[0] = 0.0f; });
        Add("heldBlockLightColor", 3, [=](C, float* o) { o[0] = o[1] = o[2] = 1.0f; });
        Add("heldBlockLightColor2", 3, [=](C, float* o) { o[0] = o[1] = o[2] = 1.0f; });
        Add("maxBlindnessDarkness", 1, [=](C c, float* o) { o[0] = F(c).blindness; });
        Add("cloudTime", 1, [=](C c, float* o) { o[0] = F(c).cloudTime; });
        Add("terrainIconSize", 1, [=](C c, float* o) { o[0] = F(c).terrainIconSize; });
        Add("blendFunc", 4, [=](C c, float* o) {
            for (int i = 0; i < 4; i++)
                o[i] = static_cast<float>(F(c).blendFunc[i]);
        });
        Add("currentPlayerHealth", 1,
            [=](C c, float* o) { o[0] = F(c).playerHealth / std::max(1.0f, F(c).maxPlayerHealth); });
        Add("maxPlayerHealth", 1, [=](C c, float* o) { o[0] = F(c).maxPlayerHealth; });
        Add("currentPlayerHunger", 1, [=](C c, float* o) { o[0] = F(c).playerHunger / 20.0f; });
        Add("maxPlayerHunger", 1, [=](C c, float* o) { o[0] = 1.0f; });
        Add("currentPlayerAir", 1,
            [=](C c, float* o) { o[0] = F(c).playerAir / std::max(1.0f, F(c).maxPlayerAir); });
        Add("maxPlayerAir", 1, [=](C c, float* o) { o[0] = 1.0f; });
        Add("currentPlayerArmor", 1, [=](C c, float* o) { o[0] = 0.0f; });
        Add("maxPlayerArmor", 1, [=](C c, float* o) { o[0] = 1.0f; });
        Add("is_sneaking", 1, [=](C c, float* o) { o[0] = F(c).isSneaking; });
        Add("is_sprinting", 1, [=](C c, float* o) { o[0] = F(c).isSprinting; });
        Add("is_hurt", 1, [=](C c, float* o) { o[0] = F(c).isHurt; });
        Add("is_invisible", 1, [=](C c, float* o) { o[0] = F(c).isInvisible; });
        Add("is_burning", 1, [=](C c, float* o) { o[0] = F(c).isBurning; });
        Add("is_on_ground", 1, [=](C c, float* o) { o[0] = F(c).isOnGround; });
        Add("is_spectator", 1, [=](C c, float* o) { o[0] = F(c).isSpectator; });
        Add("isSpectator", 1, [=](C c, float* o) { o[0] = F(c).isSpectator; });
        Add("isRightHanded", 1, [=](C c, float* o) { o[0] = F(c).isRightHanded; });
        Add("is_right_handed", 1, [=](C c, float* o) { o[0] = F(c).isRightHanded; });
        Add("cameraYaw", 1, [=](C c, float* o) { o[0] = F(c).cameraYaw; });
        Add("cameraPitch", 1, [=](C c, float* o) { o[0] = F(c).cameraPitch; });
        Add("inSwamp", 1, [=](C c, float* o) { o[0] = F(c).inSwamp; });

        Add("darknessFactor", 1, [=](C c, float* o) { o[0] = F(c).darknessFactor; });
        Add("darknessLightFactor", 1, [=](C c, float* o) { o[0] = F(c).darknessLightFactor; });
        Add("heavyFog", 1, [=](C c, float* o) { o[0] = F(c).heavyFog; });
        Add("cloudHeight", 1, [=](C c, float* o) { o[0] = F(c).cloudHeight; });
        Add("isElytraFlying", 1, [=](C c, float* o) { o[0] = F(c).isElytraFlying ? 1.0f : 0.0f; });
        Add("isNether", 1, [=](C c, float* o) { o[0] = F(c).dimensionNether ? 1.0f : 0.0f; });
        Add("isEnd", 1, [=](C c, float* o) { o[0] = F(c).dimensionEnd ? 1.0f : 0.0f; });
        Add("relativeEyePosition", 3, [=](C c, float* o) { std::memcpy(o, F(c).relativeEyePosition, 12); });
        Add("playerBodyVector", 3, [=](C c, float* o) { std::memcpy(o, F(c).playerBodyVector, 12); });
        Add("cameraPositionInt", 3, [=](C c, float* o) {
            for (int i = 0; i < 3; i++)
                o[i] = static_cast<float>(std::floor(F(c).cameraPosition[i]));
        });
        Add("cameraPositionFract", 3, [=](C c, float* o) {
            for (int i = 0; i < 3; i++)
                o[i] = static_cast<float>(Fract(F(c).cameraPosition[i]));
        });
        Add("previousCameraPositionInt", 3, [=](C c, float* o) {
            for (int i = 0; i < 3; i++)
                o[i] = static_cast<float>(std::floor(F(c).previousCameraPosition[i]));
        });
        Add("previousCameraPositionFract", 3, [=](C c, float* o) {
            for (int i = 0; i < 3; i++)
                o[i] = static_cast<float>(Fract(F(c).previousCameraPosition[i]));
        });

        Add("temperature", 1, [=](C c, float* o) { o[0] = F(c).biomeTemperature; });
        Add("rainfall", 1, [=](C c, float* o) { o[0] = F(c).biomeRainfall; });
        Add("biome", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).biome); });
        Add("biome_category", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).biomeCategory); });
        Add("biome_precipitation", 1,
            [=](C c, float* o) { o[0] = static_cast<float>(F(c).biomePrecipitation); });
        Add("day", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).worldDay); });
        Add("dayTime", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).worldTime); });
        Add("dayMoment", 1, [=](C c, float* o) {
            const double a = Fract(static_cast<double>(F(c).worldTime % 24000) / 24000.0 - 0.25 + 1.0);
            o[0] = static_cast<float>(a);
        });
        Add("player_mood", 1, [=](C c, float* o) { o[0] = F(c).playerMood; });
        Add("dimension", 1, [=](C c, float* o) { o[0] = static_cast<float>(F(c).dimensionId); });
        Add("sunAngleDegrees", 1, [=](C c, float* o) { o[0] = F(c).sunAngle * 360.0f; });
        Add("jitter", 2, [=](C c, float* o) {
            o[0] = F(c).jitter[0];
            o[1] = F(c).jitter[1];
        });
    }

    UniformTable::~UniformTable() = default;

    void UniformTable::Add(const char* name, int components, Fn fn) {
        Entry e;
        e.fn = std::move(fn);
        e.components = components;
        entries[name] = std::move(e);
    }

    bool UniformTable::Known(const std::string& name) const {
        return entries.contains(name) || (custom && custom->Has(name));
    }

    bool UniformTable::Evaluate(const std::string& name, const Context& ctx, float* out,
                                int& components) const {
        const auto it = entries.find(name);
        if (it != entries.end()) {
            it->second.fn(ctx, out);
            components = it->second.components;
            return true;
        }
        if (custom && custom->Get(name, out, components))
            return true;
        return false;
    }

    bool UniformTable::Scalar(const std::string& name, const Context& ctx, double& out) const {
        float tmp[16] = {};
        int comps = 0;
        if (!Evaluate(name, ctx, tmp, comps))
            return false;
        out = tmp[0];
        return true;
    }

    int NaturalComponents(const UniformDecl& d) {
        return d.components;
    }

    void PackUniform(const UniformDecl& d, const float* natural, float* regs) {
        const int count = std::max(1, d.arrayCount);
        const bool matrix = d.type.compare(0, 3, "mat") == 0;
        std::memset(regs, 0, sizeof(float) * 4 * static_cast<size_t>(d.regs));
        for (int e = 0; e < count; e++) {
            const float* src = natural + e * d.components;
            float* dst = regs + e * d.regsPerElement * 4;
            if (!matrix) {
                for (int i = 0; i < d.components && i < 4; i++)
                    dst[i] = src[i];
            } else if (d.type == "mat4") {
                std::memcpy(dst, src, 16 * sizeof(float));
            } else {
                const int dim = d.type == "mat3" ? 3 : 2;
                for (int col = 0; col < dim; col++)
                    for (int row = 0; row < dim; row++)
                        dst[col * 4 + row] = src[col * dim + row];
            }
        }
    }
}
