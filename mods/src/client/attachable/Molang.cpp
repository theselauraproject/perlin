#include "client/attachable/Molang.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>

namespace perlin::molang {
    struct Expr::Node {
        enum Kind {
            Num,
            Str,
            Var,
            Call,
            Index,
            Unary,
            Binary,
            Ternary,
            Coalesce,
            Assign,
            Block,
            Return
        } kind = Num;
        double number = 0;
        std::string text;
        char op = 0;
        std::vector<std::shared_ptr<Node>> kids;
        enum VarKind : int8_t {
            VUnset = -1,
            VVariable,
            VTemp,
            VQuery,
            VContext,
            VThis,
            VPi,
            VTrue,
            VFalse,
            VOther
        };
        mutable int8_t varKind = VUnset;
        mutable int8_t cacheable = -1;
        mutable int8_t isMath = -1;
        mutable std::string key;
    };

    namespace {
        using Node = Expr::Node;
        using NodePtr = std::shared_ptr<Node>;

        constexpr double kPi = 3.14159265358979323846;

        std::mt19937& Rng() {
            static std::mt19937 rng(0x5eed);
            return rng;
        }

        struct Token {
            enum Type { End, Number, String, Ident, Op } type = End;
            std::string text;
            double number = 0;
        };

        class Lexer {
        public:
            explicit Lexer(const std::string& s) : src(s) { Advance(); }
            const Token& Peek() const { return tok; }
            Token Take() {
                Token t = tok;
                Advance();
                return t;
            }
            bool Is(const char* op) const { return tok.type == Token::Op && tok.text == op; }
            bool Accept(const char* op) {
                if (!Is(op))
                    return false;
                Advance();
                return true;
            }

