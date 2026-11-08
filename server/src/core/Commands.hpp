#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace perlin::proto2225 {
    struct AvailableCommandsPacket;
}

namespace perlin::server {

    class Server;
    class Session;

    enum class ArgKind {
        Literal,
        Enum,
        Int,
        Float,
        String,
        Text,
        Targets,
        Target,
        Profiles,
        Pos,
        BlockPos,
        Item,
        Block,
        Biome
    };

    struct ParamSpec {
        std::string name;
        ArgKind kind = ArgKind::String;
        bool optional = false;

        std::vector<std::string> values;
    };
    using OverloadSpec = std::vector<ParamSpec>;

    struct ArgValue {
        bool present = false;

        std::string text;
        int64_t integer = 0;
        double number = 0;

        std::vector<Session*> players;

        std::vector<std::string> names;

        std::array<double, 3> pos{};

        int index = -1;
        int block = -1;
    };

    struct CommandOrigin {
        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0;
        int dimension = 0;
        bool eyes = false;
    };
    const CommandOrigin* ActiveOrigin();
    class OriginScope {
    public:
        explicit OriginScope(const CommandOrigin& origin);
        ~OriginScope();
        OriginScope(const OriginScope&) = delete;
        OriginScope& operator=(const OriginScope&) = delete;

    private:
        const CommandOrigin* previous;
        CommandOrigin current;
    };
    int CommandDimension(Session* source);

    struct CommandCtx {
        Server& server;

        Session* source = nullptr;
        std::string raw;
        int overload = 0;
        std::map<std::string, ArgValue> values;

        bool Has(const std::string& name) const {
            const auto it = values.find(name);
            return it != values.end() && it->second.present;
        }
        const ArgValue& operator[](const std::string& name) const {
            static const ArgValue none;
            const auto it = values.find(name);
            return it == values.end() ? none : it->second;
        }
    };

    bool ParseOverload(const std::string& notation, OverloadSpec& out, std::string* error = nullptr);

    struct MatchResult {
        bool ok = false;
        int overload = 0;
        std::map<std::string, ArgValue> values;

        std::string error;
        std::string context;
    };
    MatchResult MatchCommand(Server& server, Session* source, const std::string& name,
                             const std::vector<OverloadSpec>& overloads, const std::string& args);

    std::string UsageLine(const std::string& name, const OverloadSpec& overload);

    bool ResolveSelector(Server& server, Session* source, const std::string& text, std::vector<Session*>& out,
                         std::string& error);

    struct CommandDef;

    void FillAvailableCommands(perlin::proto2225::AvailableCommandsPacket& out,
                               const std::map<std::string, CommandDef>& commands, bool isOperator);
}
