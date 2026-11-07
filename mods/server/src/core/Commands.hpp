#pragma once
// the command layer: every command declares its overloads in a small Java-style notation, and that one declaration drives what clients are told
// (AvailableCommands, so vanilla clients validate and autocomplete properly), what the server accepts, and the usage lines of /help and of syntax errors.
//
//   notation, one string per overload, after the command name:
//     word              a keyword                          time set <day|noon|night|midnight|ticks>
//     <name:type>       a required argument
//     [name:type]       an optional argument (everything after it is optional too)
//   types: int, float, string (one word or "quoted"), text (the rest of the line), targets (selector or player name, one or more players), target (exactly one),
//          profiles (selector or a player name that may be offline), pos (x y z, ~ and ^ allowed, block centres), blockpos (the same, whole blocks),
//          item, block, biome, enum(a|b|c)
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
        // keywords of a Literal or Enum
        std::vector<std::string> values;
    };
    using OverloadSpec = std::vector<ParamSpec>;

    // one parsed argument
    struct ArgValue {
        bool present = false;
        // keywords (lowercased), strings, text, item and block names as typed
        std::string text;
        int64_t integer = 0;
        double number = 0;
        // targets / target
        std::vector<Session*> players;
        // profiles: names, resolved from selectors when online
        std::vector<std::string> names;
        // pos / blockpos
        std::array<double, 3> pos{};
        // a matched table entry (biome) or the catalog's network id (item)
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
        // null for the console
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

    // "<mode:enum(survival|creative)> [target:targets]" -> parameters; false when the notation is malformed (a programming error, caught by the tests)
    bool ParseOverload(const std::string& notation, OverloadSpec& out, std::string* error = nullptr);

    struct MatchResult {
        bool ok = false;
        int overload = 0;
        std::map<std::string, ArgValue> values;
        // java-style: the reason, then the command typed up to the point it went wrong
        std::string error;
        std::string context;
    };
    MatchResult MatchCommand(Server& server, Session* source, const std::string& name,
                             const std::vector<OverloadSpec>& overloads, const std::string& args);

    // "/give <targets> <item> [<count>]"
    std::string UsageLine(const std::string& name, const OverloadSpec& overload);

    // the selector grammar on its own, for the tests: @s @p @a @r @e with [name=, distance=, x=, y=, z=, limit=, sort=, gamemode=, type=]; false with `error` set
    bool ResolveSelector(Server& server, Session* source, const std::string& text, std::vector<Session*>& out,
                         std::string& error);

    struct CommandDef;
    // the AvailableCommands packet for what `isOperator` may run
    void FillAvailableCommands(perlin::proto2225::AvailableCommandsPacket& out,
                               const std::map<std::string, CommandDef>& commands, bool isOperator);
}