        private:
            void Advance() {
                while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])))
                    pos++;
                tok = {};
                if (pos >= src.size())
                    return;
                const char c = src[pos];
                if (std::isdigit(static_cast<unsigned char>(c)) ||
                    (c == '.' && pos + 1 < src.size() &&
                     std::isdigit(static_cast<unsigned char>(src[pos + 1])))) {
                    char* end = nullptr;
                    tok.number = std::strtod(src.c_str() + pos, &end);
                    pos = static_cast<size_t>(end - src.c_str());
                    if (pos < src.size() && (src[pos] == 'f' || src[pos] == 'F'))
                        pos++;
                    tok.type = Token::Number;
                    return;
                }
                if (c == '\'') {
                    size_t e = src.find('\'', pos + 1);
                    if (e == std::string::npos)
                        e = src.size();
                    tok.type = Token::String;
                    tok.text = src.substr(pos + 1, e - pos - 1);
                    pos = std::min(src.size(), e + 1);
                    return;
                }
                if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                    size_t e = pos;
                    while (e < src.size() && (std::isalnum(static_cast<unsigned char>(src[e])) ||
                                              src[e] == '_' || src[e] == '.'))
                        e++;
                    tok.type = Token::Ident;
                    tok.text = src.substr(pos, e - pos);
                    for (char& ch : tok.text)
                        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    pos = e;
                    return;
                }
                static const char* two[] = {"<=", ">=", "==", "!=", "&&", "||", "??"};
                for (const char* t : two)
                    if (src.compare(pos, 2, t) == 0) {
                        tok.type = Token::Op;
                        tok.text = t;
                        pos += 2;
                        return;
                    }
                tok.type = Token::Op;
                tok.text = std::string(1, c);
                pos++;
            }

            const std::string& src;
            size_t pos = 0;
            Token tok;
        };

        NodePtr Make(Node::Kind k) {
            auto n = std::make_shared<Node>();
            n->kind = k;
            return n;
        }

        std::string Normalise(std::string id) {
            struct Alias {
                const char* shortForm;
                const char* longForm;
            };
            static const Alias kAliases[] = {
                {"q.", "query."}, {"v.", "variable."}, {"c.", "context."}, {"t.", "temp."}};
            for (const Alias& a : kAliases)
                if (id.starts_with(a.shortForm))
                    return std::string(a.longForm) + id.substr(std::strlen(a.shortForm));
            return id;
        }

        class Parser {
        public:
            explicit Parser(const std::string& text) : lex(text) {}
            bool failed = false;

            NodePtr Program() {
                auto block = Make(Node::Block);
                while (lex.Peek().type != Token::End) {
                    if (lex.Accept(";"))
                        continue;
                    block->kids.push_back(Statement());
                    if (failed)
                        break;
                    if (!lex.Accept(";") && lex.Peek().type != Token::End) {
                        failed = true;
                        break;
                    }
                }
                return block;
            }

        private:
            NodePtr Statement() {
                if (lex.Peek().type == Token::Ident && lex.Peek().text == "return") {
                    lex.Take();
                    auto r = Make(Node::Return);
                    r->kids.push_back(Expression());
                    return r;
                }
                NodePtr left = Expression();
                if (lex.Is("=")) {
                    lex.Take();
                    if (left->kind != Node::Var) {
                        failed = true;
                        return left;
                    }
                    auto a = Make(Node::Assign);
                    a->text = left->text;
                    a->kids.push_back(Expression());
                    return a;
                }
                return left;
            }

            NodePtr Expression() { return Ternary(); }

            NodePtr Ternary() {
                NodePtr cond = Coalesce();
                if (lex.Accept("?")) {
                    auto t = Make(Node::Ternary);
                    t->kids.push_back(cond);
                    t->kids.push_back(Ternary());
                    if (lex.Accept(":"))
                        t->kids.push_back(Ternary());
                    else
                        t->kids.push_back(Constant(0));
                    return t;
                }
                return cond;
            }

            NodePtr Coalesce() {
                NodePtr l = Or();
                while (lex.Accept("??")) {
                    auto n = Make(Node::Coalesce);
                    n->kids = {l, Or()};
                    l = n;
                }
                return l;
            }

            NodePtr Binary(NodePtr l, char op, NodePtr r) {
                auto n = Make(Node::Binary);
                n->op = op;
                n->kids = {std::move(l), std::move(r)};
                return n;
            }

            NodePtr Or() {
                NodePtr l = And();
                while (lex.Accept("||"))
                    l = Binary(l, '|', And());
                return l;
            }
            NodePtr And() {
                NodePtr l = Equality();
                while (lex.Accept("&&"))
                    l = Binary(l, '&', Equality());
                return l;
            }
            NodePtr Equality() {
                NodePtr l = Relational();
                while (true) {
                    if (lex.Accept("=="))
                        l = Binary(l, '=', Relational());
                    else if (lex.Accept("!="))
                        l = Binary(l, '!', Relational());
                    else
                        return l;
                }
            }
            NodePtr Relational() {
                NodePtr l = Additive();
                while (true) {
                    if (lex.Accept("<="))
                        l = Binary(l, 'l', Additive());
                    else if (lex.Accept(">="))
                        l = Binary(l, 'g', Additive());
                    else if (lex.Accept("<"))
                        l = Binary(l, '<', Additive());
                    else if (lex.Accept(">"))
                        l = Binary(l, '>', Additive());
                    else
                        return l;
                }
            }
            NodePtr Additive() {
                NodePtr l = Multiplicative();
                while (true) {
                    if (lex.Accept("+"))
                        l = Binary(l, '+', Multiplicative());
                    else if (lex.Accept("-"))
                        l = Binary(l, '-', Multiplicative());
                    else
                        return l;
                }
            }
            NodePtr Multiplicative() {
                NodePtr l = Unary();
                while (true) {
                    if (lex.Accept("*"))
                        l = Binary(l, '*', Unary());
                    else if (lex.Accept("/"))
                        l = Binary(l, '/', Unary());
                    else
                        return l;
                }
            }
            NodePtr Unary() {
                if (lex.Accept("-")) {
                    auto n = Make(Node::Unary);
                    n->op = '-';
                    n->kids.push_back(Unary());
                    return n;
                }
                if (lex.Accept("+"))
                    return Unary();
                if (lex.Accept("!")) {
                    auto n = Make(Node::Unary);
                    n->op = '!';
                    n->kids.push_back(Unary());
                    return n;
                }
                return Primary();
            }

            static NodePtr Constant(double v) {
                auto n = Make(Node::Num);
                n->number = v;
                return n;
            }

            NodePtr Primary() {
                Token t = lex.Take();
                switch (t.type) {
                case Token::Number:
                    return Constant(t.number);
                case Token::String: {
                    auto n = Make(Node::Str);
                    n->text = t.text;
                    return n;
                }
                case Token::Ident: {
                    const std::string id = Normalise(t.text);
                    if (lex.Accept("(")) {
                        auto call = Make(Node::Call);
                        call->text = id;
                        if (!lex.Accept(")")) {
                            do {
                                call->kids.push_back(Expression());
                            } while (lex.Accept(","));
                            if (!lex.Accept(")"))
                                failed = true;
                        }
                        return call;
                    }
                    if (lex.Is("[")) {
                        lex.Take();
                        auto idx = Make(Node::Index);
                        idx->text = id;
                        idx->kids.push_back(Expression());
                        if (!lex.Accept("]"))
                            failed = true;
                        return idx;
                    }
                    auto v = Make(Node::Var);
                    v->text = id;
                    return v;
                }
                case Token::Op:
                    if (t.text == "{") {
                        auto block = Make(Node::Block);
                        while (!failed && lex.Peek().type != Token::End && !lex.Is("}")) {
                            if (lex.Accept(";"))
                                continue;
                            block->kids.push_back(Statement());
                            lex.Accept(";");
                        }
                        if (!lex.Accept("}"))
                            failed = true;
                        return block;
                    }
                    if (t.text == "(") {
                        NodePtr e = Expression();
                        if (!lex.Accept(")"))
                            failed = true;
                        return e;
                    }
                    [[fallthrough]];
                default:
                    failed = true;
                    return Constant(0);
                }
            }

            Lexer lex;
        };

        struct ReturnSignal {
            bool hit = false;
            Value value;
        };

        Value Eval(const Node& n, Scope& s, ReturnSignal& ret);

        double EvalNum(const Node& n, Scope& s, ReturnSignal& ret);

        Value Call(const Node& n, Scope& s, ReturnSignal& ret) {
            const std::string& name = n.text;
            if (n.isMath < 0) {
                n.isMath = name.starts_with("math.") ? 1 : 0;
                if (n.isMath)
                    n.key = name.substr(5);
            }
            double av[4] = {0, 0, 0, 0};
            Args args;
            if (n.isMath) {
                for (size_t i = 0; i < n.kids.size(); i++) {
                    const double v = EvalNum(*n.kids[i], s, ret);
                    if (i < 4)
                        av[i] = v;
                }
            } else {
                args.reserve(n.kids.size());
                for (const auto& k : n.kids)
                    args.push_back(Eval(*k, s, ret));
            }
            const auto a = [&](size_t i) { return i < 4 ? av[i] : 0.0; };
            if (n.isMath) {
                const std::string& f = n.key;
                const double deg = kPi / 180.0;
                if (f == "sin")
                    return Value::Num(std::sin(a(0) * deg));
                if (f == "cos")
                    return Value::Num(std::cos(a(0) * deg));
                if (f == "asin")
                    return Value::Num(std::asin(a(0)) / deg);
                if (f == "acos")
                    return Value::Num(std::acos(a(0)) / deg);
                if (f == "atan")
                    return Value::Num(std::atan(a(0)) / deg);
                if (f == "atan2")
                    return Value::Num(std::atan2(a(0), a(1)) / deg);
                if (f == "abs")
                    return Value::Num(std::fabs(a(0)));
                if (f == "sqrt")
                    return Value::Num(std::sqrt(std::max(0.0, a(0))));
                if (f == "exp")
                    return Value::Num(std::exp(a(0)));
                if (f == "ln")
                    return Value::Num(std::log(a(0)));
                if (f == "pow")
                    return Value::Num(std::pow(a(0), a(1)));
                if (f == "floor")
                    return Value::Num(std::floor(a(0)));
                if (f == "ceil")
                    return Value::Num(std::ceil(a(0)));
                if (f == "round")
                    return Value::Num(std::round(a(0)));
                if (f == "trunc")
                    return Value::Num(std::trunc(a(0)));
                if (f == "min")
                    return Value::Num(std::min(a(0), a(1)));
                if (f == "max")
                    return Value::Num(std::max(a(0), a(1)));
                if (f == "mod")
                    return Value::Num(a(1) != 0 ? std::fmod(a(0), a(1)) : 0.0);
                if (f == "clamp")
                    return Value::Num(std::min(std::max(a(0), a(1)), a(2)));
                if (f == "lerp")
                    return Value::Num(a(0) + (a(1) - a(0)) * a(2));
                if (f == "lerprotate") {
                    double d = std::fmod(a(1) - a(0), 360.0);
                    if (d > 180)
                        d -= 360;
                    if (d < -180)
                        d += 360;
                    return Value::Num(a(0) + d * a(2));
                }
                if (f == "min_angle") {
                    double v = std::fmod(a(0), 360.0);
                    if (v >= 180)
                        v -= 360;
                    if (v < -180)
                        v += 360;
                    return Value::Num(v);
                }
                if (f == "pi")
                    return Value::Num(kPi);
                if (f == "random")
                    return Value::Num(a(0) +
                                      (a(1) - a(0)) * std::uniform_real_distribution<double>(0, 1)(Rng()));
                if (f == "random_integer") {
                    const int lo = static_cast<int>(a(0)), hi = static_cast<int>(std::max(a(0), a(1)));
                    return Value::Num(std::uniform_int_distribution<int>(lo, hi)(Rng()));
                }
                if (f == "die_roll" || f == "die_roll_integer") {
                    double sum = 0;
                    for (int i = 0; i < static_cast<int>(a(0)); i++)
                        sum += a(1) + (a(2) - a(1)) * std::uniform_real_distribution<double>(0, 1)(Rng());
                    return Value::Num(f == "die_roll" ? sum : std::floor(sum));
                }
                if (f == "sign")
                    return Value::Num(a(0) > 0 ? 1.0 : a(0) < 0 ? -1.0 : 0.0);
                if (f == "copy_sign")
                    return Value::Num(std::copysign(std::fabs(a(0)), a(1)));
                if (f == "inverse_lerp")
                    return Value::Num(a(1) == a(0) ? 0.0 : (a(2) - a(0)) / (a(1) - a(0)));
                if (f == "sinh")
                    return Value::Num(std::sinh(a(0)));
                if (f == "cosh")
                    return Value::Num(std::cosh(a(0)));
                if (f == "tanh")
                    return Value::Num(std::tanh(a(0)));
                if (f.starts_with("ease_")) {
                    const double t = std::clamp(a(2), 0.0, 1.0);
                    const std::string k = f.substr(5);
                    auto in = [&](const std::string& n, double x) {
                        if (n == "sine")
                            return 1 - std::cos(x * kPi / 2);
                        if (n == "quad")
                            return x * x;
                        if (n == "cubic")
                            return x * x * x;
                        if (n == "quart")
                            return x * x * x * x;
                        if (n == "quint")
                            return x * x * x * x * x;
                        if (n == "expo")
                            return x == 0 ? 0.0 : std::pow(2.0, 10 * x - 10);
                        if (n == "circ")
                            return 1 - std::sqrt(1 - x * x);
                        if (n == "back")
                            return 2.70158 * x * x * x - 1.70158 * x * x;
                        if (n == "elastic")
                            return x == 0 || x == 1 ? x
                                                    : -std::pow(2.0, 10 * x - 10) *
                                                          std::sin((x * 10 - 10.75) * (2 * kPi / 3));
                        if (n == "bounce") {
                            const double y = 1 - x;
                            double r;
                            if (y < 1 / 2.75)
                                r = 7.5625 * y * y;
                            else if (y < 2 / 2.75) {
                                const double z = y - 1.5 / 2.75;
                                r = 7.5625 * z * z + 0.75;
                            } else if (y < 2.5 / 2.75) {
                                const double z = y - 2.25 / 2.75;
                                r = 7.5625 * z * z + 0.9375;
                            } else {
                                const double z = y - 2.625 / 2.75;
                                r = 7.5625 * z * z + 0.984375;
                            }
                            return 1 - r;
                        }
                        return x;
                    };
                    double w = t;
                    if (k.starts_with("in_out_")) {
                        const std::string n = k.substr(7);
                        w = t < 0.5 ? in(n, t * 2) / 2 : 1 - in(n, (1 - t) * 2) / 2;
                    } else if (k.starts_with("in_")) {
                        w = in(k.substr(3), t);
                    } else if (k.starts_with("out_")) {
                        w = 1 - in(k.substr(4), 1 - t);
                    }
                    return Value::Num(a(0) + (a(1) - a(0)) * w);
                }
                if (f == "hermite_blend") {
                    const double t = a(0);
                    return Value::Num(3 * t * t - 2 * t * t * t);
                }
                return Value::Num(0);
            }
            if (name == "query.key_frame_lerp_time")
                return Value::Num(s.keyFrameLerpTime);
            if (s.host && name.starts_with("query.")) {
                Value out;
                if (s.host->Query(name.substr(6), args, out))
                    return out;
            }
            return Value::Num(0);
        }

        double EvalNum(const Node& n, Scope& s, ReturnSignal& ret) {
            switch (n.kind) {
            case Node::Num:
                return n.number;
            case Node::Binary: {
                switch (n.op) {
                case '+':
                    return EvalNum(*n.kids[0], s, ret) + EvalNum(*n.kids[1], s, ret);
                case '-':
                    return EvalNum(*n.kids[0], s, ret) - EvalNum(*n.kids[1], s, ret);
                case '*':
                    return EvalNum(*n.kids[0], s, ret) * EvalNum(*n.kids[1], s, ret);
                case '/': {
                    const double l = EvalNum(*n.kids[0], s, ret);
                    const double r = EvalNum(*n.kids[1], s, ret);
                    return r != 0 ? l / r : 0.0;
                }
                case '<': {
                    const double l = EvalNum(*n.kids[0], s, ret);
                    return l < EvalNum(*n.kids[1], s, ret) ? 1.0 : 0.0;
                }
                case '>': {
                    const double l = EvalNum(*n.kids[0], s, ret);
                    return l > EvalNum(*n.kids[1], s, ret) ? 1.0 : 0.0;
                }
                case 'l': {
                    const double l = EvalNum(*n.kids[0], s, ret);
                    return l <= EvalNum(*n.kids[1], s, ret) ? 1.0 : 0.0;
                }
                case 'g': {
                    const double l = EvalNum(*n.kids[0], s, ret);
                    return l >= EvalNum(*n.kids[1], s, ret) ? 1.0 : 0.0;
                }
                default:
                    return Eval(n, s, ret).n;
                }
            }
            case Node::Unary:
                if (n.op == '-')
                    return -EvalNum(*n.kids[0], s, ret);
                return Eval(n, s, ret).n;
            case Node::Call:
                return Call(n, s, ret).n;
            default:
                return Eval(n, s, ret).n;
            }
        }

        Value Eval(const Node& n, Scope& s, ReturnSignal& ret) {
            switch (n.kind) {
            case Node::Num:
                return Value::Num(n.number);
            case Node::Str:
                return Value::Str(n.text);
            case Node::Var: {
                if (n.varKind == Node::VUnset) {
                    const std::string& id = n.text;
                    if (id.starts_with("variable.")) {
                        n.varKind = Node::VVariable;
                        n.key = id.substr(9);
                    } else if (id.starts_with("temp.")) {
                        n.varKind = Node::VTemp;
                        n.key = id.substr(5);
                    } else if (id.starts_with("query.")) {
                        n.varKind = Node::VQuery;
                        n.key = id.substr(6);
                    } else if (id.starts_with("context.")) {
                        n.varKind = Node::VContext;
                        n.key = id.substr(8);
                    } else if (id == "this")
                        n.varKind = Node::VThis;
                    else if (id == "math.pi")
                        n.varKind = Node::VPi;
                    else if (id == "true")
                        n.varKind = Node::VTrue;
                    else if (id == "false")
                        n.varKind = Node::VFalse;
                    else
                        n.varKind = Node::VOther;
                }
                switch (n.varKind) {
                case Node::VVariable: {
                    const auto it = s.variables.find(n.key);
                    return it != s.variables.end() ? it->second : Value::Num(0);
                }
                case Node::VTemp: {
                    const auto it = s.temps.find(n.key);
                    return it != s.temps.end() ? it->second : Value::Num(0);
                }
                case Node::VQuery: {
                    if (n.key == "key_frame_lerp_time")
                        return Value::Num(s.keyFrameLerpTime);
                    if (!s.host)
                        return Value::Str(n.text);
                    if (s.cacheQueries) {
                        if (n.cacheable < 0)
                            n.cacheable = s.host->Volatile(n.key) ? 0 : 1;
                        if (n.cacheable == 1) {
                            const auto it = s.queryCache.find(&n);
                            if (it != s.queryCache.end())
                                return it->second;
                            Value out;
                            if (!s.host->Query(n.key, {}, out))
                                out = Value::Num(0);
                            s.queryCache.emplace(&n, out);
                            return out;
                        }
                    }
                    Value out;
                    if (s.host->Query(n.key, {}, out))
                        return out;
                    return Value::Num(0);
                }
                case Node::VContext: {
                    if (!s.host)
                        return Value::Str(n.text);
                    Value out;
                    if (s.host->Context(n.key, out))
                        return out;
                    return Value::Num(0);
                }
                case Node::VThis:
                    return Value::Num(s.thisValue);
                case Node::VPi:
                    return Value::Num(kPi);
                case Node::VTrue:
                    return Value::Num(1);
                case Node::VFalse:
                    return Value::Num(0);
                default:
                    return Value::Str(n.text);
                }
            }
            case Node::Call:
                return Call(n, s, ret);
            case Node::Index: {
                const Value i = Eval(*n.kids[0], s, ret);
                const auto it = s.arrays.find(n.text);
                if (it == s.arrays.end() || it->second.empty())
                    return Value::Num(0);
                const long size = static_cast<long>(it->second.size());
                long idx = static_cast<long>(std::floor(i.n));
                idx = std::clamp(idx, 0L, size - 1);
                return it->second[static_cast<size_t>(idx)];
            }
            case Node::Unary: {
                const Value v = Eval(*n.kids[0], s, ret);
                return Value::Num(n.op == '-' ? -v.n : (v.Truthy() ? 0.0 : 1.0));
            }
            case Node::Binary: {
                if (n.op == '&') {
                    const Value l = Eval(*n.kids[0], s, ret);
                    return Value::Num(l.Truthy() && Eval(*n.kids[1], s, ret).Truthy() ? 1.0 : 0.0);
                }
                if (n.op == '|') {
                    const Value l = Eval(*n.kids[0], s, ret);
                    return Value::Num(l.Truthy() || Eval(*n.kids[1], s, ret).Truthy() ? 1.0 : 0.0);
                }
                const Value l = Eval(*n.kids[0], s, ret);
                const Value r = Eval(*n.kids[1], s, ret);
                if (n.op == '=' || n.op == '!') {
                    bool eq;
                    if (l.isString || r.isString) {
                        const auto text = [](const Value& v) {
                            std::string t = v.isString ? v.s : std::string();
                            for (char& c : t)
                                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            return t;
                        };
                        eq = l.isString && r.isString && text(l) == text(r);
                    } else {
                        eq = l.n == r.n;
                    }
                    return Value::Num((n.op == '=') == eq ? 1.0 : 0.0);
                }
                switch (n.op) {
                case '+':
                    return Value::Num(l.n + r.n);
                case '-':
                    return Value::Num(l.n - r.n);
                case '*':
                    return Value::Num(l.n * r.n);
                case '/':
                    return Value::Num(r.n != 0 ? l.n / r.n : 0.0);
                case '<':
                    return Value::Num(l.n < r.n);
                case '>':
                    return Value::Num(l.n > r.n);
                case 'l':
                    return Value::Num(l.n <= r.n);
                case 'g':
                    return Value::Num(l.n >= r.n);
                default:
                    return Value::Num(0);
                }
            }
            case Node::Ternary:
                return Eval(*n.kids[0], s, ret).Truthy() ? Eval(*n.kids[1], s, ret)
                                                         : Eval(*n.kids[2], s, ret);
            case Node::Coalesce: {
                const Value l = Eval(*n.kids[0], s, ret);
                return l.isString && l.s.contains('.') ? Eval(*n.kids[1], s, ret) : l;
            }
            case Node::Assign: {
                const Value v = Eval(*n.kids[0], s, ret);
                if (n.text.starts_with("variable."))
                    s.variables[n.text.substr(9)] = v;
                else if (n.text.starts_with("temp."))
                    s.temps[n.text.substr(5)] = v;
                return Value::Num(0);
            }
            case Node::Return: {
                ret.hit = true;
                ret.value = Eval(*n.kids[0], s, ret);
                return ret.value;
            }
            case Node::Block: {
                Value last;
                for (const auto& k : n.kids) {
                    last = Eval(*k, s, ret);
                    if (ret.hit)
                        return ret.value;
                }
                return last;
            }
            }
            return Value::Num(0);
        }

        bool Constant(const Node& n) {
            if (n.kind == Node::Num)
                return true;
            if (n.kind == Node::Block)
                return n.kids.size() == 1 && Constant(*n.kids[0]);
            if (n.kind == Node::Unary)
                return Constant(*n.kids[0]);
            if (n.kind == Node::Binary)
                return Constant(*n.kids[0]) && Constant(*n.kids[1]);
            return false;
        }
    }

    std::shared_ptr<Expr> Expr::Parse(const std::string& text) {
        auto e = std::make_shared<Expr>();
        Parser parser(text);
        e->root = parser.Program();
        if (parser.failed) {
            e->ok = false;
            e->root = std::make_shared<Node>();
        }
        return e;
    }

    std::shared_ptr<Expr> Expr::Constant(double v) {
        auto e = std::make_shared<Expr>();
        e->root = std::make_shared<Node>();
        e->root->number = v;
        return e;
    }

    Value Expr::Eval(Scope& scope) const {
        if (!root)
            return Value::Num(0);
        ReturnSignal ret;
        return molang::Eval(*root, scope, ret);
    }

    double Expr::Number(Scope& scope) const {
        if (!root)
            return 0.0;
        ReturnSignal ret;
        return molang::EvalNum(*root, scope, ret);
    }

    bool Expr::IsConstant() const {
        return root && molang::Constant(*root);
    }
}
