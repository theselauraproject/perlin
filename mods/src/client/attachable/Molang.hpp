#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::molang {
    struct Value {
        double n = 0;
        std::string s;
        bool isString = false;
        static Value Num(double v) {
            Value r;
            r.n = v;
            return r;
        }
        static Value Str(std::string v) {
            Value r;
            r.s = std::move(v);
            r.isString = true;
            return r;
        }
        bool Truthy() const { return isString ? !s.empty() : n != 0.0; }
    };
    using Args = std::vector<Value>;

    struct Host {
        virtual ~Host() = default;
        virtual bool Query(const std::string& name, const Args& args, Value& out) {
            (void)name;
            (void)args;
            (void)out;
            return false;
        }
        virtual bool Volatile(const std::string&) const { return true; }
        virtual bool Context(const std::string& name, Value& out) {
            (void)name;
            (void)out;
            return false;
        }
    };

    struct Scope {
        Host* host = nullptr;
        std::unordered_map<std::string, Value> variables;
        std::unordered_map<std::string, Value> temps;
        std::unordered_map<std::string, std::vector<Value>> arrays;
        bool cacheQueries = false;
        std::unordered_map<const void*, Value> queryCache;
        double keyFrameLerpTime = 0;
        double thisValue = 0;
        double thisVec[3] = {0, 0, 0};
    };

    class Expr {
    public:
        static std::shared_ptr<Expr> Parse(const std::string& text);
        static std::shared_ptr<Expr> Constant(double v);

        Value Eval(Scope& scope) const;
        double Number(Scope& scope) const;
        bool IsConstant() const;
        bool Ok() const { return ok; }

        struct Node;

    private:
        std::shared_ptr<Node> root;
        bool ok = true;
    };
}
