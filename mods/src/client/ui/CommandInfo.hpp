#pragma once
#include <string>
#include <vector>

namespace perlin::ui {
    struct CommandParam {
        std::string name;
        std::vector<std::string> values;
        bool target = false;
        bool optional = false;
        std::string softEnum;
    };
    struct CommandInfo {
        std::string name, description;
        std::vector<std::vector<CommandParam>> overloads;
    };
}
